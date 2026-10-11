#!/bin/bash
# Full rollback: disable compositor alpha blending, remove the layer manifest
# and binaries.  The config dir and settings backup are kept unless --purge.
set -uo pipefail
DEST="$HOME/.local/share/mrchroma"
if [[ -x "$DEST/mrctl" ]] && pgrep -x vrserver >/dev/null; then
  "$DEST/mrctl" remove camera forceSceneAlphaBlending || true
fi
"$(dirname "$0")/toggle/install_toggle.sh" --remove || true
rm -f "$HOME/.local/share/vulkan/implicit_layer.d/mrchroma.json"
rm -f "$DEST/libVkLayer_mrchroma.so" "$DEST/mrctl"
if [[ "${1:-}" == "--purge" ]]; then
  rm -rf "$DEST" "$HOME/.config/mrchroma"
fi
echo "uninstalled. A running vrlink keeps the layer until the stream restarts."
echo "original settings backup (if any): $DEST/backup/steamvr.vrsettings.orig"
