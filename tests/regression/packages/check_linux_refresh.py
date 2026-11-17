"""Linux SDK refresh must deploy matching code, compiler and runtime artifacts."""
import io
from pathlib import Path
import struct
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import package_distributed_sdk as packager


class LinuxRefresh(unittest.TestCase):
    def minimal_archive(self, root):
        (root / "bin").mkdir()
        elf = b"\x7fELF\x02\x01" + bytes(12) + struct.pack("<H", 62)
        for name in ("tezzc-linux-x64", "libtnrt-native.so", "libtzcpu-native.so"):
            (root / "bin" / name).write_bytes(elf)
        archive = root / "original.tar.gz"
        with tarfile.open(archive, "w:gz") as stream:
            entry = tarfile.TarInfo("lib")
            entry.type = tarfile.DIRTYPE
            stream.addfile(entry)
        return archive

    def test_refresh_updates_native_dependencies_and_tools(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            download = root / "download"
            sdk = download / "sdk"
            (sdk / "bin").mkdir(parents=True)
            (root / "tools").mkdir()
            (root / "lib").mkdir()
            library = root / "lib/stt_temporal.tn"
            library.write_text("// new TezzNative model\n", encoding="utf-8")
            (root / "tools/speech_temporal_train.tn").write_text("// new native driver\n", encoding="utf-8")
            (root / "tezz.mod").write_text("dep.stt_temporal = 0.1.0\n", encoding="utf-8")
            elf = b"\x7fELF\x02\x01" + bytes(12) + struct.pack("<H", 62) + bytes(44)
            for name in ("tezzc-linux-x64", "libtnrt-native.so", "libtzcpu-native.so"):
                (sdk / "bin" / name).write_bytes(elf + name.encode())
            archive = download / "tezznative-sdk-linux.tar.gz"
            with tarfile.open(archive, "w:gz") as stream:
                for name in ("lib/stt_temporal.tn", "bin/tezzc", "bin/tezzc-linux-x64", "bin/libtnrt-native.so", "bin/libtzcpu-native.so", "tools/speech_temporal_train.tn", "tezz.mod"):
                    data = b"old incompatible content"
                    entry = tarfile.TarInfo(name)
                    entry.size = len(data)
                    stream.addfile(entry, io.BytesIO(data))
            with mock.patch.multiple(packager, root=root, site_dl=download, sdk_src=sdk), \
                    mock.patch.object(packager, "verified_library_sources", return_value={"stt_temporal.tn": library}), \
                    mock.patch("builtins.print"):
                packager.main()
            with tarfile.open(archive) as stream:
                self.assertEqual(stream.extractfile("bin/tezzc").read(), elf + b"tezzc-linux-x64")
                self.assertEqual(stream.extractfile("bin/libtnrt-native.so").read(), elf + b"libtnrt-native.so")
                self.assertEqual(stream.extractfile("bin/libtzcpu-native.so").read(), elf + b"libtzcpu-native.so")
                self.assertEqual(stream.extractfile("tools/speech_temporal_train.tn").read(), (root / "tools/speech_temporal_train.tn").read_bytes())
                self.assertEqual(stream.extractfile("tezz.mod").read(), (root / "tezz.mod").read_bytes())
                self.assertEqual(stream.getmember("bin/tezzc").mode & 0o111, 0o111)

    def test_invalid_artifact_preserves_existing_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "bin").mkdir()
            for name in ("tezzc-linux-x64", "libtnrt-native.so", "libtzcpu-native.so"):
                (root / "bin" / name).write_bytes(b"MZ wrong operating system")
            archive = root / "original.tar.gz"
            archive.write_bytes(b"original archive must remain untouched")
            with self.assertRaisesRegex(ValueError, "ELF64"):
                packager.refresh_linux_archive(archive, {}, root, root)
            self.assertEqual(archive.read_bytes(), b"original archive must remain untouched")

    def test_unrecognized_layout_preserves_existing_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "bin").mkdir()
            elf = b"\x7fELF\x02\x01" + bytes(12) + struct.pack("<H", 62)
            for name in ("tezzc-linux-x64", "libtnrt-native.so", "libtzcpu-native.so"):
                (root / "bin" / name).write_bytes(elf)
            archive = root / "original.tar.gz"
            with tarfile.open(archive, "w:gz"):
                pass
            original = archive.read_bytes()
            with self.assertRaisesRegex(ValueError, "layout"):
                packager.refresh_linux_archive(archive, {}, root, root)
            self.assertEqual(archive.read_bytes(), original)

    def test_optional_gpu_is_validated(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.minimal_archive(root)
            original = archive.read_bytes()
            (root / "bin/libtzgpu-native.so").write_bytes(b"MZ not a Linux GPU library")
            with self.assertRaisesRegex(ValueError, "ELF64"):
                packager.refresh_linux_archive(archive, {}, root, root)
            self.assertEqual(archive.read_bytes(), original)

    def test_checksum_write_and_publish_failures_preserve_release_pair(self):
        for failure in ("write", "publish"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = self.minimal_archive(root)
                original = archive.read_bytes()
                sidecar = archive.with_name(archive.name + ".sha256")
                sidecar.write_bytes(b"previous checksum")
                original_write, original_replace = Path.write_text, Path.replace
                def write(path, *args, **kwargs):
                    if path.name == "updated.sha256" and failure == "write":
                        raise OSError("simulated checksum write failure")
                    return original_write(path, *args, **kwargs)
                def replace(path, target):
                    if path.name == "updated.sha256" and failure == "publish":
                        raise OSError("simulated checksum publication failure")
                    return original_replace(path, target)
                with mock.patch.object(Path, "write_text", write), mock.patch.object(Path, "replace", replace):
                    with self.assertRaisesRegex(OSError, "simulated"):
                        packager.refresh_linux_archive(archive, {}, root, root)
                self.assertEqual(archive.read_bytes(), original)
                self.assertEqual(sidecar.read_bytes(), b"previous checksum")


if __name__ == "__main__":
    unittest.main()
