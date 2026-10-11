#!/bin/bash
# mrtoggle — switch chroma keying on/off without regenerating the shader.
#   mrtoggle.sh on | off | toggle | status
# "on" re-enables the last shader built by mrkey.sh (active_se2.spv), so the
# key colour / threshold / softness / spill from the last panel session apply.
set -euo pipefail
SHADER=distort_ps_achromatic_se2.spv
STATE="$HOME/.local/share/mrchroma"
CONF="$HOME/.config/mrchroma/mrchroma.conf"
ACTIVE="$STATE/active_se2.spv"
LINE="override = $SHADER $ACTIVE"

is_on() { grep -q "^override = $SHADER " "$CONF" 2>/dev/null; }

# PC home background (applied on the PC by pc/mrchroma_pcsync.pyw):
#   mrtoggle.sh bg on | off | toggle | status
#   on  = PC SteamVR background painted in the key colour (keys to passthrough)
#   off = PC SteamVR default background (mountains, floor)
PCBG="$HOME/.config/mrchroma/pcbg"
if [[ "${1:-}" == bg ]]; then
  cur=$(cat "$PCBG" 2>/dev/null || echo off)
  case "${2:-status}" in
    status) echo "$cur"; exit 0 ;;
    on|off) new=$2 ;;
    toggle) if [[ "$cur" == on ]]; then new=off; else new=on; fi ;;
    *) sed -n '15,18p' "$0"; exit 2 ;;
  esac
  echo "$new" > "$PCBG.tmp" && mv "$PCBG.tmp" "$PCBG"
  echo "bg $new"
  exit 0
fi

cmd="${1:-status}"
if [[ "$cmd" == toggle ]]; then
  if is_on; then cmd=off; else cmd=on; fi
fi
case "$cmd" in
  status)
    if is_on; then echo on; else echo off; fi
    exit 0 ;;
  on)
    if [[ ! -s "$ACTIVE" ]]; then
      echo "mrtoggle: no $ACTIVE yet - run mrkey.sh chroma ... once" >&2
      exit 1
    fi
    is_on || echo "$LINE" >> "$CONF" ;;
  off)
    sed -i "/^override = $SHADER /d" "$CONF" ;;
  *) sed -n '2,5p' "$0"; exit 2 ;;
esac
touch "$CONF"
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 timeout 10 /opt/steamvr/bin/linuxarm64/vrcmd --reload-shaders >/dev/null 2>&1 || true
echo "$cmd"
