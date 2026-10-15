# Cold one-shot pixel snapshot session

This developer helper stages the temporary **public Vulkan implicit layer** used
by GloomhavenVR's Steam Link mask investigation. It is not a BepInEx plugin,
a production passthrough implementation, or an additional PC Steam sender hook.
It never starts/stops a service, restarts Steam/SteamVR, connects over SSH,
changes shaders, installs a preload/audit library, or starts a timer.

Before arming, the source test in GloomhavenVR must be enabled and visibly
streamed to the Frame. The layer saves at most one immutable decoded P010 image;
a reusable native recording may repeat its GPU copy until retirement. Pixel
recovery is offline. Source-frame identity, eye placement, native foveation/UV
mapping and working passthrough remain unproved.

## Files and ownership

The default home is exactly `/home/steamos`:

- Config: `.local/share/GloomhavenVR/SteamLinkMaskProbe/session.conf`
- Loader manifest: `.local/share/vulkan/implicit_layer.d/VkLayer_GHVR_pixel_snapshot.json`
- A unique private run below `.local/share/GloomhavenVR/SteamLinkMaskProbe/runs/`
  contains the copied library, immutable `session-receipt.json`, and eventual
  `capture.arm`, `launch.claim`, `status.jsonl`, `snapshot.p010`, `snapshot.json`.

Directories created by the helper are private (`0700`); files are `0600`.
Every parent component is opened without following symlinks. Existing foreign
activation files are refused, not replaced or silently backed up. Existing
ordinary parent directories are preserved; unsafe ownership/permissions are
refused rather than changed. An identical prepare may resume the exact recorded
owned plan, including an interrupted stage. Nonempty unrecorded run directories
are never adopted.

The supplied library must match an explicit SHA-256 and be a bounded, owned
ELF64 little-endian AArch64 shared library (at most 8 MiB). This checks the file
and program-header envelope; it does **not** prove that the Vulkan loader or
headset runtime will accept it. `--host-fixture` permits a sandbox home/proc root
and x86-64 fixture library only for local tests; do not use it for deployment.

## Explicit manual sequence

Use a fresh run path and the exact release/build library hash. The following
commands only change local files. Copying the helper/package to a device and
restarting its runtime require the maintainer's separate authorization.

```sh
python3 tools/pixel_snapshot_session.py prepare \
  --run-dir /home/steamos/.local/share/GloomhavenVR/SteamLinkMaskProbe/runs/CAPTURE_NAME \
  --library /absolute/path/libVkLayer_frame_pixel_snapshot.so \
  --library-sha256 EXACT_LOWERCASE_SHA256
```

`stage` is an alias for `prepare`. Staging publishes the manifest but does not
load a library into an existing process. It is intentionally unarmed. The next
manually started `vrcompositor` may consume `launch.claim`; other process names
remain inert. A crash, reboot or later SteamVR restart cannot automatically claim
that run again. Do not delete `launch.claim` to reuse a session.

The **later orchestration must use the existing `NativeEvidenceSource` native UI
health checks** before requesting a stream or arming, and after an agreed manual
restart/rollback. Do not substitute a running PID, open SSH connection, this
helper's success, or a software fixture for visible startup health. This helper
reports `native_ui_health_verified:false` and `startup_screen_pixels_verified:false`.
It has no authority to restart the headset or infer that its UI is usable.

Once the ordinary Frame interface is verified, the maintainer confirms the PC VR
stream and source pattern are visible. Arm explicitly:

```sh
python3 tools/pixel_snapshot_session.py arm --run-dir /absolute/owned/run
python3 tools/pixel_snapshot_session.py status --run-dir /absolute/owned/run
```

`status` reads bounded metadata/status only, never the raw image. It reports
same-user loaded-library mappings, including inode aliases and deleted mappings;
uncertain or excessive process scans block deletion. No output is a claim that
an image, native association or visible headset result has been validated.

Some SteamOS same-user processes protect `/proc/PID/maps` even though their UID
is visible. The default scan remains unprivileged and refuses an incomplete
result. With separately authorized read-only administration, explicitly add
`--read-protected-maps` to permit a fallback **only** after an ordinary maps read
raises `PermissionError` for a pinned same-UID, numeric PID. It runs exactly
`sudo -n /usr/bin/head -c 1048577 /proc/PID/maps`, without a shell or password
prompt. Each child is reaped, limited to three seconds, at most 1 MiB plus one
sentinel byte of stdout, and 4 KiB of stderr. Nonzero exit, excess output,
timeout, changed process identity, or missing privilege leaves the scan
uncertain. Readable maps and other users' processes never use this fallback.
The scan result records whether the mode is enabled and which PIDs needed it.
The helper never obtains credentials, writes as root, or changes services.
Do not run it with `sudo` as a whole; authenticate any separately approved
read-only administration outside this helper. Fake proc roots can only use an
explicitly injected test reader; they cannot invoke the real privileged reader.

## Disarm and exact rollback

```sh
python3 tools/pixel_snapshot_session.py disarm --run-dir /absolute/owned/run
python3 tools/pixel_snapshot_session.py rollback --run-dir /absolute/owned/run
```

Disarm removes only the exact owned arm token and permanently marks the run
spent. **It cannot revoke an arm already observed by a live native process.**
The admitted copy remains one-shot; there is no automatic restart at completion.

Rollback removes the exact recorded manifest/config/arm first, preventing future
activation. It then checks process maps and removes only the exact owned library
if it is not mapped. If an owned or foreign diagnostic mapping remains, or a
scan is uncertain, it retains the library and exits with status 2. An agreed
manual SteamVR restart and UI health verification are then required before
retrying rollback. Removing a manifest does not unload an existing mapping.
A map scan is a point-in-time observation; staging is for a controlled cold
runtime transition, not concurrent deployment while new processes are loading.

Hash changes, permissions changes, hardlinks, symlinks and foreign files are
preserved and reported. Rollback never follows paths supplied by a modified
receipt outside its fixed owned locations. A successful rollback is idempotent;
`rollback-complete.json` records file cleanup only, not native UI health.
Raw captures, source evidence, `status.jsonl`, `launch.claim` and the receipt are
always retained for analysis. Start a new nonce/run for any later capture.

Exit 0 means the requested bounded filesystem operation completed. Exit 2 means
refusal, unresolved ownership/mapping, or another reported error. No command
turns on passthrough. No command has an automatic expiry or replacement timer.
