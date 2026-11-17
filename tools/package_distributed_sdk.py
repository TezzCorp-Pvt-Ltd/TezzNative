import shutil, zipfile, tarfile, hashlib
import tempfile
from pathlib import Path
from library_package import verified_library_sources

root = Path(__file__).resolve().parent.parent
site_dl = root / "TezzCorp_WebSites" / "tn_tezzcorp_com" / "download"
sdk_src = site_dl / "sdk"


def refresh_linux_archive(archive, libraries, sdk, repository):
    """Refresh code and its matching native ABI together, preserving the old tar on failure."""
    binaries = {"tezzc": sdk / "bin/tezzc-linux-x64",
                "tezzc-linux-x64": sdk / "bin/tezzc-linux-x64",
                "libtnrt-native.so": sdk / "bin/libtnrt-native.so",
                "libtzcpu-native.so": sdk / "bin/libtzcpu-native.so"}
    def validate_elf(path):
        with path.open("rb") as stream:
            header = stream.read(20)
        if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01" or int.from_bytes(header[18:20], "little") != 62:
            raise ValueError("Linux SDK requires an ELF64 x64 artifact: " + str(path))
    for path in set(binaries.values()):
        validate_elf(path)
    gpu = sdk / "bin/libtzgpu-native.so"
    if gpu.is_file():
        validate_elf(gpu)
    with tempfile.TemporaryDirectory(prefix="linux-sdk-", dir=archive.parent) as temporary:
        staging = Path(temporary)
        extracted = staging / "contents"
        extracted.mkdir()
        with tarfile.open(archive, "r:gz") as stream:
            stream.extractall(extracted, filter="data")
        destination = extracted
        if not (destination / "lib").is_dir():
            children = list(extracted.iterdir())
            if len(children) != 1 or not children[0].is_dir() or not (children[0] / "lib").is_dir():
                raise ValueError("unrecognized Linux SDK archive layout")
            destination = children[0]
        for name, source in libraries.items():
            target = destination / "lib" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        (destination / "bin").mkdir(exist_ok=True)
        for name, source in binaries.items():
            target = destination / "bin" / name
            shutil.copy2(source, target)
            target.chmod(0o755)
        # Optional native GPU artifacts must not be silently retained at an older ABI.
        if gpu.is_file():
            shutil.copy2(gpu, destination / "bin/libtzgpu-native.so")
        elif (destination / "bin/libtzgpu-native.so").exists():
            raise ValueError("Linux archive contains CUDA but a current native CUDA library is missing")
        for source in (repository / "tools").rglob("*"):
            if source.is_file() and "__pycache__" not in source.parts and source.suffix in {".py", ".tn", ".ps1", ".sh", ".cmd", ".json", ".md"}:
                target = destination / "tools" / source.relative_to(repository / "tools")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
        for name in ("version.json", "tezz.mod", "tezz.lock", "registry.tnx", "tezz", "hello.tn"):
            source = repository / name
            if source.is_file():
                shutil.copy2(source, destination / name)
                if name == "tezz":
                    (destination / name).chmod(0o755)
        updated = staging / "updated.tar.gz"
        def executable_modes(member):
            # Windows chmod cannot represent POSIX execute bits in a staging tree.
            if member.isfile() and Path(member.name).name in (*binaries, "tezz"):
                member.mode = 0o755
            return member
        with tarfile.open(updated, "w:gz") as stream:
            for item in sorted(extracted.iterdir()):
                stream.add(item, arcname=item.name, filter=executable_modes)
        checksum = hashlib.sha256(updated.read_bytes()).hexdigest()
        sidecar = archive.with_name(archive.name + ".sha256")
        staged_sidecar = staging / "updated.sha256"
        staged_sidecar.write_text(checksum + "  " + archive.name + "\n", encoding="utf-8", newline="\n")
        backup = staging / "previous.tar.gz"
        previous_sidecar = staging / "previous.sha256"
        shutil.copy2(archive, backup)
        had_sidecar = sidecar.exists()
        if had_sidecar:
            shutil.copy2(sidecar, previous_sidecar)
        try:
            updated.replace(archive)
            staged_sidecar.replace(sidecar)
        except OSError:
            backup.replace(archive)
            if had_sidecar:
                previous_sidecar.replace(sidecar)
            elif sidecar.exists():
                sidecar.unlink()
            raise


def main():
    libraries = verified_library_sources(root)
    print("Copying all runtime DLLs from sdk/bin to download/ ...")
    dll_names = [
        "tnrt.dll",
        "tzcheckpoint.dll",
        "tzcpu.dll",
        "tzgguf.dll",
        "tzgpu.dll",
        "tzgpu_rtx3060.dll",
        "tzgpu_stub.dll",
        "tzgui.dll",
        "tzmnist.dll",
        "tzsafetensors.dll",
        "tztokenizer.dll",
    ]

    for dll in dll_names:
        src_dll = sdk_src / "bin" / dll
        if src_dll.exists():
            dst_dll = site_dl / dll
            shutil.copy2(src_dll, dst_dll)
            print(f"  Copied {dll} -> download/{dll}")
        else:
            print(f"  WARNING: {src_dll} not found!")

    # Also copy tezz_lsp.exe, tezzc.exe to site_dl if needed
    for exe in ["tezzc.exe", "tezz_lsp.exe"]:
        src_exe = sdk_src / "bin" / exe
        if src_exe.exists():
            shutil.copy2(src_exe, site_dl / exe)
            print(f"  Updated download/{exe}")

    print("\nPackaging tezznative-sdk.zip...")
    zip_path = site_dl / "tezznative-sdk.zip"
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        # Add manifest
        zf.writestr(".tezz_install_manifest", "tezznative-sdk v1.1.0\nfull-feature distributed build\n")

        # Add bin/ executables and ALL DLLs
        bin_files = [
            "tezz.cmd", "tezz.ps1", "tezzc.exe", "tezzc-windows-x64.exe", "tezzc-windows-arm64.exe", "tezz_lsp.exe"
        ] + dll_names
        for b in bin_files:
            p = sdk_src / "bin" / b
            if p.exists():
                zf.write(p, f"bin/{b}")
                print(f"  [ZIP] bin/{b}")
            else:
                # check root or site_dl
                alt = site_dl / b
                if alt.exists():
                    zf.write(alt, f"bin/{b}")
                    print(f"  [ZIP (from dl)] bin/{b}")
                else:
                    alt2 = root / b
                    if alt2.exists():
                        zf.write(alt2, f"bin/{b}")
                        print(f"  [ZIP (from root)] bin/{b}")
                    elif b == "tezzc-windows-x64.exe":
                        # Write tezzc.exe as tezzc-windows-x64.exe
                        src_c = root / "tezzc.exe"
                        if not src_c.exists():
                            src_c = site_dl / "tezzc.exe"
                        if src_c.exists():
                            zf.write(src_c, "bin/tezzc-windows-x64.exe")
                            print("  [ZIP] bin/tezzc-windows-x64.exe (from tezzc.exe)")

        # Include nested implementations and modules unique to the SDK.
        lib_dir = root / "lib"
        for name, source in libraries.items():
            zf.write(source, f"lib/{name}")
        # Also add any DLLs in lib if any
        for d in sorted(lib_dir.glob("*.dll")):
            zf.write(d, f"lib/{d.name}")
        print(f"  [ZIP] Added {len(libraries)} library source files")

        # Add tools/ tree (including tezz.tn, probes, helper scripts)
        tools_dir = root / "tools"
        tool_count = 0
        for tf in tools_dir.rglob("*"):
            if (tf.is_file() and "__pycache__" not in tf.parts
                    and tf.suffix in {".py", ".tn", ".ps1", ".sh", ".cmd", ".json", ".md"}):
                rel_tf = tf.relative_to(tools_dir)
                zf.write(tf, f"tools/{rel_tf.as_posix()}")
                tool_count += 1
        print(f"  [ZIP] Added {tool_count} tool files from tools/")

        # Add root metadata & launchers
        for meta in ["version.json", "tezz.mod", "tezz.lock", "registry.tnx", "tezz", "tezz.cmd", "tezz.ps1", "hello.tn"]:
            p = root / meta
            if p.exists():
                zf.write(p, meta)

    print(f"Created {zip_path} ({zip_path.stat().st_size} bytes)")

    # Update tezznative-sdk.zip.sha256
    h = hashlib.sha256(zip_path.read_bytes()).hexdigest().lower()
    (site_dl / "tezznative-sdk.zip.sha256").write_text(f"{h}  tezznative-sdk.zip\n", encoding="utf-8")
    print(f"Updated tezznative-sdk.zip.sha256: {h}")

    # Refresh Linux code and the native ABI artifacts it imports together.
    linux_tar = site_dl / "tezznative-sdk-linux.tar.gz"
    if linux_tar.exists():
        print("\nUpdating Linux SDK compiler, runtime, libraries and tools...")
        refresh_linux_archive(linux_tar, libraries, sdk_src, root)
        print(f"Repackaged {linux_tar}")

    print("\nPackage generation completed successfully!")


if __name__ == "__main__":
    main()
