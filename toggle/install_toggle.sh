#!/bin/bash
# Build + register the chroma-key dashboard toggle (user scope, removable).
#   install_toggle.sh            build, install, register manifest, enable autolaunch, start it
#   install_toggle.sh --remove   unregister, stop, delete installed files
set -euo pipefail
cd "$(dirname "$0")"
DEST="$HOME/.local/share/mrchroma/toggle"
MANIFEST="$DEST/mrtoggle.vrmanifest"
SDK=/opt/steamvr/tools/hellovr_vulkan_linux/src/openvr/headers
LIB=/opt/steamvr/bin/linuxarm64
export LD_LIBRARY_PATH=$LIB

if [[ "${1:-}" == "--remove" ]]; then
  [[ -x "$DEST/mrtoggle_reg" ]] && "$DEST/mrtoggle_reg" remove "$MANIFEST" mrchroma.toggle || true
  pkill -f "$DEST/mrtoggle_overlay" || true
  rm -rf "$DEST"
  echo "toggle removed"
  exit 0
fi

mkdir -p "$DEST"
g++ -O2 -std=c++17 -o "$DEST/mrtoggle_overlay" mrtoggle_overlay.cpp -I$SDK $LIB/libopenvr_api.so -Wl,-rpath,$LIB -pthread $(pkg-config --cflags --libs freetype2)
g++ -O2 -std=c++17 -o "$DEST/mrtoggle_reg" mrtoggle_reg.cpp -I$SDK $LIB/libopenvr_api.so -Wl,-rpath,$LIB
cp icon.png "$DEST/"
rm -f "$DEST"/on.png "$DEST"/off.png "$DEST"/busy.png
chmod +x ../mrtoggle.sh
cat > "$MANIFEST" <<EOF
{
  "source": "user",
  "applications": [{
    "app_key": "mrchroma.toggle",
    "launch_type": "binary",
    "binary_path_linux": "$DEST/mrtoggle_overlay",
    "binary_path_linux_arm": "$DEST/mrtoggle_overlay",
    "arguments": "$HOME/mr-chroma/mrtoggle.sh",
    "is_dashboard_overlay": true,
    "image_path": "$DEST/icon.png",
    "strings": { "en_us": { "name": "Chroma Key Toggle", "description": "Turn mr-chroma passthrough keying on/off" } }
  }]
}
EOF
"$DEST/mrtoggle_reg" add "$MANIFEST" mrchroma.toggle
pkill -f "$DEST/mrtoggle_overlay" && sleep 1 || true
(nohup "$DEST/mrtoggle_overlay" "$HOME/mr-chroma/mrtoggle.sh" >/tmp/mrtoggle_overlay.log 2>&1 &)
echo "installed; overlay running (log /tmp/mrtoggle_overlay.log)"
