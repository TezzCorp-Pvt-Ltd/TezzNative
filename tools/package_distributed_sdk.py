import os, shutil, zipfile, tarfile, hashlib
from pathlib import Path

root = Path(__file__).resolve().parent.parent
site_dl = root / "TezzCorp_WebSites" / "tn_tezzcorp_com" / "download"
sdk_src = site_dl / "sdk"

print("Copying all runtime DLLs from sdk/bin to download/ ...")
dll_names = [
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

    # Add lib/*.tn files
    lib_dir = root / "lib"
    for tn in sorted(lib_dir.glob("*.tn")):
        zf.write(tn, f"lib/{tn.name}")
    # Also add any DLLs in lib if any
    for d in sorted(lib_dir.glob("*.dll")):
        zf.write(d, f"lib/{d.name}")
    print(f"  [ZIP] Added {len(list(lib_dir.glob('*.tn')))} standard library modules from lib/")

    # Add tools/ tree (including tezz.tn, probes, helper scripts)
    tools_dir = root / "tools"
    tool_count = 0
    for tf in tools_dir.rglob("*"):
        if tf.is_file() and not tf.name.endswith(('.obj', '.log', '.tmp', '.tar.gz')):
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

# Also update linux tar.gz if tezznative-sdk-linux.tar.gz exists
# update its lib files
linux_tar = site_dl / "tezznative-sdk-linux.tar.gz"
if linux_tar.exists():
    print("\nUpdating tezznative-sdk-linux.tar.gz standard library...")
    # Re-extract and repackage with updated lib
    tmp_extract = site_dl / "_tmp_linux_sdk"
    if tmp_extract.exists():
        shutil.rmtree(tmp_extract)
    tmp_extract.mkdir()
    try:
        with tarfile.open(linux_tar, "r:gz") as tar:
            tar.extractall(tmp_extract)
        # update lib/
        target_lib = tmp_extract / "lib"
        if not target_lib.exists():
            # check if inside a subfolder
            subs = list(tmp_extract.iterdir())
            if len(subs) == 1 and subs[0].is_dir():
                target_lib = subs[0] / "lib"
        if target_lib.exists():
            for tn in lib_dir.glob("*.tn"):
                shutil.copy2(tn, target_lib / tn.name)
            print(f"  Updated linux lib/*.tn files")
        
        with tarfile.open(linux_tar, "w:gz") as tar:
            for item in tmp_extract.iterdir():
                tar.add(item, arcname=item.name)
        print(f"Repackaged {linux_tar}")
        hl = hashlib.sha256(linux_tar.read_bytes()).hexdigest().lower()
        (site_dl / "tezznative-sdk-linux.tar.gz.sha256").write_text(f"{hl}  tezznative-sdk-linux.tar.gz\n", encoding="utf-8")
        print(f"Updated tezznative-sdk-linux.tar.gz.sha256: {hl}")
    finally:
        if tmp_extract.exists():
            shutil.rmtree(tmp_extract)

print("\nPackage generation completed successfully!")
