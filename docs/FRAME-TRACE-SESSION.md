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

After the manifest correction and focused deadline review, the fourth attempt
used a 68,056-byte library, SHA-256
`abefb338e1ce8e89ee16e57a6c0b3637399fe327d1c5c41252d6c84ae670f609`.
The next reviewed library is also 68,056 bytes, SHA-256
`1c1fe5a040461d860fc2aeb6845d54b038f091eb4fdd2627de6e0b4081e55d1a`.
Its filtered writer closes by 120 seconds; unfiltered capture retains its
30-second limit. The event/byte bounds remain unchanged. The latest repair
separates intentional closure of the event budget from genuine observation
loss; it does not alter native rendering or forgive losses in earlier evidence.
The earlier hashes remain the identities used in the recorded attempts.

After the fourth attempt's automatic restart interrupted the maintainer's VR
stream, the maintainer explicitly requested removal of time-triggered recovery
for the next run. Use `start --manual-rollback` for that run. This disables the
independent rollback timer, not the bounded recording. Finishing a recording
does not restart SteamVR. Manual recovery and reboot recovery remain available.
The default helper mode still provides the original watchdog for other sessions.

The following new files are the entire deployment:

- `~/.local/share/frame-mask-trace/`: the library, session helper and bounded
  capture/ledger/recovery markers and the read-only startup/probe helpers. This
  directory must not exist before staging.
- `~/.local/share/vulkan/implicit_layer.d/frame_mask_trace.json`: opt-in manifest.
- `/run/user/<uid>/systemd/user/steamvr.service.d/90-frame-mask-trace.conf`:
  temporary environment variables for this service only.
- In default timed mode only, an independent transient user service,
  `frame-mask-trace-rollback.service`.
  It initiates rollback by 180 seconds, removes the activation files and restarts
  SteamVR without the layer. It is started and acknowledged before activation.

Each service operation has a bounded timeout. The rollback/restart can take
additional time after the capture window. An ordinary runtime crash remains a
possibility for any native diagnostic layer. Timed mode has independent recovery;
manual mode requires the operator to recover it explicitly. Both use boot-scoped
activation, without claiming that a runtime crash is impossible. Manual mode
refuses activation if an earlier timed rollback service is still active.

The `/run` drop-in disappears on reboot. The manifest additionally requires
`FRAME_MASK_TRACE=1`, so any leftover manifest alone cannot activate the layer on
the next boot. No global manager environment or persistent restart service is
installed. A reboot therefore returns to the ordinary SteamVR launch path.

The reviewed manual-mode activation, after the maintainer is ready for one
initial SteamVR restart, is:

```bash
python3 ~/.local/share/frame-mask-trace/frame_trace_session.py start \
  --sha256 1c1fe5a040461d860fc2aeb6845d54b038f091eb4fdd2627de6e0b4081e55d1a \
  --manual-rollback
```

Do not add loader-debug logging now that the manifest cause is established.
Startup failures still remove the activation and attempt native recovery.
After capture, announce the interruption before manual restoration. The manual
rollback, from SSH or a Frame terminal, is:

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
private session directory after confirming no rollback service remains active.
Do not delete unrelated Vulkan layers or service drop-ins. Record any exception
and verify normal SteamVR/SSH operation.

## Execution record

Before activation, record the service state, current compositor PID, absence of
both activation paths and the library identity. Restart only after the maintainer
is ready to reconnect the PC stream. The helper writes every activation/removal
and restart to `session.jsonl`. When enabled, its watchdog is separate from the
SteamVR cgroup. The ledger records `rollback_mode: manual` with no rollback
deadline for the maintainer-requested variant.
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

No capture was accepted. At that checkpoint the native-compositor non-loading
cause was unresolved. The subsequent targeted startup capture below identifies
an invalid manifest field. Stage/descriptor/frame proof and genuine mask
passthrough remain blocked; there is no installed diagnostic left on the Frame.

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

## Targeted loader capture and correction

The bounded helper now has an opt-in `start --loader-debug` variant. This adds
only `VK_LOADER_DEBUG=error,warn,layer` to the same temporary user-service
drop-in. It does not force-enable a layer, disable another layer or replace the
loader. It was used in the second authorized attempt below. Twelve focused recovery
checks pass, including default logging preservation and exact removal of the
debug variant. The native trace library is unchanged, so its previous compiled
and GPU evidence remains applicable without rebuilding it.

The capture collected the real startup journal, bounded to at most 1 MiB
and 2,048 copied lines, together with actual compositor PID/executable, loader
mapping and trace header. This bounds the evidence copy; it is not a promise
that the native SteamVR journal producer itself obeys our trace file cap.
No full function-call loader tracing is enabled. Abort and restore on a new
warning, missing capture or missing compositor mapping instead of retrying
indefinitely. The exact warning separately observed in the restored native
baseline is recorded below; it is not automatically attributed to the layer.
`systemctl is-active` measures the launcher (`vrcmd`) only; it must never be
accepted as compositor readiness or a successful pipeline capture.

The [startup review](FRAME-TRACE-STARTUP-REVIEW.md) records why a blind repeat,
privilege change or forced layer enable is unjustified. All new writes/restarts
still use the approved temporary scope and require the maintainer to be ready
for the stream interruption.

The second attempt started at 13:12:59 CEST. Its native loader found the manifest
but rejected the numeric `implementation_version` field. The manifest must use
string `"1"`. The same deployed manifest hash was present in the first attempt.
No diagnostic mapping or trace header appeared, so the run was aborted without
asking the maintainer to start a PC stream. The journal copy hit its line cap
and is marked truncated; only the specific manifest rejection is established,
not a complete startup or active pipeline.

Manual recovery removed both activation files at 13:13:26 and completed the
native restart at 13:13:31. At 13:14:17 the native service was active, compositor
PID 295808 had no diagnostic mapping and the independent watchdog was inactive.
The temporary headset directory was removed by 13:14:37. The copied ledger and
small bounded startup record remain private under
`build/private-frame-loader-run-20261011`; no Valve binary or shader is published.
The SSH credential environment was cleared and its session closed.

The helper and CMake manifest now serialize the version as a string. Strengthened
checks must use the actual helper manifest, validate complete properties rather
than only a returned layer name, and verify real implicit instance loading.
The previous name-only discovery result was insufficient: this loader can
return partially parsed properties even after logging a skipped manifest.
See the [startup review](FRAME-TRACE-STARTUP-REVIEW.md) for the source evidence.

## Corrected loading, stream capture and manual-mode preparation

The third attempt began at 13:19:45 CEST with the corrected manifest. The
diagnostic actually mapped into compositor PID 309241 and wrote a header,
shader and pipeline records. The operator stopped the run after a Gamescope
warning rather than accepting a partial trace. There was no healthy footer.
Native recovery completed at 13:20:20; the temporary directory was removed by
13:21:25. This was successful loading, not an accepted pipeline capture.

The same non-hooked-swapchain warning appeared in a read-only baseline after
complete restoration, without the diagnostic mapping or activation. The
maintainer confirmed the Frame remained usable and the ordinary VR stream ran.
That exact warning alone therefore did not establish diagnostic causation.
It does not justify ignoring new errors or accepting an incomplete capture.

The fourth attempt began at 13:26:08 CEST with the deadline-reviewed library.
It mapped into compositor PID 325073 and produced 2,048 metadata records by
13:27:35: 211 shader, 109 pipeline, 864 draw and 864 accepted-submit events.
The observed draw tuples paired vertex fingerprint `9cc04d186b9c8fe5` with
fragment fingerprint `f333a137b4e79ced`. The bounded footer reported successful
I/O, no state loss and no byte-cap hit, but **one dropped event**. The capture
remains incomplete under the healthy-footer rule; those tuples are observations,
not an accepted active-pipeline proof. The later boundary repair cannot establish
the historical cause of that loss or retroactively accept this evidence.

At 13:29:08 the timed watchdog removed the activation, requested native restart
at 13:29:09 and completed it at 13:29:15. This interrupted the maintainer's newly
connected VR stream. Recovery was verified with native compositor PID 332705,
no diagnostic mapping, no activation files and an inactive watchdog. The private
headset directory was removed by 13:31:47. Capture and recovery evidence remain
ignored in `build/private-frame-stream-run-20261011/`.

The maintainer then authorized the manual-only recovery variant described above.
Eighteen focused helper/schema/recovery checks pass, including no timer creation,
idempotent exact-file rollback, startup-failure cleanup and refusal of a stale
watchdog. The event-boundary core and real-loader/GPU suites pass, including the
exact deployed implicit manifest, healthy footer and unchanged fixture pixels.
These are focused checks, not a repeated complete foundation gate or a headset
outcome.

The new library and helper have been staged in the private headset directory.
The dependency/discovery-only native probe reports complete matching manifest
properties and successful `RTLD_NOW` loading. It creates no Vulkan instance or
device and cannot prove the compositor instance chain. Both activation files
remain absent, SteamVR remains native and no timer is active. The maintainer
asked to wait before activation; no further restart or capture has been started.
