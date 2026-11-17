"""Source-build Linux ARM64/RV64 CPU lanes and run them under qemu-user.

Requires cross GCC/libc and qemu-user. This verifies explicit emulated Linux
CPU profiles, not physical boards, GPU/NPU providers, TLS, or release packaging.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
sys.path.insert(0, str(ROOT / "tests"))
from run import execute, FAILURE

PROFILES = {
    "aarch64": {"cc": "aarch64-linux-gnu-gcc", "qemu": "qemu-aarch64", "machine": 183},
    "riscv64": {"cc": "riscv64-linux-gnu-gcc", "qemu": "qemu-riscv64", "machine": 243},
}
FIXTURES = [
    "math/known_answers", "math/boundaries", "floats/global_formats",
    "floats/rounding_boundaries", "floats/nan_comparisons", "floats/aggregate_calls",
    "floats/int64_conversions", "floats/unsigned64_conversions", "floats/differential",
    "nn/cross_entropy_value", "nn/cross_entropy_extreme", "nn/output_derivative", "nn/topk",
    "tensor/gradient_finite_difference", "tensor/view_chain_gradient",
    "tensor/softmax_axis_gradient", "tensor/rmsnorm_gradient", "tensor/mse_broadcast_gradient",
    "tensor/shape_guards", "tensor/repeat_backward", "tensor/contiguous_copy_ownership",
    "tensor_bridge/roundtrip", "tensor_bridge/gradient_boundary", "tensor_bridge/cpu_training",
    "tensor_bridge/invalid_inputs", "tensor_bridge/zero_stride",
    "speech/frontend", "speech/wav", "speech/audio_argument_errors", "speech/models",
    "runtime/linux_host_name", "runtime/time_units", "runtime/strcmp_contract",
    "io/directory_lifecycle", "io/whole_file",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_elf(path, machine):
    header = path.read_bytes()[:20]
    if (len(header) != 20 or header[:6] != b"\x7fELF\x02\x01"
            or int.from_bytes(header[18:20], "little") != machine):
        raise ValueError("artifact is not the selected little-endian ELF64 CPU: " + str(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT/"build/verification/hosted-cpu")
    parser.add_argument("--profile", choices=PROFILES, action="append")
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"host": platform.platform(), "execution": "qemu-user Linux CPU emulation",
              "physical_hardware": False, "tls_verified": False, "accelerators_verified": False,
              "native_executable_flow": "explicit generated-C cross compilation",
              "runner_sha256": digest(Path(__file__)), "results": [], "profiles": {}}
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    env.pop("PYTHONOPTIMIZE", None)

    def save():
        report["summary"] = {key: sum(row["status"] == key for row in report["results"])
                             for key in ("pass", "fail")}
        (output/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")

    def step(name, commands, folder, expected=0):
        row = {"case": name, "status": "fail", "steps": []}
        folder.mkdir(parents=True, exist_ok=True)
        try:
            for command in commands:
                result = execute(command, folder, env, 300)
                row["steps"].append(result)
                if (result["exit"] != expected or result["sanitizer_failure"]
                        or FAILURE.search(result["stdout"]+result["stderr"])):
                    break
            else:
                row["status"] = "pass"
        except (OSError, ValueError) as error:
            row["error"] = str(error)
        report["results"].append(row)
        save()
        print(row["status"].upper(), name, flush=True)
        if row["status"] != "pass":
            print(json.dumps(row, indent=2), flush=True)
        return row["status"] == "pass"

    if platform.system() != "Linux":
        parser.error("run this explicit Linux CPU gate on Linux or inside WSL")
    for target in args.profile or PROFILES:
        profile = PROFILES[target]
        folder = output/target
        folder.mkdir(parents=True, exist_ok=True)
        cc, qemu = shutil.which(profile["cc"]), shutil.which(profile["qemu"])
        sysroot = Path("/usr")/(target+"-linux-gnu")
        if not cc or not qemu or not sysroot.is_dir():
            report["results"].append({"case":target+"/dependencies", "status":"fail",
                                      "error":"cross GCC, cross libc sysroot and qemu-user are required"})
            save()
            continue
        guest = [qemu, "-L", str(sysroot)]
        compiler = folder/"tezzc"
        runtime, cpu = folder/"libtnrt-native.so", folder/"libtzcpu-native.so"
        speech = sorted((SDK/"runtime/speech").glob("*.c"))
        sources = [*sorted((SDK/"src").glob("*.c")), *speech,
                   SDK/"runtime/native_alloc.c", SDK/"runtime/args.c", SDK/"runtime/gpu_bridge.c",
                   SDK/"lib/tzcpu_kernels.c", *sorted((SDK/"include").glob("*.h")),
                   *sorted((SDK/"runtime/speech").glob("*.h")),
                   *sorted((SDK/"lib/tzcpu").glob("*.h"))]
        hashes = {str(path.relative_to(ROOT)):digest(path) for path in sources}
        report["profiles"][target] = {"source_sha256":hashes, "sysroot":str(sysroot),
            "compiler_tool":execute([cc,"--version"],ROOT,env,30),
            "emulator_tool":execute([qemu,"--version"],ROOT,env,30)}
        flags = [cc,"-std=gnu11","-O1","-I"+str(SDK/"include"),"-DTN_ENABLE_HOST_GUI=1"]
        links = ["-lpthread","-lm","-ldl"]
        commands = [
            [*flags,*sorted((SDK/"src").glob("*.c")),*speech,"-o",compiler,*links],
            [*flags,"-shared","-fPIC","-DTN_NATIVE_X64_ABI=1",SDK/"src/tnrt.c",
             SDK/"src/host_gui_backend.c",SDK/"src/security.c",SDK/"src/decimal.c",
             SDK/"runtime/native_alloc.c",SDK/"runtime/args.c",SDK/"runtime/gpu_bridge.c",
             *speech,"-o",runtime,*links],
            [*flags,"-shared","-fPIC","-DTN_NATIVE_X64_ABI=1",SDK/"lib/tzcpu_kernels.c",
             "-o",cpu,*links],
        ]
        # Stale executables cannot satisfy a failed source build.
        for artifact in (compiler,runtime,cpu):
            artifact.unlink(missing_ok=True)
        if not step(target+"/source_build",commands,folder):
            continue
        try:
            for artifact in (compiler,runtime,cpu):
                verify_elf(artifact,profile["machine"])
            if any(digest(ROOT/path)!=value for path,value in hashes.items()):
                raise ValueError("source changed during cross build")
            report["profiles"][target]["artifact_sha256"] = {
                path.name:digest(path) for path in (compiler,runtime,cpu)}
        except (OSError,ValueError) as error:
            report["results"].append({"case":target+"/artifact_identity","status":"fail","error":str(error)})
            save()
            continue
        for case in FIXTURES:
            source = ROOT/"tests/regression"/(case+".tn")
            work = folder/"bc"/case
            commands = []
            if case.startswith("speech/"):
                commands.append([sys.executable,ROOT/"tests/regression/speech/prepare_fixtures.py",
                                 work/"build/speech-regression/fixtures"])
            commands.append([*guest,compiler,"run",source,"--bc"])
            if case=="floats/differential":
                commands.append([sys.executable,ROOT/"tests/regression/floats/check_bits.py",work])
            step(target+"/bc/"+case,commands,work)
        for case in ("aggregate_c","indirect_c","utf8_c"):
            work = folder/"generated_c"/case
            source = ROOT/"tests/regression/codegen/support"/(case+".tn")
            generated,obj,executable = work/"program.c",work/"program.o",work/"program"
            commands = [
                [*guest,compiler,"buildir",source,generated,"--target","c","--freestanding"],
                [cc,"-std=c11","-O2","-Dmain=tezz_entry","-c",generated,"-o",obj],
                [cc,obj,ROOT/"tests/regression/codegen/support/c_entry.c","-o",executable],
                [*guest,executable],
            ]
            step(target+"/generated_c/"+case,commands,work)
        for case,arguments in (("stt_model_test",["stt.tns"]),("tts_model_test",["tts.tns"]),
                               ("duration_expectation",[]),("duration_bounds",[]),("vocoder_bounds",[])):
            work = folder/"native_speech"/case
            executable = work/"program"
            step(target+"/native_speech/"+case,[
                [*flags,"-I"+str(SDK/"runtime/speech"),ROOT/"tests/regression/speech"/(case+".c"),
                 *speech,"-o",executable,*links], [*guest,executable,*arguments]],work)
    save()
    print("HOSTED_CPU_SUMMARY",report["summary"],flush=True)
    return int(report["summary"]["fail"]!=0 or not report["results"])


if __name__=="__main__":
    raise SystemExit(main())
