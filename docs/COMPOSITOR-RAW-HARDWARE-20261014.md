# Native import join and operator-reserved capture (2026-10-14)

This is diagnostic infrastructure, not working Steam Link passthrough. No mask,
shader replacement, pixels, source frame, eye, native UV mapping or GPU lifetime
has been admitted. The PC Steam sender remains unchanged.

## Hardware evidence

The authorized v3 capture on the updated Frame reached the pinned native Facet
creation route. Its image creation, memory binding, view creation, native send
and reply completed. The existing owning Vulkan journal joined that exact local
image/view/memory generation without recorded loss. The observer copied 449
private bytes, below its separately reviewed 757-byte maximum. Finalized is the
normal closing refusal for this successfully completed diagnostic, not evidence
that the import crashed.

This candidate was a 1920 by 1080 startup UI import, recorded before the operator
started a PC-VR stream. It proves a local CPU resource join only. Neither a
monotonic capture time nor the native reply token proves the selected PC source
session, video frame, eye, UV ownership or GPU consumption.

Private receipts and original captures are retained under
`build/private-compositor-facet-integration-20261014/`. Native binaries, capture
bytes and disassembly are not redistributed.

## Why the visible restart sequence happened

The startup gate required an explicit renderer command line. On this Frame,
the actual forked native system UI retains `--type=zygote` in `/proc/cmdline`.
Its fresh native application registration, libcef mapping and live Compositor,
GpuMemoryThread and Chrome_ChildIOT tasks establish the missing renderer role.
A Steam browser renderer outside the SteamVR service group is not a substitute.
The gate therefore rejected an otherwise advancing launch and initiated the
already authorized fault rollback.

Restoration then encountered a delayed coredump log whose MESSAGE was truncated
to null by systemd's default JSON output. That parser rejection triggered a
second original-state restart. Native compositor shutdown SIGSEGV records also
occur in original-state transitions at the same native PC; those records alone
do not establish that the diagnostic caused a crash. The corrected reader uses
bounded selected fields with `--all`, classifies the crashed process rather than
the logging daemon, excludes only authenticated prior-process crashes, and
still rejects fresh crashes in the current service scope.

Required UI roles remain checked throughout monitoring. Ordinary unrelated
Chromium thread creation or exit must not revoke an otherwise authenticated UI
process or consume its one-time native connection/registration evidence.

The user confirmed the restored original UI. All verified owned diagnostic
files were removed after restoration; native service files and user layers were
left unchanged. No further Frame installation or restart is authorized by that
confirmation alone.

## Reserve the next capture for the PC-VR stream

The v4 package uses source marker FCMRAW03 / envelope version 3. It keeps the
same one-candidate, 757-byte private-read, 32-record, 64-KiB output and absolute
120-second bounds. It cannot relabel an old automatic artifact as operator-armed.

Cold startup still authenticates the reviewed selector, audit providers and
owning journal. Before the operator signal, native Begin callbacks perform only
bounded atomic/TLS metadata handling: no candidate claim, stack preparation or
private copies. A Begin skipped before arming cannot become a candidate merely
because its After arrives after arming. The existing journal replays lifecycle
metadata but reserves draw/submit events and binding snapshots for the signal.

After the operator confirms a healthy UI and visible PC-VR stream, the helper
revalidates the current service invocation, compositor identity and mappings,
open locked captures and remaining absolute deadline. It exclusively creates
`capture-arm.marker` in the existing protected session directory. Only existing
background writers read that marker. It does not restart SteamVR, extend any
deadline, reopen a journal or reset a candidate/read budget.

## First operator-armed attempt and bounded log catch-up

The v4 installation passed its native startup and isolation checks. At the
operator's visible-stream confirmation, the arm helper refused a native log
delta above its old 128-KiB single-file limit. It never published the marker.
The raw capture consequently expired unarmed with zero private bytes; the
owning journal retained 342 shader/pipeline records without loss, draw records,
binding snapshots or a byte-cap violation. This was a diagnostic evidence
refusal, not an observed headset startup failure. No timed restart occurred.

The saved startup counter was 265,191 bytes. The matching previous log nodes,
inspected after coordinated restoration, had grown by 161,919 bytes for
vrserver and 30,719 bytes for vrcompositor. Those completed ranges and the saved
counter total 457,829 bytes before a further journal sample. They do not prove
the exact sizes at the earlier arm instant, but confirm that a single-file
burst can exceed 128 KiB while remaining inside the 512-KiB cumulative budget.

The reader now reserves a whole snapshot against that unchanged cumulative
budget and drains it with unbuffered reads of at most 128 KiB. It concatenates
bytes before UTF-8 decoding and scans all complete lines for fresh failures;
there is no tail-only shortcut or skipped gap. The startup gate accepts the
combined bounded log snapshot instead of imposing a second 128-KiB per-file
cap. Bytes already read remain charged on truncation or rotation, and a failed
arm sample persists a denied checkpoint so a retry cannot reuse the older
passed counter. An oversized unterminated line is refused before replacing the
bounded pending fragment; the failure receipt therefore remains small enough
to persist. Existing fresh timestamp and process-identity rules for logs
which rotated before sampling remain in force.

The arm attempt occurred about 100.57 seconds into the absolute 120-second
capture, leaving about 19.4 seconds. The eligible native callback handles
resource imports, not every rendered frame. Buffers may already have been
imported before a visible-stream acknowledgment; neither this repair nor the
remaining time guarantees another eligible import or PC-stream association.

Recovery removed the exact own activation, restarted original SteamVR,
passed native UI startup checks and preserved original files. The old target
process had exited, and readable mappings in all 15 observed current SteamVR
service processes contained no owned diagnostic libraries. The broader scan
of all user processes was refused at an unrelated unreadable `/proc` mapping;
it is not reported as an all-process unload proof. Archived evidence was
copied and hash-verified before removing the 39 verified session/input files.
Private receipts are retained under
`build/private-compositor-armed-integration-20261014/`.

The software evidence consists of a 103-test startup-health pass and a 98-test
session pass, followed by affected checks for the later limited repairs: all
22 native-source tests and both denied-arm tests passed. Earlier passing gate,
runtime, ARM artifact and session evidence is inherited for unchanged areas;
this is not a new complete native/GPU or headset validation.

An operator acknowledgment selects the recording period; it is not source-frame
association proof. Missing, invalid, stale or lost evidence keeps rendering
opaque. Capture expiry stops recording only. Installation removal and a native
service restart remain coordinated recovery operations, with no timed restart.
