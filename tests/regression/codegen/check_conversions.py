"""Run real standalone generated C/CUDA-host conversion programs under UBSan."""
import argparse
import json
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--out", default="build/verification/conversion-codegen")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    compiler = str(Path(args.compiler).resolve())
    output = Path(args.out).resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["TEZZ_SDK_ROOT"] = str(root / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk")
    results = []
    for fixture in ("unsigned64_conversions", "integer_conversion_boundaries", "f32_arithmetic"):
        for lane, cc, language in (("c", args.cc, "c"), ("cuda_host", args.cxx, "c++")):
            generated = output / (fixture + "-" + lane + ".c")
            target = "c" if lane == "c" else "cuda"
            command = [compiler, "buildir", str(root / "tests/regression/floats" / (fixture + ".tn")),
                       str(generated), "--target", target, "--freestanding", "--experimental-gpu"]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
            assert result.returncode == 0, (command, result.stdout, result.stderr)
            standalone = generated.with_name(generated.stem + "-standalone.c")
            standalone.write_text("#define main tn_language_main\n" + generated.read_text() +
                                  "\n#undef main\nint main(void){return (int)tn_language_main();}\n")
            executable = generated.with_suffix(".out")
            command = [cc, "-x", language, "-O2", "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                       "-I" + str(Path(__file__).with_name("support")), str(standalone), "-o", str(executable)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, (command, result.stdout, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0 and not result.stderr, (fixture, lane, result.returncode, result.stdout, result.stderr)
            results.append({"fixture": fixture, "lane": lane, "status": "pass"})
            print("conversion_codegen_pass", fixture, lane, flush=True)
    (output / "results.json").write_text(json.dumps({"results": results, "cuda_device_execution": "not tested; host lane only"}, indent=2) + "\n")


if __name__ == "__main__":
    main()
