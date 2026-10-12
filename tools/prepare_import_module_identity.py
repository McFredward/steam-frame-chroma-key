#!/usr/bin/env python3
"""Prepare hash/layout metadata for the one reviewed client module, offline.

No native bytes, executable code or instruction listings are emitted. This
preparation does not establish that any process has loaded the file.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct

MAX_FILE = 8 * 1024 * 1024
MAX_PHDR = 8
FILE_SHA256 = "19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0"
BUILD_ID = "4e83060016f97f409f01f5f3ab917254d26f7392"
CALLER_RVA = 0x244DBC
PROFILE_BYTES = 1064


def digest(data):
    return hashlib.sha256(data).digest()


def profile_from_bytes(data):
    if not 64 <= len(data) <= MAX_FILE:
        raise ValueError("Client file exceeds the bounded ELF input")
    if hashlib.sha256(data).hexdigest() != FILE_SHA256:
        raise ValueError("Unsupported client file identity")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    if header[0][:7] != b"\x7fELF\x02\x01\x01" or header[1:4] != (3, 183, 1) or header[8:10] != (64, 56):
        raise ValueError("Unsupported ELF64 little-endian AArch64 layout")
    phoff, phnum = header[5], header[10]
    if not 1 <= phnum <= MAX_PHDR or phoff > len(data) or 56 * phnum > len(data) - phoff:
        raise ValueError("Unsupported bounded program header table")
    phdrs = [struct.unpack_from("<IIQQQQQQ", data, phoff + 56 * i) for i in range(phnum)]
    code = bytearray()
    segments = []
    rx_hashes = []
    build_note = None
    loads = []
    for ph in phdrs:
        kind, flags, offset, virtual, physical, file_size, memory_size, alignment = ph
        if offset > len(data) or file_size > len(data) - offset or memory_size < file_size:
            raise ValueError("Program header exceeds owned file")
        if kind == 1:
            if not flags & 4 or flags & 2 and flags & 1 or virtual + memory_size > MAX_FILE:
                raise ValueError("Unsupported readable load layout")
            if any(virtual < end and start < virtual + memory_size for start, end in loads):
                raise ValueError("Overlapping load segments")
            loads.append((virtual, virtual + memory_size))
        owned = data[offset:offset + file_size]
        if kind == 1 and flags & 1:
            code.extend(owned)
            rx_hashes.append(digest(owned))
        else:
            rx_hashes.append(bytes(32))
        if kind == 4:
            if build_note is not None or file_size != 36:
                raise ValueError("Unsupported build-ID note shape")
            names, size, note_type = struct.unpack_from("<III", owned)
            if (names, size, note_type) != (4, 20, 3) or owned[12:16] != b"GNU\0" or owned[16:].hex() != BUILD_ID:
                raise ValueError("Unsupported GNU build identity")
            build_note = (virtual, file_size, owned)
        segments.append({"type": kind, "flags": flags, "offset": offset, "vaddr": virtual, "paddr": physical,
                         "file_size": file_size, "memory_size": memory_size, "alignment": alignment})
    if not build_note or not code or len(code) > MAX_FILE:
        raise ValueError("Missing bounded code/build identity")
    if not any(ph[0] == 1 and ph[1] & 1 and ph[3] <= CALLER_RVA < ph[3] + ph[5] for ph in phdrs):
        raise ValueError("Reviewed caller does not belong to executable file bytes")
    note_virtual, note_size, note_bytes = build_note
    blob = struct.pack("<9Q", 1, len(data), phoff, phnum, note_virtual, note_size, 20, CALLER_RVA, 183)
    hashes = [digest(data), digest(data[:64]), digest(data[phoff:phoff + 56 * phnum]), digest(note_bytes), digest(code)]
    blob += b"".join(hashes) + bytes.fromhex(BUILD_ID) + bytes(12)
    for ph in phdrs:
        blob += struct.pack("<8Q", *ph)
    blob += bytes((MAX_PHDR - phnum) * 64)
    blob += b"".join(rx_hashes) + bytes((MAX_PHDR - phnum) * 32)
    fingerprint = digest(blob)
    blob += fingerprint
    if len(blob) != PROFILE_BYTES:
        raise ValueError("Prepared POD layout changed")
    report = {"schema": 1, "scope": "offline expected hash/layout metadata only; no runtime mapping proof",
              "file_size": len(data), "file_sha256": hashes[0].hex(), "header_sha256": hashes[1].hex(),
              "program_headers_sha256": hashes[2].hex(), "build_note_sha256": hashes[3].hex(),
              "code_sha256": hashes[4].hex(), "build_id": BUILD_ID, "caller_rva": CALLER_RVA,
              "profile_sha256": fingerprint.hex(), "profile_bytes": len(blob), "phdrs": segments,
              "rx_sha256": [value.hex() for value in rx_hashes]}
    return blob, report


def prepare(source, output):
    with open(source, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 64 <= before.st_size <= MAX_FILE:
            raise ValueError("Input must be a bounded regular file")
        data = stream.read(MAX_FILE + 1)
        after = os.fstat(stream.fileno())
    if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise ValueError("Input changed during preparation")
    blob, report = profile_from_bytes(data)
    destination = Path(output)
    destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    for name, content in (("import-client-profile.bin", blob),
                          ("import-client-profile.json", (json.dumps(report, indent=2) + "\n").encode())):
        path = destination / name
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = prepare(args.client, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")
    print(json.dumps({"prepared": True, "profile_sha256": report["profile_sha256"], "code_sha256": report["code_sha256"]}))


if __name__ == "__main__":
    main()
