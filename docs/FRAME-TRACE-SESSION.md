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

This page describes the approved procedure. The actual installation, capture
result and verified removal will be appended after the run.
