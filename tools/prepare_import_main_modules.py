#!/usr/bin/env python3
"""Offline bounded hash/layout descriptors; emits no native bytes or instructions.

Native roles are exact reviewed AArch64 inputs. Observer descriptors require a
separate reviewed artifact SHA. Fixture descriptors cannot enable preparation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct

MAX_FILE = 8 * 1024 * 1024
MAX_MAPPED = 16 * 1024 * 1024
MAX_PHDR = 16
MAX_NOTES = 4
PROFILE_BYTES = 2032
NATIVE = {
    1: ("vrlink", "31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153"),
    # The receiver actually binds the Steam-runtime API. The distinct /opt
    # SteamVR copy is deliberately not an alternative accepted role-2 input.
    2: ("libopenvr_api.so", "4a8723b7134c53ec6c9291d9d2f55ea86349ffd0d227d10ae49a4951ae5395d4"),
    3: ("libc.so.6", "be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b"),
    4: ("ld-linux-aarch64.so.1", "2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e"),
}
# Keep NATIVE's receiver set unchanged: receiver preparation iterates it and
# must never start requiring the unrelated compositor file/API namespace.
COMPOSITOR = ("vrcompositor", "0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96")


def digest(data):
    return hashlib.sha256(data).digest()


def profile_from_bytes(data, role, reviewed_sha=None):
    if not 64 <= len(data) <= MAX_FILE or role not in range(1, 8):
        raise ValueError("Unsupported bounded descriptor input")
    sha = hashlib.sha256(data).hexdigest()
    native = COMPOSITOR if role == 7 else NATIVE.get(role)
    if native and sha != native[1]:
        raise ValueError("Unsupported native identity")
    if role == 5 and (not reviewed_sha or sha != reviewed_sha):
        raise ValueError("Observer requires the independently reviewed artifact SHA")
    h = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    if h[0][:7] != b"\x7fELF\x02\x01\x01" or h[1] != 3 or h[3] != 1 or h[8:10] != (64, 56):
        raise ValueError("Unsupported ELF64 little-endian dynamic layout")
    if h[2] not in (62, 183) or role != 6 and h[2] != 183:
        raise ValueError("Native preparation requires reviewed AArch64 modules")
    phoff, phnum = h[5], h[10]
    if not 1 <= phnum <= MAX_PHDR or phoff > len(data) or phnum * 56 > len(data) - phoff:
        raise ValueError("Program headers outside bounded input")
    phdrs = [struct.unpack_from("<IIQQQQQQ", data, phoff + 56 * i) for i in range(phnum)]
    loads, notes, hashes, code, build_id = [], [], [], bytearray(), None
    mapped_span, readonly_total = 0, 0
    for ph in phdrs:
        kind, flags, off, va, pa, filesz, memsz, align = ph
        if off > len(data) or filesz > len(data) - off or memsz < filesz:
            raise ValueError("Program header outside owned file")
        if kind == 1:
            if not flags & 4 or flags & 1 and flags & 2 or va > MAX_MAPPED or memsz > MAX_MAPPED - va:
                raise ValueError("Unsupported load permissions/span")
            if any(va < end and start < va + memsz for start, end in loads):
                raise ValueError("Overlapping loads")
            loads.append((va, va + memsz))
            mapped_span = max(mapped_span, va + memsz)
            if not flags & 2:
                readonly_total += filesz
        owned = data[off:off + filesz]
        hashes.append(digest(owned) if kind == 1 and flags & 1 else bytes(32))
        if kind == 1 and flags & 1:
            code.extend(owned)
        if kind == 4:
            if len(notes) == MAX_NOTES or not 12 <= filesz <= 256:
                raise ValueError("Unsupported bounded note segment")
            notes.append((va, filesz, digest(owned)))
            cursor = 0
            while cursor < len(owned):
                if len(owned) - cursor < 12:
                    raise ValueError("Truncated note")
                namesz, descsz, note_type = struct.unpack_from("<III", owned, cursor)
                name_at = cursor + 12
                desc_at = name_at + ((namesz + 3) & ~3)
                end = desc_at + ((descsz + 3) & ~3)
                if end > len(owned):
                    raise ValueError("Note outside owned segment")
                if note_type == 3 and namesz == 4 and owned[name_at:name_at + 4] == b"GNU\0":
                    if build_id is not None or not 1 <= descsz <= 64:
                        raise ValueError("Ambiguous build identity")
                    build_id = owned[desc_at:desc_at + descsz]
                cursor = end
    if not code or not notes or build_id is None or readonly_total > MAX_FILE:
        raise ValueError("Missing/excessive code or build identity")
    blob = struct.pack("<10Q", 1, role, h[2], h[1], len(data), phoff, phnum, len(notes), len(build_id), mapped_span)
    blob += digest(data[:64]) + digest(data[phoff:phoff + phnum * 56]) + digest(code) + digest(data)
    blob += build_id + bytes(64 - len(build_id))
    blob += b"".join(struct.pack("<8Q", *ph) for ph in phdrs) + bytes((MAX_PHDR - phnum) * 64)
    blob += b"".join(hashes) + bytes((MAX_PHDR - phnum) * 32)
    blob += b"".join(struct.pack("<2Q", va, size) + hash_ for va, size, hash_ in notes) + bytes((MAX_NOTES - len(notes)) * 48)
    fingerprint = digest(blob)
    blob += fingerprint
    if len(blob) != PROFILE_BYTES:
        raise ValueError("Descriptor POD changed")
    report = {"role": role, "machine": h[2], "file_sha256": sha, "profile_sha256": fingerprint.hex(),
              "code_sha256": digest(code).hex(), "build_id": build_id.hex(), "phnum": phnum,
              "mapped_span": mapped_span, "readonly_file_bytes": readonly_total,
              "scope": "offline expected metadata; not loaded ownership or native runtime proof"}
    return blob, report


def read_owned(path):
    with open(path, "rb") as file:
        before = os.fstat(file.fileno())
        if not stat.S_ISREG(before.st_mode) or not 64 <= before.st_size <= MAX_FILE:
            raise ValueError("Input must be a bounded regular file")
        data = file.read(MAX_FILE + 1)
        after = os.fstat(file.fileno())
    if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise ValueError("Input changed during preparation")
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native-dir", type=Path)
    p.add_argument("--api", type=Path, help="Explicit private Steam-runtime API file; the role-2 hard pin remains required")
    p.add_argument("--observer", type=Path)
    p.add_argument("--observer-sha")
    p.add_argument("--fixture", type=Path)
    p.add_argument("--compositor", action="store_true", help="Prepare the exact compositor/libc/loader set without a receiver or OpenVR API")
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    try:
        if bool(args.fixture) == bool(args.native_dir):
            raise ValueError("Choose either a native set or one explicit host fixture")
        if args.api and not args.native_dir:
            raise ValueError("An explicit API override requires the native set")
        if args.compositor and (not args.native_dir or args.api):
            raise ValueError("Compositor preparation requires the native set and no API override")
        inputs = [(6, args.fixture, None)] if args.fixture else [
            (role, args.api if role == 2 and args.api else args.native_dir / (COMPOSITOR if role == 7 else NATIVE[role])[0], None)
            for role in ((7, 3, 4) if args.compositor else (1, 2, 3, 4))]
        if args.observer:
            inputs.append((5, args.observer, args.observer_sha))
        args.output.mkdir(mode=0o700, parents=True, exist_ok=True)
        reports = []
        for role, path, reviewed in inputs:
            blob, report = profile_from_bytes(read_owned(path), role, reviewed)
            report["name"] = path.name
            for name, content in ((f"module-{role}.bin", blob), (f"module-{role}.json", (json.dumps(report, indent=2) + "\n").encode())):
                fd = os.open(args.output / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                with os.fdopen(fd, "wb") as file:
                    file.write(content)
            reports.append(report)
    except (ValueError, OSError) as error:
        p.exit(1, str(error) + "\n")
    print(json.dumps(reports))


if __name__ == "__main__":
    main()
