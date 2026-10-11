#!/bin/bash
# Build on the Steam Frame (native aarch64).  Output: ./build/
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
OVR_INC=/opt/steamvr/tools/hellovr_vulkan_linux/src/openvr/headers
OVR_LIB=/opt/steamvr/bin/linuxarm64

glslc -O --target-env=vulkan1.1 layer/key.vert -o build/key.vert.spv
glslc -O --target-env=vulkan1.1 layer/key.frag -o build/key.frag.spv
python3 - <<'EOF'
import struct
out = []
for name, path in (("k_vs_spv", "build/key.vert.spv"), ("k_fs_spv", "build/key.frag.spv")):
    d = open(path, "rb").read()
    words = struct.unpack("<%dI" % (len(d) // 4), d)
    out.append("static const uint32_t %s[] = {%s};" % (name, ",".join("0x%08x" % w for w in words)))
open("build/mrchroma_shaders.h", "w").write("#pragma once\n#include <cstdint>\n" + "\n".join(out) + "\n")
EOF

g++ -std=c++17 -O2 -g -fPIC -shared -fvisibility=hidden -Wall -Wno-missing-field-initializers \
    -Ibuild layer/mrchroma_layer.cpp -o build/libVkLayer_mrchroma.so -lpthread
g++ -std=c++17 -O2 -Wall -I"$OVR_INC" tools/mrctl.cpp -o build/mrctl \
    -L"$OVR_LIB" -lopenvr_api -Wl,-rpath,"$OVR_LIB"
echo "build ok"
