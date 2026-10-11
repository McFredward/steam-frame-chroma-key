# Authorized temporary Frame trace session

The maintainer authorized a temporary passive capture on 2026-10-11, with a
written change record and an explicit rollback procedure. This permission is
limited to the diagnostic; it does not authorize mask activation, runtime binary
patches, system configuration changes or permanent installation.

## Scope and recovery

All commands run as `steamos`, without `sudo`. The reviewed library is AArch64,
80,888 bytes, SHA-256
`11ac6b92bd921c35bee845925398a6fe8dad58e6985ad3b056d5516754eef040`.
The original SteamVR binaries, shader files, camera configuration, boot settings
and persistent service configuration remain untouched.

The following new files are the entire deployment:

- `~/.local/share/frame-mask-trace/`: the library, session helper and bounded
  capture/ledger/recovery markers. This directory must not exist before staging.
- `~/.local/share/vulkan/implicit_layer.d/frame_mask_trace.json`: opt-in manifest.
- `/run/user/<uid>/systemd/user/steamvr.service.d/90-frame-mask-trace.conf`:
  temporary environment variables for this service only.
- An independent transient user service, `frame-mask-trace-rollback.service`.
  It initiates rollback by 180 seconds, removes the activation files and restarts
  SteamVR without the layer. It is started and acknowledged before activation.

Each service operation has a bounded timeout. The rollback/restart can take
additional time after the capture window. An ordinary runtime crash remains a
possibility for any native diagnostic layer; the independent recovery service
and boot-scoped activation limit that risk without claiming it is impossible.

The `/run` drop-in disappears on reboot. The manifest additionally requires
`FRAME_MASK_TRACE=1`, so any leftover manifest alone cannot activate the layer on
the next boot. No global manager environment or persistent restart service is
installed. A reboot therefore returns to the ordinary SteamVR launch path.

The manual rollback, from SSH or a Frame terminal, is:

```bash
python3 ~/.local/share/frame-mask-trace/frame_trace_session.py rollback
```

If the helper cannot run, inspect/remove only these exact diagnostic activation
files, then reload and restart the unmodified service:

```bash
rm -f ~/.local/share/vulkan/implicit_layer.d/frame_mask_trace.json
rm -f /run/user/$(id -u)/systemd/user/steamvr.service.d/90-frame-mask-trace.conf
systemctl --user daemon-reload
systemctl --user restart steamvr.service
```

The helper normally refuses altered or preexisting files rather than deleting
unrelated content. These emergency commands are for this identified deployment
only. After recovery, copy the small ledger/capture to the development machine,
confirm the compositor no longer maps the diagnostic library and remove the
private session directory after confirming/stopping the rollback service. Do not
delete unrelated Vulkan layers or service
drop-ins. Record any exception and verify normal SteamVR/SSH operation.

## Execution record

Before activation, record the service state, current compositor PID, absence of
both activation paths and the library identity. Restart only after the maintainer
is ready to reconnect the PC stream. The helper writes every activation/removal
and restart to `session.jsonl`; its watchdog is separate from the SteamVR cgroup.
Do not interpret capture data until a healthy trace footer, unchanged output
bounds and matching recorded/submitted generations have been verified. A timeout
or incomplete capture never authorizes enabling a mask.

## Actual run, 2026-10-11

The authorized session was staged in a new mode-0700 user directory. The reviewed
AArch64 library hash matched. Both activation paths were absent and the native
`steamvr.service` was active before staging. The independent rollback service
acknowledged readiness before any activation file was written.

At 12:36:28 CEST the helper requested the capture restart; SteamVR reported active
at 12:36:33. The new compositor inherited the three intended `FRAME_MASK_TRACE*`
variables, but no capture file was created and its process mappings did not
contain the diagnostic library. The journal included Gamescope's non-hooked
swapchain warning. This warning describes missing Gamescope hook bookkeeping;
it does not establish that the apparently unloaded diagnostic caused it. The
run was aborted rather than accepting missing data or forcing a shader.

Manual recovery removed the manifest and runtime drop-in at 12:38:07, then
restarted the native service. Recovery completed at 12:38:14. The new compositor
was active, had no diagnostic mapping and had no diagnostic service drop-in.
The watchdog had exited and its transient unit was collected. The small session
ledger was copied to the ignored developer evidence directory and the entire
temporary headset session directory was removed by 12:39:15.

A separate dependency/discovery-only probe completed at 12:42 without restarting
SteamVR. It loaded the reviewed library with `RTLD_NOW` to check dependencies and
enumerated the isolated temporary manifest with the native Vulkan loader. Both
checks passed. It created **no Vulkan instance or device**, touched no existing
process and removed its temporary manifests automatically. Its private copied
library/helper directory was removed at 12:42:57; SteamVR remained active.

Read-only checks also found `AT_SECURE=0`, user UID/GID, no effective/ambient
capabilities, ordinary executable permissions and a shared mount namespace for
the native compositor. No relevant loader settings file was present at the
standard checked locations. These eliminate several obvious installation and
dependency failures, but do not prove the compositor's runtime layer-selection
policy. `/proc/environ` represents the initial environment; absence there does
not exclude later application-side changes.

No capture was accepted. The exact remaining native-compositor non-loading cause
is unresolved. Stage/descriptor/frame proof and genuine mask passthrough remain
blocked. Further live retries must first address this startup behavior; there is
no installed diagnostic left on the Frame.

Local validation was expanded to exercise implicit `XDG_DATA_HOME` discovery
through the real loader, in addition to explicit loading and unchanged GPU
pixels. The recovery helper has ten focused checks, including concurrent/delayed
recovery, restart/reload failures, preexisting files and a full ledger filesystem.
These bounded changes reuse the unchanged foundation's complete green CI result;
they do not imply a successful headset capture.

Checkpoint `b0007c5` subsequently passed the complete
[eleven-suite CI gate](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38132993045),
including implicit-loader/GPU validation and the ten-case recovery suite,
Windows cross-build and protocol sanitizers. Completed local build/test outputs
were removed; only small private evidence and the still-reviewed staged library
were retained. The SSH credential environment was cleared when the session ended.
