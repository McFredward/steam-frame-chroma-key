# Receiver startup boundary observed on the Frame, 2026-10-12

The first authorized receiver-observer attempt exposed an incorrect launcher
assumption: native Steam Link's `vrlink` belongs to **`steam.service`**, not
`steamvr.service`. The SteamVR-only drop-in loaded the library in the compositor
but could not reach the receiver. No receiver capture file or arm marker was
created, and no protected selection records were read. This is a startup-scope
failure, not a lossless scalar capture or evidence of frame association.

The separately approved [follow-up Steam-client attempt](FRAME-RECEIVER-STEAM-RUN-20261012.md)
later verified receiver loading. It refused arming on a Btrfs file-identity guard
and was fully restored; this report retains the first attempt's original evidence.

## Native evidence

The production observer was 74,896 bytes, SHA-256
`97acec88ae307ade7130c8424d3b47dbcdee44cf14a8da8c2be6235afa31cccc`.
The reviewed native-file preflight passed against the actual Frame libc and
loader, including all mandatory versioned imports. The existing 21-suite CI
[passed before deployment](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38177123604).
Those software checks did not establish which service starts the native receiver.

Read-only inspection after the explicit VR-stream confirmation found:

| Component | Observed startup role |
| --- | --- |
| `vrcompositor`, PID 733097 | `steamvr.service`; observer mapped, expected diagnostic environment present |
| `vrlink`, PID 737455 | `steam.service`; observer absent, diagnostic environment absent |
| Receiver parent, PID 737452 | native `run_vrlink.sh` |
| Launcher parent, PID 733311 | native ARM64 Steam client |

The receiver executable remained the exact pinned SHA-256
`0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b`.
Its actual path was `/opt/steamvr/tools/vrlink/bin/linuxarm64/vrlink`.
The launcher script's SHA-256 was
`cab8373a484a48f70ecec60e488862f672b1f0b87e3046ad22406cba37658686`.
It invokes `./vrlink` as its child, forwards signals and preserves its exit
status. This inspected script does not unset `LD_PRELOAD`; whether the Steam
client preserves a newly supplied preload for this child still needs a runtime
check. No native script or executable was changed.

Although the receiver's `LD_LIBRARY_PATH` named Steam's ARM64 runtime directory,
its actual mapped providers were the system files, independently hashed through
its `/proc/<pid>/root` namespace:

| Provider | SHA-256 |
| --- | --- |
| `/usr/lib/libc.so.6`, 1,722,992 bytes | `be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b` |
| `/usr/lib/ld-linux-aarch64.so.1`, 204,048 bytes | `2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e` |

Both matched the actual-file preflight. The native environment file
`/usr/share/deckard/mesavars.sh` remained unchanged, SHA-256
`4c642e067d933154c401b931665b5c64c8f4a1be1bed5975ea14424b65e18938`.
Private startup inventories, scope records and file snapshots are retained in
ignored `build/private-receiver-run-20261012/`; native source/binaries are not
redistributed by this report.

## Corrected preparation and permission boundary

Proposal v2 explicitly names `receiver_service: steam.service`. Its manual
helper must target that exact user service and prove that the capture's exclusive
lock owner is the pinned receiver with the reviewed observer/provider mappings
in the expected service cgroup. A compositor mapping, active user service or
header alone must not substitute for receiver startup evidence.

The previous concrete runtime permission targeted a SteamVR-only drop-in.
Loading through the Steam client service changes the scope and interrupts the
Frame's Steam interface, so the corrected attempt requires an additional
reviewed permission and restart readiness. Local preparation is authorized;
no new Steam-service activation is authorized merely by this report or a green
test. Remove and verify the previous session before attempting the new one.
Keep the observer library unchanged; the diagnosed problem is the launch route.

## Verified restoration

After the maintainer confirmed manual restart readiness, the original v1 helper
removed its exact owned SteamVR drop-in and restarted the native service. It
then conservatively reported a manager snapshot difference. Independent checks
proved the original service configuration and native environment file matched;
all affected SteamVR processes lacked observer mappings, and compositor PID
805882 retained the native SHA-256
`9b1bd5fe4463592cbc71b2517b9ceb0a0fcd66cd6885a19dca4be0784bf6c265`.

Only `GAMESCOPE_LIMITER_FILE` and `MANGOHUD_CONFIGFILE` changed: both named newly
allocated native session files under `/run/user/1000`. The diagnostic did not
write either manager variable. Do not force these pointers back to obsolete
session files. The corrected helper permits only the exact two normalized
runtime-child path shapes and records `exact_manager:false` when they change;
all other manager values remain strictly compared.

Exact-file inventories and the independent restoration record were archived
before removing the owned session, staging files and generated staging cache.
The final check proved both owned directories and the old drop-in absent,
native SteamVR active, and no Steam-client drop-in created. The SSH session was
closed, and the maintainer was told the Frame could enter standby. No timed
rollback or unapproved Steam-client restart occurred.

This attempt does not establish accepted presentation, source-session/video
identity, GPU coherence or native-mask passthrough. The preceding
[public resource capture](FRAME-COMPACT-JOURNAL-RUN-20261011.md) retains its
separate measured evidence.
