"""Install selected NVIDIA CUDA 12.6.3 Linux archives into an isolated prefix.

No system driver or global environment is changed. NVIDIA's manifest supplies
the archive SHA-256 and sizes; only the required compiler/runtime components
are downloaded. This is an explicit development setup command, never fallback.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import tarfile
import time
import threading
import urllib.request

BASE = "https://developer.download.nvidia.com/compute/cuda/redist/"
VERSION = "12.6.3"
COMPONENTS = ("cuda_nvcc", "cuda_cudart", "cuda_cccl", "cuda_sanitizer_api")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    cache = prefix / "archives"
    cache.mkdir(parents=True, exist_ok=True)
    url = BASE + "redistrib_" + VERSION + ".json"
    with urllib.request.urlopen(url, timeout=60) as response:
        raw = response.read()
    manifest = json.loads(raw)
    records = []
    connections = threading.BoundedSemaphore(4)

    def fetch(name):
        record = dict(manifest[name]["linux-x86_64"], component=name,
                      version=manifest[name]["version"])
        archive = cache / Path(record["relative_path"]).name
        if not archive.exists() or digest(archive) != record["sha256"]:
            temporary = archive.with_suffix(archive.suffix + ".partial")
            print("Downloading", name, int(record["size"]) // 1048576, "MiB", flush=True)
            size = int(record["size"])
            if size > 8 * 1024 * 1024:
                # Bounded range requests avoid very slow single CDN streams.
                if not temporary.exists() or temporary.stat().st_size != size:
                    with temporary.open("wb") as output:
                        output.truncate(size)
                block = 4 * 1024 * 1024
                def part(offset):
                    end = min(size, offset + block) - 1
                    # Sparse partial files contain only completed range writes.
                    # Reuse populated ranges; the complete SHA256 remains final.
                    if hasattr(os, "SEEK_DATA"):
                        with temporary.open("rb") as cached:
                            try:
                                data_start = os.lseek(cached.fileno(), offset, os.SEEK_DATA)
                                hole = os.lseek(cached.fileno(), offset, os.SEEK_HOLE)
                                if data_start == offset and hole >= end+1:
                                    return
                            except OSError:
                                pass
                    request = urllib.request.Request(BASE + record["relative_path"],
                        headers={"Range": f"bytes={offset}-{end}"})
                    for attempt in range(3):
                        try:
                            with connections:
                                with urllib.request.urlopen(request, timeout=120) as response:
                                    data = response.read()
                                    if response.status != 206 or len(data) != end-offset+1:
                                        raise RuntimeError("incorrect partial archive response")
                            with temporary.open("r+b") as output:
                                output.seek(offset)
                                output.write(data)
                            return
                        except (OSError, RuntimeError):
                            if attempt == 2:
                                raise
                            time.sleep(1)
                with ThreadPoolExecutor(max_workers=4) as parts:
                    list(parts.map(part, range(0, size, block)))
            else:
                with urllib.request.urlopen(BASE + record["relative_path"], timeout=120) as response:
                    with temporary.open("wb") as output:
                        shutil.copyfileobj(response, output, length=1024 * 1024)
            if temporary.stat().st_size != int(record["size"]) or digest(temporary) != record["sha256"]:
                raise RuntimeError("NVIDIA archive checksum/size mismatch: " + name)
            temporary.replace(archive)
        print("Verified", name, flush=True)
        return record, archive

    with ThreadPoolExecutor(max_workers=3) as executor:
        downloads = list(executor.map(fetch, COMPONENTS))
    for record, archive in downloads:
        name = record["component"]
        with tarfile.open(archive, "r:xz") as package:
            for member in package:
                parts = Path(member.name).parts
                if len(parts) < 2:
                    continue
                relative = Path(*parts[1:])
                # Preserve each component's license separately when flattening.
                if relative.name.startswith("LICENSE"):
                    relative = Path("licenses") / (name + "-" + relative.name)
                member.name = relative.as_posix()
                package.extract(member, path=prefix, filter="data")
        records.append(record)
        print("Installed", name, flush=True)
    report = {"version": VERSION, "manifest_url": url,
              "manifest_sha256": hashlib.sha256(raw).hexdigest(), "components": records,
              "driver_installed": False}
    (prefix / "toolchain.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("NVCC:", prefix / "bin/nvcc")


if __name__ == "__main__":
    main()
