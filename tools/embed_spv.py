#!/usr/bin/env python3
"""Embed the two legacy layer shaders; no Valve shader is read or redistributed."""
from pathlib import Path
import struct
import sys


def main():
    destination, vertex, fragment = map(Path, sys.argv[1:])
    declarations = []
    for name, source in (("k_vs_spv", vertex), ("k_fs_spv", fragment)):
        data = source.read_bytes()
        if len(data) < 20 or len(data) % 4:
            raise ValueError("invalid SPIR-V length")
        words = struct.unpack(f"<{len(data) // 4}I", data)
        if words[0] != 0x07230203:
            raise ValueError("invalid SPIR-V magic")
        declarations.append("static const uint32_t {}[] = {{{}}};".format(
            name, ",".join(f"0x{word:08x}" for word in words)))
    destination.write_text("#pragma once\n#include <cstdint>\n" + "\n".join(declarations) + "\n")


if __name__ == "__main__":
    main()
