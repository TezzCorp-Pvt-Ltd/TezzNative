"""Execute generated C/CUDA-host reduced floats against independent bit oracles.

The CUDA-host lane is compiled as C++ with an attribute shim; it does not
establish GPU execution. All conversion inputs reach generated Tezz functions.
"""
import argparse
import bisect
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SUPPORT = Path(__file__).with_name("support")


def f32(bits):
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def bits32(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def small(bits, e5):
    sign = -1 if bits & 128 else 1
    width, bias = (2, 15) if e5 else (3, 7)
    exponent, mantissa = (bits & 127) >> width, bits & ((1 << width)-1)
    if exponent == 0:
        return sign * math.ldexp(mantissa / (1 << width), 1-bias)
    if e5 and exponent == 31:
        return math.nan if mantissa else sign * math.inf
    if not e5 and exponent == 15 and mantissa == 7:
        return math.nan
    return sign * math.ldexp(1 + mantissa / (1 << width), exponent-bias)


TABLES = {1: [f32(i << 16) for i in range(0x7f80)],
          2: [small(i, False) for i in range(127)],
          3: [small(i, True) for i in range(124)]}


def expected(operation, raw):
    if operation == 0:
        value = struct.unpack("<e", struct.pack("<H", raw))[0]
    elif operation == 1:
        value = f32(raw << 16)
    elif operation < 4:
        value = small(raw, operation == 3)
    else:
        mode, value = operation-4, f32(raw)
        if math.isnan(value):
            return None
        if mode == 0:
            try:
                return struct.unpack("<H", struct.pack("<e", value))[0]
            except OverflowError:
                return (0x8000 if raw >> 31 else 0) | 0x7c00
        sign = (raw >> (16 if mode == 1 else 24)) & (0x8000 if mode == 1 else 0x80)
        magnitude, values = abs(value), TABLES[mode]
        if mode == 1 and magnitude >= (values[-1] + math.ldexp(1.0, 128)) / 2:
            return sign | 0x7f80
        upper = bisect.bisect_left(values, magnitude)
        if upper == 0:
            nearest = 0
        elif upper >= len(values):
            nearest = len(values)-1
        else:
            lower = upper-1
            left, right = magnitude-values[lower], values[upper]-magnitude
            nearest = lower if left < right or (left == right and lower % 2 == 0) else upper
        return sign | nearest
    return None if math.isnan(value) else bits32(value)


def valid_nan(operation, raw, actual):
    if operation < 4:
        return actual & 0x7f800000 == 0x7f800000 and actual & 0x7fffff != 0
    mode = operation-4
    sign = (raw >> (16 if mode < 2 else 24)) & (0x8000 if mode < 2 else 0x80)
    if mode == 0:
        return actual & 0x8000 == sign and actual & 0x7c00 == 0x7c00 and actual & 0x3ff != 0
    if mode == 1:
        return actual & 0x8000 == sign and actual & 0x7f80 == 0x7f80 and actual & 0x7f != 0
    return actual == sign | 0x7f


def extra_inputs():
    inputs = {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
              0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
              0x7f800001, 0xff800001, 0x7fc00000, 0xffc00000}
    tables = [[struct.unpack("<e", struct.pack("<H", i))[0] for i in range(0x7c00)],
              TABLES[1], TABLES[2], TABLES[3]]
    for values in tables:
        # Cover every exponent transition and both parity choices around 1,
        # plus the subnormal and saturation boundaries in each representation.
        indices = {0, 1, 2, len(values)-2}
        indices.update(i for i in range(1, len(values)-1)
                       if math.frexp(values[i])[0] == 0.5)
        unit = bisect.bisect_left(values, 1.0)
        indices.update(range(unit, min(unit+4, len(values)-1)))
        for index in sorted(indices):
            midpoint = bits32((values[index] + values[index+1])/2)
            for raw in (midpoint-1, midpoint, midpoint+1):
                if 0 <= raw < 0x7f800000:
                    inputs.add(raw)
                    inputs.add(raw | 0x80000000)
    for value in (65504.0, 65520.0, 448.0, 464.0, 57344.0, 61440.0):
        raw = bits32(value)
        for nearby in (raw-1, raw, raw+1):
            inputs.add(nearby)
            inputs.add(nearby | 0x80000000)
    return sorted(inputs)


ENTRY = r'''
#undef main
#include <stdio.h>
#include <stdlib.h>
static FILE* output_file;
static uint32_t* extra_values;
static int64_t extra_count;
int64_t float_oracle_emit(int64_t operation,int64_t input,int64_t result){
  uint32_t record[3]={(uint32_t)operation,(uint32_t)input,(uint32_t)result};
  return fwrite(record,sizeof(record),1,output_file)==1 ? 0 : -1;
}
int64_t float_oracle_count(void){ return extra_count; }
int64_t float_oracle_input(int64_t index){ return extra_values[index]; }
int main(int argc,char** argv){
  if(argc!=3) return 90;
  FILE* input=fopen(argv[1],"rb"); if(!input) return 91;
  if(fseek(input,0,SEEK_END)) return 92;
  long bytes=ftell(input);
  if(bytes<0 || bytes%4 || bytes>1024*1024 || fseek(input,0,SEEK_SET)) return 93;
  extra_count=bytes/4;
  extra_values=(uint32_t*)malloc((size_t)bytes);
  if(!extra_values || fread(extra_values,4,(size_t)extra_count,input)!=(size_t)extra_count) return 94;
  fclose(input);
  output_file=fopen(argv[2],"wb"); if(!output_file) return 95;
  int status=(int)tn_language_main();
  if(fclose(output_file)) status=96;
  free(extra_values);
  return status;
}
'''

KERNEL_ENTRY = ENTRY.replace("int status=(int)tn_language_main();", r'''
  int status=0;
  for(int64_t i=0;i<65536+extra_count;i++){
    uint32_t data[9]={i<65536 ? (uint32_t)i*65537u : extra_values[i-65536]};
    reduced_kernel((int64_t)(uintptr_t)data);
    for(int mode=0;mode<4;mode++){
      if(float_oracle_emit(mode+4,data[0],data[mode+1])) status=97;
      if(float_oracle_emit(mode,data[0]&(mode<2 ? 65535u : 255u),data[mode+5])) status=98;
    }
  }
''')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--out", type=Path, default=ROOT/"build/verification/reduced-codegen")
    args = parser.parse_args()
    compiler, output = args.compiler.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(ROOT/"TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"))
    records = []
    extra = extra_inputs()
    vectors = output/"inputs.bin"
    vectors.write_bytes(struct.pack("<"+"I"*len(extra), *extra))

    def execute(command, timeout=120):
        result = subprocess.run([str(value) for value in command], cwd=ROOT, env=env,
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
        item = {"command": [str(value) for value in command], "exit": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
        if result.returncode:
            raise RuntimeError(json.dumps(item))
        return item

    # Exercise emitter APIs with deliberately unlowered IR. This cannot occur
    # in the normal frontend path, but must never silently generate wrong code.
    guard = {"lane": "emitters", "fixture": "unlowered_float_ir", "status": "fail", "steps": []}
    try:
        sdk = ROOT/"TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
        executable = output/("unlowered_float_ir.exe" if os.name == "nt" else "unlowered_float_ir")
        sources = [SUPPORT/"unlowered_float_ir.c", *[sdk/"src"/name for name in
                   ("ir_codegen_c.c", "ir_codegen_cuda.c", "types.c", "util.c")]]
        guard["steps"].append(execute([args.cc, "-std=c11", "-I"+str(sdk/"include"),
                                        *sources, "-o", executable]))
        for target, invalid, diagnostic in (("c", False, "C backend: this floating-point conversion is not implemented"),
                                            ("cuda", False, "CUDA backend: reduced floating-point conversions require IR lowering"),
                                            ("cuda", True, "CUDA backend: unsupported IR operation")):
            artifact = output/("unlowered-must-not-exist-"+target+("-unknown" if invalid else "")+".c")
            artifact.unlink(missing_ok=True)
            process = subprocess.run([str(executable), target, str(artifact), *(["unknown"] if invalid else [])], cwd=ROOT, env=env,
                                     capture_output=True, text=True, timeout=30)
            guard["steps"].append({"target": target, "unknown_opcode": invalid, "exit": process.returncode,
                                  "stderr": process.stderr, "artifact_absent": not artifact.exists()})
            if process.returncode == 0 or diagnostic not in process.stderr or artifact.exists():
                raise RuntimeError("Unlowered floating IR was not rejected before artifact creation: "+target)
        guard["status"] = "pass"
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        guard["error"] = str(error)
    records.append(guard)
    print("emitters unlowered_float_ir", guard["status"], flush=True)

    for lane, cc, language, target in (("c", args.cc, "c", "c"), ("cuda_host", args.cxx, "c++", "cuda")):
        fixtures = ("reduced_float", "aggregate_c", "indirect_c", "utf8_c")
        if target == "cuda": fixtures += ("reduced_kernel",)
        for fixture in fixtures:
            record = {"lane": lane, "fixture": fixture, "status": "fail", "steps": []}
            try:
                source = SUPPORT/(fixture+".tn")
                generated = output/(fixture+"-"+lane+".c")
                record["steps"].append(execute([compiler, "buildir", source, generated, "--target", target,
                                                 "--freestanding", "--experimental-gpu"]))
                standalone = generated.with_name(generated.stem+"-standalone.c")
                body = generated.read_text(encoding="utf-8")
                numeric = fixture in ("reduced_float", "reduced_kernel")
                entry = (KERNEL_ENTRY if fixture == "reduced_kernel" else ENTRY) if numeric else "\n#undef main\nint main(void){return (int)tn_language_main();}\n"
                standalone.write_text("#define main tn_language_main\n"+body+entry, encoding="utf-8")
                executable = generated.with_suffix(".exe" if os.name == "nt" else ".out")
                record["steps"].append(execute([cc, "-x", language, "-O2", "-fsanitize=undefined",
                                                 "-fno-sanitize-recover=undefined", "-I"+str(SUPPORT), standalone, "-o", executable]))
                binary = output/(lane+"-results.bin")
                arguments = [vectors, binary] if numeric else []
                record["steps"].append(execute([executable, *arguments]))
                if record["steps"][-1]["stderr"]:
                    raise RuntimeError("Unexpected executable stderr: "+record["steps"][-1]["stderr"])
                if numeric:
                    data = binary.read_bytes()
                    required = (65536+len(extra))*8 if fixture == "reduced_kernel" else 393728 + len(extra)*4
                    if len(data) != required*12:
                        raise RuntimeError(f"Expected {required} conversion records, got {len(data)} bytes")
                    mismatches = []
                    for operation, raw, actual in struct.iter_unpack("<III", data):
                        answer = expected(operation, raw)
                        good = valid_nan(operation, raw, actual) if answer is None else actual == answer
                        if not good and len(mismatches) < 12:
                            mismatches.append([operation, raw, actual, answer])
                    if mismatches:
                        raise RuntimeError(json.dumps(mismatches))
                    record.update(checks=required, exhaustive_decode_patterns=131584, extra_input_patterns=len(extra))
                record.update(status="pass", source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                              generated_sha256=hashlib.sha256(generated.read_bytes()).hexdigest())
            except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                record["error"] = str(error)
            records.append(record)
            print(lane, fixture, record["status"], flush=True)
    report = {"compiler": str(compiler), "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
              "cuda_device_execution": False, "results": records,
              "summary": {"pass": sum(row["status"] == "pass" for row in records),
                          "fail": sum(row["status"] != "pass" for row in records)}}
    (output/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    for row in records:
        if row["status"] != "pass": print(row.get("error"))
    print("REDUCED_CODEGEN_SUMMARY", report["summary"])
    return bool(report["summary"]["fail"])


if __name__ == "__main__":
    raise SystemExit(main())
