"""Shared source inventory and checksum-covered component manifests for libraries."""
from pathlib import Path, PurePosixPath
import re

COMPONENT_PREFIX = "// package-file "
MODULE_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_-]*\Z")


def source_checksum(path):
    value = 5381
    for byte in Path(path).read_bytes().replace(b"\r\n", b"\n"):
        value = (value * 33 + byte) & 0x7fffffff
    return f"{value:08X}"


def validate_name(name):
    if not MODULE_NAME.fullmatch(name):
        raise ValueError(f"invalid library name: {name!r}")
    return name


def component_manifest(facade):
    """Return the complete implementation inventory owned by one public module."""
    facade = Path(facade)
    directory = facade.with_suffix("")
    return [(path.relative_to(facade.parent).as_posix(), source_checksum(path))
            for path in sorted(directory.rglob("*.tn")) if path.is_file()]


def manifest_source(facade):
    facade = Path(facade)
    body = "".join(line for line in facade.read_text(encoding="utf-8").splitlines(True)
                   if not line.startswith(COMPONENT_PREFIX))
    header = "".join(f"{COMPONENT_PREFIX}{name} {checksum}\n"
                     for name, checksum in component_manifest(facade))
    return header + body


def read_components(facade):
    facade = Path(facade)
    entries = {}
    for line in facade.read_text(encoding="utf-8").splitlines():
        if not line.startswith(COMPONENT_PREFIX):
            continue
        fields = line[len(COMPONENT_PREFIX):].split()
        if len(fields) != 2:
            raise ValueError(f"malformed package component in {facade}")
        name, checksum = fields
        path = PurePosixPath(name)
        if (not re.fullmatch(r"[A-Za-z0-9_/-]+\.tn", name)
                or path.as_posix() != name or len(path.parts) < 2
                or path.parts[0] != facade.stem
                or not re.fullmatch(r"[0-9A-F]{8}", checksum)
                or name in entries):
            raise ValueError(f"invalid package component in {facade}: {name}")
        entries[name] = checksum
    return entries


def verify_components(facade):
    facade = Path(facade)
    declared = read_components(facade)
    actual = dict(component_manifest(facade))
    if declared != actual:
        raise ValueError(f"missing, modified, or unlisted package components: {facade}")
    return [facade.parent / name for name in declared]


def library_sources(repo):
    """Canonical public modules plus SDK-only modules, keeping relative paths."""
    repo = Path(repo)
    public = repo / "lib"
    sdk = repo / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk/lib"
    files = {path.relative_to(sdk).as_posix(): path
             for path in sorted(sdk.rglob("*.tn"))}
    for path in sorted(public.rglob("*.tn")):
        name = path.relative_to(public).as_posix()
        mirror = files.get(name)
        if mirror and (path.read_bytes().replace(b"\r\n", b"\n")
                       != mirror.read_bytes().replace(b"\r\n", b"\n")):
            raise ValueError(f"public/SDK library mismatch: {name}")
        files[name] = path
    return dict(sorted(files.items()))


def verified_library_sources(repo):
    files = library_sources(repo)
    for name, path in files.items():
        if "/" not in name:
            verify_components(path)
    return files
