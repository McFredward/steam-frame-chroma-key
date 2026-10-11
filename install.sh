#!/bin/bash
# Install the mrchroma layer for the current user only.  Nothing in /usr or
# /opt is touched.  Takes effect the next time vrlink starts (new stream).
set -euo pipefail
cd "$(dirname "$0")"
DEST="$HOME/.local/share/mrchroma"
MANIFEST_DIR="$HOME/.local/share/vulkan/implicit_layer.d"
CONF_DIR="$HOME/.config/mrchroma"
BACKUP_DIR="$DEST/backup"

mkdir -p "$DEST" "$MANIFEST_DIR" "$CONF_DIR" "$BACKUP_DIR"
install -m 755 build/libVkLayer_mrchroma.so build/mrctl "$DEST/"

# One-time backup of SteamVR settings before we ever change them.
VRSET="$HOME/.config/openvr/config/steamvr.vrsettings"
if [[ -f "$VRSET" && ! -f "$BACKUP_DIR/steamvr.vrsettings.orig" ]]; then
  cp -p "$VRSET" "$BACKUP_DIR/steamvr.vrsettings.orig"
fi

cat > "$MANIFEST_DIR/mrchroma.json" <<EOF
{
  "file_format_version": "1.2.0",
  "layer": {
    "name": "VK_LAYER_MRCHROMA",
    "type": "GLOBAL",
    "library_path": "$DEST/libVkLayer_mrchroma.so",
    "api_version": "1.4.309",
    "implementation_version": "1",
    "description": "Chroma-key alpha for vrlink (loads only into vrlink)",
    "functions": { "vkNegotiateLoaderLayerInterfaceVersion": "mrchroma_NegotiateLoaderLayerInterfaceVersion" },
    "disable_environment": { "DISABLE_MRCHROMA": "1" }
  }
}
EOF

[[ -f "$CONF_DIR/mrchroma.conf" ]] || install -m 644 mrchroma.conf.example "$CONF_DIR/mrchroma.conf"
echo "installed. layer manifest: $MANIFEST_DIR/mrchroma.json"
echo "restart the stream (or vrlink) for the layer to load."
