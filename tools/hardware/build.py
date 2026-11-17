"""Compile TezzNative firmware using the selected C-IR hardware ABI."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from .profiles import PROFILES, ROOT, SDK


def execute(command, cwd, env=None, timeout=120):
    result = subprocess.run([str(arg) for arg in command], cwd=cwd, env=env,
                            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    record = {"command": [str(arg) for arg in command], "exit": result.returncode,
              "stdout": result.stdout, "stderr": result.stderr}
    if result.returncode:
        raise RuntimeError(json.dumps(record, indent=2))
    return record


def require_tool(name):
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"Required target tool is unavailable: {name}")
    return path


def build(target, source, compiler, output):
    profile = PROFILES[target]
    source, compiler, output = Path(source).resolve(), Path(compiler).resolve(), Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    generated, elf, image = output / "program.c", output / "firmware.elf", output / profile["image"]
    owned = (generated, elf, image, output / "run.json", output / "build.json")
    if source in owned or compiler in owned:
        raise RuntimeError("Build inputs must not use the builder's reserved output filenames")
    # Remove only this builder's fixed output filenames, so a failed build
    # cannot leave an earlier image looking like a successful new build.
    for path in owned:
        path.unlink(missing_ok=True)
    report = {"target": target, "source": str(source), "compiler": str(compiler),
              "status": "running", "steps": [], "source_sha256": {}}
    report_path = output / "build.json"
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))

    def run(command):
        record = execute(command, output, env)
        report["steps"].append(record)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    try:
        if not source.is_file() or not compiler.is_file():
            raise RuntimeError("Source file and source-built tezzc compiler must both exist")
        report["compiler_sha256"] = hashlib.sha256(compiler.read_bytes()).hexdigest()
        cc = require_tool(profile["cc"])
        inputs = [source, *profile["sources"], Path(__file__), Path(__file__).with_name("profiles.py")]
        inputs.extend((SDK / "runtime/hardware/common").glob("*.h"))
        if "linker" in profile:
            inputs.append(profile["linker"])
        for path in inputs:
            if not path.is_file():
                raise RuntimeError(f"Target implementation is missing: {path}")
            report["source_sha256"][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        if profile["format"] == "iso":
            for name in ("grub-mkrescue", "grub-file", "xorriso"):
                require_tool(name)
        else:
            require_tool(profile["objcopy"])
        run([cc, "--version"])
        run([compiler, "buildir", source, generated, "--target", target, "--freestanding"])
        flags = ["-std=gnu11", "-Os", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                 "-fno-pie", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
                 "-ffunction-sections", "-fdata-sections", *profile["flags"]]
        objects = []
        for index, path in enumerate([generated, *profile["sources"]]):
            obj = output / f"part-{index}.o"
            rename = ["-Dmain=tn_main"] if index == 0 else []
            run([cc, *flags, *rename, "-c", path, "-o", obj])
            report["source_sha256"][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
            objects.append(obj)
        link = [cc, *profile["link"], "-Wl,--gc-sections", "-Wl,-Map=" + str(output / "firmware.map")]
        if "linker" in profile:
            link += ["-nostdlib", "-no-pie", "-Wl,--build-id=none", "-Wl,-T," + str(profile["linker"])]
        run([*link, *objects, "-lgcc", "-o", elf])
        run([require_tool(profile["size"]), elf])
        if target == "arduino-uno":
            sections = execute([require_tool(profile["size"]), "-A", elf], output)
            report["steps"].append(sections)
            sizes = {fields[0]: int(fields[1]) for line in sections["stdout"].splitlines()
                     if len(fields := line.split()) >= 2 and fields[1].isdigit()}
            ram = sizes.get(".data", 0) + sizes.get(".bss", 0) + sizes.get(".noinit", 0)
            flash = sizes.get(".text", 0) + sizes.get(".data", 0)
            if flash > 32256 or ram > 1536:
                raise RuntimeError(f"Uno image exceeds budget: flash={flash}/32256, static RAM={ram}/1536 (512 bytes reserved for stack)")
            report["memory"] = {"flash_bytes": flash, "static_ram_bytes": ram, "stack_reserve_bytes": 512}
        if profile["format"] == "iso":
            run([require_tool("grub-file"), "--is-x86-multiboot2", elf])
            staging = output / "iso/boot/grub"
            staging.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(elf, staging.parent / "kernel.elf")
            (staging / "grub.cfg").write_text('set timeout=0\nset default=0\nmenuentry "TezzNative" {\n  multiboot2 /boot/kernel.elf\n  boot\n}\n', encoding="ascii")
            run([require_tool("grub-mkrescue"), "-o", image, staging.parent.parent])
        else:
            run([require_tool(profile["objcopy"]), "-O", profile["format"], elf, image])
        report.update(status="pass", elf=str(elf), image=str(image),
                      image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                      elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest())
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        image.unlink(missing_ok=True)
        elf.unlink(missing_ok=True)
        report.update(status="fail", error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report
