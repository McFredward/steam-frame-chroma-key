#!/bin/bash
# mrkey — switch the compositor scene shader between stock / rect test / chroma key.
#   mrkey.sh off
#   mrkey.sh rect   [x0 y0 x1 y1 [invert]]          (normalized eye coords, default 0.3 0.3 0.7 0.7)
#   mrkey.sh chroma [R G B threshold softness spill] (default 0 1 0 0.18 0.10 0.5)
# Requires: install.sh done, ~/.config/mrchroma/enable-compositor present and
# vrcompositor started with the layer.  Applies live via shader reload.
set -euo pipefail
cd "$(dirname "$0")"
SHADER=distort_ps_achromatic_se2.spv
ORIG=/opt/steamvr/resources/shaders/vulkan/$SHADER
STATE="$HOME/.local/share/mrchroma"
CONF="$HOME/.config/mrchroma/mrchroma.conf"
OUT="$STATE/active_se2.spv"
mkdir -p "$STATE"

mode="${1:-}"; shift || true
case "$mode" in
  off)
    sed -i "/^override = $SHADER /d" "$CONF" ;;
  rect)
    [[ $# -ge 4 ]] || set -- 0.3 0.3 0.7 0.7
    spirv-dis "$ORIG" -o "$STATE/orig_se2.spvasm"
    python3 tools/gen_key_spv.py "$STATE/orig_se2.spvasm" "$OUT.new" rect "$@"
    mv "$OUT.new" "$OUT" ;;
  chroma)
    [[ $# -ge 6 ]] || set -- 0 1 0 0.18 0.10 0.5
    spirv-dis "$ORIG" -o "$STATE/orig_se2.spvasm"
    python3 tools/gen_key_spv.py "$STATE/orig_se2.spvasm" "$OUT.new" chroma "$@"
    mv "$OUT.new" "$OUT" ;;
  *) sed -n '2,6p' "$0"; exit 2 ;;
esac
if [[ "$mode" != off ]] && ! grep -q "^override = $SHADER " "$CONF"; then
  echo "override = $SHADER $OUT" >> "$CONF"
fi
touch "$CONF"
echo "$mode $*" > "$STATE/mrkey.state"
# Ask vrcompositor to rebuild its shaders (VREvent_ReloadShaders).
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 timeout 10 /opt/steamvr/bin/linuxarm64/vrcmd --reload-shaders || true
echo "mrkey: $mode $*"
