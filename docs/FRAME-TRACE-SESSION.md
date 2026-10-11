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

Before the fifth attempt, the new library and helper were staged in the private
headset directory.
The dependency/discovery-only native probe reports complete matching manifest
properties and successful `RTLD_NOW` loading. It creates no Vulkan instance or
device and cannot prove the compositor instance chain. Both activation files
were absent, SteamVR remained native and no timer was active. The maintainer
asked to wait; activation began only after the subsequent explicit start signal.

## Fifth attempt: bounded positive pipeline evidence

At 13:42:46 CEST the helper recorded the approved manual-mode preflight with
`rollback_mode: manual`, no rollback deadline and loader-debug disabled. It
requested the initial restart at 13:42:47; the launcher became active at
13:42:53. Compositor PID 359674 actually mapped the reviewed library and wrote
the expected header. The operator signalled readiness for the PC VR stream.
At 13:45:00 a read-only process snapshot found the native `vrlink` executable
(PID 364298, comm `vrlinkrunthread`) beside the same compositor. Process argv
was not collected because it can contain streaming credentials.

The 120-second recording closed without a restart. Its 792,956-byte file has
one header, 1,670 consecutively numbered events and one intact footer. The
footer reports `dropped_events: 0`, `lost_state: 0`, no byte-cap hit and
successful I/O. SHA-256:
`269c03b54bb40cfbfd787fa46ddc11a933e9d4cd44a16e02b360cba626c68666`.

Root and an independent read-only reviewer verified **39 complete direct
draw/accepted-submit generation pairs** on device 1. Each has matching complete
command, pipeline and shader records; pipeline generations 19399 and 19422
account for 20 and 19 pairs respectively. All have these exact stages:

| Stage | Bytes | Standard FNV-1a 64 | Entry |
| --- | ---: | --- | --- |
| Vertex | 9,460 | `9cc04d186b9c8fe5` | `VS_DISTORT` |
| Fragment | 6,620 | `f333a137b4e79ced` | `PS_DISTORT` |

This is the first accepted **positive recorded/submitted pairing**. It is not
an exhaustive or wholly complete capture. Event 478 reports state capacity;
26 such reports taint later observations. Of 616 draw records and 616 accepted
submission records, 577 draws cannot be certified as complete pairs. The event
sequence strongly suggests exhaustion of the 256-entry shader table: pipeline
479 references an untracked fragment immediately after the first capacity
report. The shared reason code does not identify the table, so this attribution
remains inferred. A clean footer does not override `complete: false` events.

The capture establishes neither GPU completion, resource/descriptor binding,
UV/eye coherence nor a particular streamed video's source identity. It has no
per-event timestamp or video-frame key. Observing the stream process in the
same session is context, not a draw-to-video-frame association. The installed
shader fingerprints and the explicit matching records establish only the
narrow positive pairing above. Time-ended writer closure in this binary also
does not synchronize all in-flight producers; no absence or exhaustive-window
claim is made from its footer.

There was no timed rollback service. SteamVR continued after recording ended.
After announcing manual restoration, the operator invoked rollback at 13:46:28.
Both activation files were removed at 13:46:28, native restart was requested at
13:46:29 and completed at 13:46:34. The new native compositor PID 368833 had
neither diagnostic mappings nor diagnostic environment variables. The service
was active, the watchdog inactive and both activation paths absent. The private
headset directory was removed by 13:47:45. No persistent configuration, native
binary, shader or camera setting was changed.

The tiny ledgers, capture and selected validation remain private and ignored in
`build/private-frame-manual-run-20261011/`. The SSH credential environment was
cleared and the terminal closed. Local software checkpoint `9e3fa41` also
passed the complete eleven-suite CI; that software result is separate from the
limited live evidence above. Subsequent closure/fixture repairs need only their
focused core and loader/GPU checks; they cannot retroactively broaden this run.

## Post-run diagnostic repairs and validation

The review found that the deployed binary's time-ended writer did not wait for
in-flight bookkeeping before serializing the footer. Source now closes an atomic
admission gate and waits only on the background writer for admitted observers.
Render hooks keep their try-lock behavior; no admission scope spans a forwarded
native Vulkan call. Queued records and loss counters are stable before the final
drain/footer. Tests exercise admitted observers during closure, post-close
counter stability and concurrent producers. The real-loader fixture also now
requires complete accepted-submit records, the same device/command/generation,
later event serial and a footer matching all emitted serials.

The primary integration's first focused run passed the core but rejected the GPU
fixture because its footer reported one dropped event. Source review established
that the single render producer shared the event mutex with the background
reader: fewer than 128 attempted events could not overflow the ring, and a
reader `pop`/`empty` could make the producer's try-lock fail. This queue behavior
predated the closure repair. The reader now uses release/acquire published and
consumed positions; only producers use the try-lock mutex. Actual producer
contention and capacity loss remain reported and rejected. Deterministic tests
cover reader independence and a concurrent 2,048-record sequence with unchanged
payloads, order and counts. No assertion was weakened and no retry-until-green
was used. This identified fixture cause does not reclassify the earlier rejected
headset capture's unknown one-event loss.

After this bounded repair, the integrated core and real Vulkan loader/GPU suites
both pass (2/2, 1.93 seconds), with unchanged fixture pixels. Unchanged suites
retain checkpoint `9e3fa41`'s complete green CI evidence; this focused pass is not
a repeated complete local gate. Strict-warning AArch64 compilation also passes.
The new local-only library is 68,064 bytes, SHA-256
`ff50e29c5de359b2dab6625fe61cdfea6dfedcd0379cfb9ca232427130ac79d5`.
It was **not installed or tested on the Frame**. The fifth attempt still uses
the previously recorded 68,056-byte library identity and its narrower evidence.
Generated build caches and the completed worker worktree are removed after
retaining small validation records; private hardware evidence stays intact.

## First resource diagnostic and rejected live capture

The next local artifact adds the opt-in public-API scalar binding scope. The
reviewed AArch64 library is 133,696 bytes, SHA-256
`c531c2e78e5c26a38c60c9fc167b744c17b41df4d9ca3908f651fb68f0f291e7`.
This artifact was installed and run on the Frame in the authorized manual-mode
resource attempt below. All preceding headset restore and cleanup records
remain historical records of their own runs. Its binary identity supersedes the earlier
example only for this new diagnostic; never stage a different artifact under
this hash.

Activation, after staging and the operator's readiness for the initial restart:

```bash
python3 ~/.local/share/frame-mask-trace/frame_trace_session.py start \
  --sha256 c531c2e78e5c26a38c60c9fc167b744c17b41df4d9ca3908f651fb68f0f291e7 \
  --manual-rollback --bindings
```

This adds two service-local diagnostic variables:
`FRAME_MASK_TRACE_BINDINGS=1` and a private `FRAME_MASK_TRACE_BINDINGS_ARM_FILE`.
The resource tracker observes creation/binding metadata during startup, but does
not spend its 32-draw snapshot budget before the operator reconnects a **VR**
stream. Within the existing 120-second bounded recording window, request it with:

```bash
python3 ~/.local/share/frame-mask-trace/frame_trace_session.py arm-bindings
```

That control operation creates only
`~/.local/share/frame-mask-trace/bindings-arm.marker`, with mode 0600 and a fixed
versioned payload. The background writer checks it at most ten times a second
until accepted; rendering hooks neither inspect the file nor wait. The helper
requires unchanged owned activation files and serializes this operation with
rollback. No second restart or timed rollback is added. Arming describes the
operator's stream context, not proof of a specific selected frame or physical
eye; those still require the exact native bridge.

After copying the bounded capture, announce and use the same manual rollback
command above. Remove the marker with the rest of the private diagnostic folder
only after native operation and absence of activation have been confirmed. The
manifest/service drop-in remain the only activation files; the marker alone
cannot load the layer, and reboot still removes the required service environment.

Software validation: the integrated 13-suite gate passed, including the exact
loader manifest and unchanged GPU pixels. After adding control arming and the
32-snapshot stress fixture, only the three affected recovery/GPU suites were
rerun; all passed. The remaining ten suites inherit the complete passing gate.
Worker association and binding cores additionally passed ASan/UBSan. The layer
cross-compiles with strict AArch64 warnings; the new scalar association ledger
also cross-compiles for Windows. This evidence establishes local software
behavior, not headset resource/eye/frame association or working passthrough.

### Live result and pre-arm budget defect

The initial resource deployment used the exact 133,696-byte artifact above.
Activation was requested at 14:29:58 CEST; the service reported active at
14:30:04. The maintainer confirmed a real PC-VR stream and the helper requested
arming at 14:31:29. Read-only verification found both the native compositor and
the actual `vrlinkrunthread` executable, with the diagnostic mapped in the
compositor. No stream arguments or credentials were collected.

The 981,423-byte capture contains 2,048 events: 208 shader, 107 pipeline, 867
draw and 866 accepted-submit records. Its stable footer reports no queue or
pipeline-state loss but **152 binding-state losses**, zero resource snapshots
and `binding_capture_armed: false`. The event cap closed recording before the
marker was accepted. The helper's old `binding_capture_armed` ledger entry
described only its marker creation, not actual writer acceptance. This run is
**rejected for resource evidence**. It establishes neither current descriptors,
physical-eye/UV values nor exact selected-video-frame association. The causes
of the 152 resource losses are not known from this version's aggregate counter.

The defect was in diagnostic flow: only the 32 resource snapshots waited for
arming; ordinary filtered draws/submits kept consuming the separate 2,048-event
budget while the user reconnected. The six-draw unarmed fixture could not expose
that lifetime exhaustion. No Vulkan rendering argument or shader was changed.
The capture closed itself; manual-mode recovery did not restart the headset.
Private evidence is retained in `build/private-frame-resource-run-20261011/`.

The bounded repair keeps lifecycle tracking but suppresses draw, execute and
submit **diagnostic events** until the writer accepts arming. Native calls still
forward unchanged. Writer acceptance emits `bindings_armed`; the helper records
only `binding_capture_arm_requested` and refuses finished/crashed captures using
a background-writer file lock and a bounded owned-file check. No lock/file I/O
is added to a render hook. The original 120-second total window, 2,048 events,
32 snapshots and 1 MiB cap remain; arming does not extend them. Footer counters
now identify resource loss reasons and the bounded table responsible for any
capacity overflow, without adding a recurring log stream or relaxing proof.

The real-loader fixture now executes 2,400 native draws before arming, confirms
that no pre-arm draw/submit consumed capacity, and requires all six subsequent
draws, their resource snapshots and accepted submissions. A separate 2,400-draw
unarmed fixture must remain empty of those records. Every rendered pixel is
still checked. Focused validation and the replacement artifact are recorded
below once complete; a local pass cannot repair this rejected hardware run.

### Reviewed repair artifact and authorized live deployment

The replacement AArch64 library is 154,088 bytes, SHA-256
`e0f976e4f42ab0fbf03383dc97e928401a8760bce7a1a8277d6ced3255653473`.
It cross-compiles with strict warnings. The three affected pipeline, resource
and real-loader/GPU suites pass; the integrated recovery helper passes 33 checks.
After adding a real-writer file-lock assertion, only its affected GPU suite was
rerun and passed. The other nine suites inherit checkpoint `00e7270`'s complete
green CI evidence. No new complete local gate or hardware success is claimed.
Small records remain in `build/arm-repair-validation-20261011/`.

The old library may remain mapped after its recording closes. Never overwrite
or truncate a mapped `.so`. Stage the replacement under a new filename/inode,
check its exact hash and passive loader probe, then wait for the maintainer's
readiness before interrupting the active stream. A verified rotation may remove
only the old helper's byte-identical manifest/drop-in, retain its tiny rejected
capture in a private archive and install a fresh owned session directory. The
next helper start performs one SteamVR restart with the reviewed replacement:

```bash
python3 ~/.local/share/frame-mask-trace/frame_trace_session.py start \
  --sha256 e0f976e4f42ab0fbf03383dc97e928401a8760bce7a1a8277d6ced3255653473 \
  --manual-rollback --bindings
```

The same exact activation paths and manual rollback command remain applicable.
If a rotation cannot be verified, stop and use the documented native recovery;
do not delete unknown files or bypass loader policy. No automatic restart at
capture expiry is reinstated. The rejected archive/staged files have no loader
manifest of their own and must be removed after retained evidence and recovery.

### Armed resource capture result

The maintainer confirmed restart readiness. At 16:28:07 CEST the exact old
activation files were verified and removed, its private directory was renamed
to the rejected-run archive, and the staged replacement directory was renamed
into place. The mapped old library inode was never overwritten or truncated.
At 16:28:15 the replacement helper activated the reviewed 154,088-byte hash
above and requested one SteamVR restart. The service was active at 16:28:22;
read-only verification found compositor PID 522854 with the diagnostic mapped,
the expected five service-local diagnostic variables and no active watchdog.
The passive loader probe had separately loaded the same hash and matched the
entire serialized manifest without creating an instance/device.

The maintainer then confirmed the PC-VR game was visible. Read-only inspection
also found `vrlinkrunthread` PID 525191 running the actual native `vrlink`
executable. The helper requested arming at 16:29:42; the writer emitted one
`bindings_armed` event, followed by all **32 resource snapshots**. The earlier
control-budget defect is validated as repaired in this live run: before arming,
only shader/pipeline inventory consumed capture space. No automatic restart
occurred when recording closed.

The 1,046,498-byte capture closed at its byte bound and retained 210 shaders,
108 pipelines, 772 recorded draws, 32 resources and 772 accepted submissions.
The footer reports zero event drops and zero pipeline-state losses. Two trailing
emitted records do not appear in the bounded file once its byte cap is reached;
the cap is explicit, so this is not a complete event transcript. All 32 resource
records and the arm event are retained. Evidence, byte-cap semantics and derived
validation are private in `build/private-frame-armed-resource-run-20261011/`.

**Complete resource/UV/frame proof is still rejected.** The footer reports six
binding-state losses: five try-lock contention losses and one unknown-state
operation, with zero capacity/unsupported-command/stale/bounds losses. The exact
API operations that lost state were not recorded by this version. Every populated
snapshot is marked incomplete by the historical loss guard; its reason 2 does
not mean its own snapshot lock failed. No loss is cleared or treated as harmless.
The imported image/view and sampler are also locally incomplete: the observer
does not interpret their extension chains. A zero `unsupported` loss counter
does not certify supported `pNext` chains; those object-level rejections use
their completeness flags rather than that global counter.

The following scalar observations are retained for the next diagnostic design,
not certified GPU-consumed values or an activation decision:

| Set 0 binding | Observed scalar data | Limit |
| --- | --- | --- |
| 0 and 4 | Same 524,288-byte buffer, alternating effective offsets 320/960 and range 272. | Two slices are visible; physical-eye identity and contents remain unread. |
| 2 and 6 | Same 2,800-byte buffer, effective offset 0/range 2,800. | Shader interpretation cannot substitute for live coherent values. |
| 3 | 400-byte buffers, effective offset 0/range 400. | No constant contents were read. |
| 9 | 30 snapshots retain a 1,152 × 4,608 × 1 image in `VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16`, with DRM modifier tiling. | Two snapshots lack image metadata; extension interpretation and resource/frame bridge remain incomplete. |
| 25 | A separate sampler is observed, including a custom border-color enum. | Its actual extension-chain parameters are not captured. |
| 31 | Target entries say absent for the observed layout. | Global `layout_has_binding31` remains unknown after loss; adding a mask binding is not authorized. |

The sampled native shader tuple again matches the earlier verified layered VS
and achromatic fragment shader. Neither that tuple nor allocation identity
proves selected content generation. No memory/image readback, shader mutation,
mask activation or change to native rendering occurred. The next source work
must address nonblocking metadata tracking and explicit bounded extension
interpretation, rather than repeating the same lossy capture and hoping for
different timing.

### Verified manual recovery and cleanup

After the maintainer approved manual rollback, the helper removed both exact
activation files at 16:38:33 CEST, requested the native SteamVR restart at
16:38:34 and recorded completion at 16:38:40. Read-only verification found
SteamVR active with compositor PID 543270, no diagnostic library mapped, no
diagnostic environment variables and no active rollback watchdog. The capture
and complete recovery ledger were retained privately on the development machine.

SSH became unavailable when the maintainer put the headset into standby; this
did not undo or interrupt the completed native recovery. After the maintainer
woke it, native state was verified again. At 16:46:43, the exact owned current
and rejected-session directories were removed after verifying their inventories,
library hashes, closed-writer locks and copied-evidence hashes. A final check
confirmed both directories, the staged directory and both activation files were
absent while SteamVR remained active. Cleanup did not restart SteamVR. The
maintainer was told that standby was safe and no more headset access was needed.

The private cleanup and recovery validation are retained beside the capture in
`build/private-frame-armed-resource-run-20261011/`. No active or residual
diagnostic installation remains on the headset. The next implementation is
described in [the passive binding loss audit](PASSIVE-BINDING-LOSS-AUDIT.md):
replace lossy hook-time bookkeeping with bounded ordered metadata replay and
interpret explicitly supported import/sampler extension metadata. These are
source work to complete before another resource capture; no native passthrough
or resource/frame/UV proof is claimed by this diagnostic result.
