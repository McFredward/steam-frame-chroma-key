#!/usr/bin/env python3
"""Read-only, bounded inspection of an explicitly selected SteamVR installation.

Reports static candidates, never a working mask/video association. Optional
copies are private diagnostic material and must not be committed or published.
No Steam configuration, registry, environment, running process or GPU is read.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys


MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 128 * 1024 * 1024
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_SHADER_FILES = 128
MAX_DIRECTORY_ENTRIES = 1024
MAX_INTERFACE_VERSIONS = 64
CHUNK_BYTES = 1024 * 1024
KNOWN_FILES = (
    "drivers/vrlink/bin/win64/driver_vrlink.dll",
    "drivers/vrlink/bin/linux64/driver_vrlink.so",
    "drivers/vrlink/driver.vrdrivermanifest",
    "bin/version.txt",
    "version.txt",
)
PRIVATE_COPY_FILES = frozenset(KNOWN_FILES[:3])
INTERFACES = re.compile(
    rb"(?:IVR(?:DriverDirectModeComponent|VirtualDisplay|Compositor"
    rb"(?:SystemInternal)?|DriverContext)|IServerTrackedDeviceProvider)"
    rb"_(?:[0-9]{1,3}|XXX)\x00"
)
SHADER_SUFFIXES = frozenset((
    ".spv", ".cso", ".vcs", ".dxbc", ".hlsl", ".glsl", ".frag", ".vert", ".comp"
))


def _inside(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _file_metadata(root, relative, budget, copy_root=None):
    result = {"path": relative}
    source = root / relative
    try:
        resolved = source.resolve(strict=True)
        if not _inside(resolved, root) or source.is_symlink():
            result["status"] = "rejected_external_or_symlink"
            return result
        before = resolved.stat()
        if not stat.S_ISREG(before.st_mode):
            result["status"] = "rejected_non_regular"
            return result
        result["bytes"] = before.st_size
        cap = MAX_MANIFEST_BYTES if relative.endswith(".vrdrivermanifest") else MAX_FILE_BYTES
        if before.st_size > cap or before.st_size > budget[0]:
            result["status"] = "skipped_size_or_total_budget"
            return result
        digest = hashlib.sha256()
        found = set()
        interfaces_truncated = False
        prefix = bytearray()
        tail = b""
        copied = None
        data = bytearray() if relative.endswith(".vrdrivermanifest") else None
        if copy_root is not None and relative in PRIVATE_COPY_FILES:
            copied = copy_root / relative
            copied.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        read_size = 0
        with resolved.open("rb") as input_file:
            output_file = copied.open("xb") if copied is not None else None
            try:
                if output_file is not None and hasattr(os, "fchmod"):
                    os.fchmod(output_file.fileno(), 0o600)
                while read_size < before.st_size:
                    chunk = input_file.read(min(CHUNK_BYTES, before.st_size - read_size))
                    if not chunk:
                        break
                    read_size += len(chunk)
                    budget[0] -= len(chunk)
                    digest.update(chunk)
                    if len(prefix) < 4096:
                        prefix.extend(chunk[:4096 - len(prefix)])
                    for match in INTERFACES.finditer(tail + chunk):
                        value = match.group(0)[:-1].decode("ascii")
                        if value in found or len(found) < MAX_INTERFACE_VERSIONS:
                            found.add(value)
                        else:
                            interfaces_truncated = True
                    tail = (tail + chunk)[-128:]
                    if data is not None:
                        data.extend(chunk)
                    if output_file is not None:
                        output_file.write(chunk)
                after = os.fstat(input_file.fileno())
            finally:
                if output_file is not None:
                    output_file.close()
        if read_size != before.st_size or after.st_size != before.st_size or \
                after.st_mtime_ns != before.st_mtime_ns or \
                after.st_ino != before.st_ino or after.st_dev != before.st_dev:
            raise ValueError("file_changed_during_read")
        result.update(status="inspected", sha256=digest.hexdigest(),
                      interface_strings=sorted(found))
        if interfaces_truncated:
            result["interface_inventory_truncated"] = True
        if prefix.startswith(b"MZ"):
            result["binary_format"] = "PE candidate"
            if len(prefix) >= 64:
                pe_offset = struct.unpack_from("<I", prefix, 0x3c)[0]
                if pe_offset <= len(prefix) - 6 and prefix[pe_offset:pe_offset + 4] == b"PE\0\0":
                    machine = struct.unpack_from("<H", prefix, pe_offset + 4)[0]
                    result["machine"] = {0x8664: "x86_64", 0xaa64: "aarch64",
                                         0x14c: "x86"}.get(machine, "other")
        elif prefix.startswith(b"\x7fELF"):
            result["binary_format"] = "ELF candidate"
        elif relative.endswith((".dll", ".so")):
            result["binary_format"] = "unrecognized"
        if data is not None:
            try:
                manifest = json.loads(data.decode("utf-8-sig"))
                if not isinstance(manifest, dict):
                    raise ValueError("manifest_not_object")
                selected = {}
                if manifest.get("name") == "vrlink":
                    selected["name"] = "vrlink"
                for field in ("redirectsDisplay", "resourceOnly", "alwaysActivate"):
                    if isinstance(manifest.get(field), bool):
                        selected[field] = manifest[field]
                result["manifest_fields"] = selected
            except (UnicodeError, ValueError, RecursionError):
                result["manifest_parse"] = "unrecognized"
        if copied is not None:
            result["private_copy"] = relative
        return result
    except FileNotFoundError:
        result["status"] = "missing"
    except (OSError, ValueError):
        result["status"] = "inspection_failed"
        if 'copied' in locals() and copied is not None:
            try:
                copied.unlink()
            except OSError:
                pass
    return result


def _shader_inventory(root):
    shader_root = root / "drivers/vrlink/resources/shaders"
    result = []
    warnings = []
    if not shader_root.is_dir():
        return result, ["Known vrlink shader directory is absent."]
    if shader_root.is_symlink() or not _inside(shader_root.resolve(), root):
        return result, ["Shader directory outside the selected root was rejected."]
    pending = [(shader_root, 0)]
    entries = 0
    while pending:
        directory, depth = pending.pop()
        try:
            # Enumerate only a bounded number; never follow junctions/symlinks.
            with os.scandir(directory) as iterator:
                for entry in iterator:
                    entries += 1
                    if entries > MAX_DIRECTORY_ENTRIES:
                        return result, ["Shader inventory entry limit reached."]
                    path = Path(entry.path)
                    if entry.is_symlink() or not _inside(path.resolve(), root):
                        continue
                    if entry.is_dir(follow_symlinks=False):
                        if depth < 4:
                            pending.append((path, depth + 1))
                        continue
                    if not entry.is_file(follow_symlinks=False) or \
                            path.suffix.lower() not in SHADER_SUFFIXES:
                        continue
                    name = path.relative_to(root).as_posix()
                    if len(name) > 512:
                        continue
                    result.append({"path": name, "bytes": entry.stat(follow_symlinks=False).st_size})
                    if len(result) >= MAX_SHADER_FILES:
                        return sorted(result, key=lambda x: x["path"]), \
                            ["Shader inventory file limit reached."]
        except OSError:
            warnings.append("A known shader directory could not be read.")
    return sorted(result, key=lambda x: x["path"]), warnings


def collect(steamvr_root, private_output=None):
    root = Path(steamvr_root).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("SteamVR root must be an existing directory")
    destination = None
    if private_output is not None:
        destination = Path(private_output).resolve()
        if _inside(destination, root):
            raise ValueError("Private output must be outside the inspected installation")
        # A fresh directory avoids overwriting files or following planted links.
        destination.mkdir(mode=0o700, parents=True, exist_ok=False)
        (destination / ".gitignore").write_text("*\n", encoding="ascii")
    budget = [MAX_TOTAL_BYTES]
    files = [_file_metadata(root, relative, budget, destination)
             for relative in KNOWN_FILES]
    shaders, warnings = _shader_inventory(root)
    return {
        "schema": "mr-pc-mask-source/1",
        "collection_scope": "Static known-file inspection; no process or runtime activation",
        "source_alpha_verified": False,
        "encoder_decoder_identity_verified": False,
        "interface_strings_are_not_component_or_abi_proof": True,
        "private_copies_requested": destination is not None,
        "max_read_bytes": MAX_TOTAL_BYTES,
        "inspected_read_bytes": MAX_TOTAL_BYTES - budget[0],
        "files": files,
        "shader_inventory": shaders,
        "warnings": warnings,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steamvr-root", required=True,
                        help="Explicit installed SteamVR root; no automatic discovery")
    parser.add_argument("--private-output",
                        help="Opt-in new private directory for driver/manifest copies")
    args = parser.parse_args(argv)
    try:
        report = collect(args.steamvr_root, args.private_output)
    except (OSError, ValueError):
        parser.error("Invalid/unreadable source or unsafe/reused private output directory")
    json.dump(report, sys.stdout, indent=2, ensure_ascii=True)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
