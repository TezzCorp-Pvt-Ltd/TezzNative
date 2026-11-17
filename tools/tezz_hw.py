"""Build explicit TezzNative hardware profiles without a hosted runtime fallback."""
import argparse
import json
from pathlib import Path
import sys

from hardware.build import build
from hardware.profiles import PROFILES
from hardware.run import run_qemu


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="show implemented target profiles and their limits")
    builder = commands.add_parser("build")
    builder.add_argument("source", type=Path)
    builder.add_argument("--target", choices=PROFILES, required=True)
    builder.add_argument("--compiler", type=Path, required=True)
    builder.add_argument("--out", type=Path, required=True)
    runner = commands.add_parser("run")
    runner.add_argument("--target", choices=[name for name, profile in PROFILES.items() if "emulator" in profile], required=True)
    runner.add_argument("--out", type=Path, required=True)
    runner.add_argument("--expect", type=int, default=0)
    runner.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    if args.command == "list":
        for name, profile in PROFILES.items():
            print(name + ": " + profile["description"])
        return 0
    try:
        if args.command == "run":
            result = run_qemu(args.target, args.out, args.expect, args.timeout)
            print(json.dumps(result, indent=2))
            return int(result["status"] != "pass")
        result = build(args.target, args.source, args.compiler, args.out)
    except Exception as error:
        print("Hardware command failed: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps({key: result[key] for key in ("target", "status", "elf", "image", "image_sha256")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
