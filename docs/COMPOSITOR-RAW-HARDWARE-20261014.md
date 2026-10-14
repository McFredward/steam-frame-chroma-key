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

## Successful arming still missed stream allocation

The following catch-up attempt passed native startup, isolation and the fresh
arm health sample. Its accepted cumulative startup evidence was 395,857 bytes.
The operator had confirmed a visible PC-VR stream; the raw writer observed
the marker about 69.60 seconds into its original 120-second window. The footer
reports armed but unclaimed, zero private bytes and no local resource join.

The passive journal recorded 32 binding snapshots: 16 UI samples and 16 complete
imported YUV descriptor samples, with eight distinct imported image generations
seen twice. The latter use VS_DISTORT/PS_DISTORT, DMA-BUF import and YCbCr
conversion. These are real recording-time API resource facts, not proof of a
selected PC frame, physical eye, native UV ownership or coherent GPU use.

The journal reached its 1-MiB cap about 3.32 seconds after arming, after 882 draw
and 294 submit records. All 699,909 admitted lifecycle publications replayed
without recorded loss before closure. The remaining raw window could not rely
on continued journal currentness. Cap closure alone does not explain the
unclaimed raw candidate: it does not itself claim a Begin or retire the raw
observer's native notifications.

Independent retained-source checks establish a pool of 16 imported resources
rebuilt on decoder source change, then reused during ordinary presentation.
This explains why a post-visible-stream signal can miss the import burst.
The next selection window is therefore explicit readiness immediately before
a fresh connection, as described in [the pre-stream contract](COMPOSITOR-PRESTREAM-ARM.md).
It is not a claim that the first following import must be video.

The new raw-only collector deliberately samples at most 32 draw, 16 execute and
16 submit observations. It reserves the existing 32 binding attempts until an
actual tagged import request is published; those samples are not guaranteed
to consume the imported image. Optional output exhaustion no longer retires
the owning lifecycle mirror: it drains until the unchanged original deadline.
Actual losses and deliberate output omissions remain separate. The final
artifact carries a hidden read-only FCMSMP01 quota/lifecycle marker, with no
new launch getter or private acquisition. Bounded aggregate public Begin
notifications distinguish observations before/after candidate arming and
skipped pre-arm After callbacks; they are not packet/frame association proof.

Recovery restarted original SteamVR, passed its native UI gate, observed the old
target's exit and no own mappings in 15 observed current service processes,
then archived evidence and removed 45 verified session/input files. Original
files remained unchanged. Private captures and independent instruction/API
classification evidence are retained in
`build/private-compositor-catchup-integration-20261014/` and
`build/private-compositor-stream-import-review-20261014/`. No passthrough shader
or mask was activated.

An operator acknowledgment selects the recording period; it is not source-frame
association proof. Missing, invalid, stale or lost evidence keeps rendering
opaque. Capture expiry stops recording only. Installation removal and a native
service restart remain coordinated recovery operations, with no timed restart.

## Pre-connect capture selected a later UI import

The authorized pre-connect attempt on `b5098ae` passed native startup and armed
about 3.98 seconds into the unchanged absolute window, before asking the operator
to connect. The operator subsequently confirmed a visible, normally functioning
PC-VR stream. Unlike the earlier post-visible attempt, the first armed Begin
produced a successful native reply and a current local CPU resource join.

That candidate was a **64 by 24, format-50 Facet UI surface**, not an identified
video buffer. The capture observed 27 authenticated public Begin calls before
arming and one after it; their paired pre-arm After callbacks remained inert.
Private acquisition stayed at 16 parent, 412 scalar and 21 wire bytes (449 total),
with ten bounded records and a quiescent successful terminal result. A successful
import is not sufficient stream eligibility: unrelated native UI imports can
still arrive between readiness and the actual stream allocation burst.

The raw-only sampling/lifecycle repair worked within its stated scope. The final
journal stayed open for its full 120-second window, emitted 387,263 bytes instead
of reaching its 1-MiB cap, and replayed all 645,863 public metadata publications
with no recorded loss. It intentionally omitted 198,062 draw and 10,430 submit
observations. These counts are not an exhaustive draw trace or a performance
comparison. Its 32 descriptor snapshot attempts were again spent after the UI
import request; they do not establish a draw of a selected stream candidate.

The next change must select stream-compatible public image metadata **before**
the irreversible one-candidate claim and private acquisition. Moving a format
predicate after the existing Begin claim cannot solve this: that Begin already
copies its parent and dispatch scalars. Any deferred selection must retain exact
same-thread/source pairing and prepare stack ownership outside graphics
callbacks. Historical scalar values must not be invented or treated as observed
at an earlier boundary. Format/extent eligibility remains weaker than source
session, frame, eye, UV or GPU association; those admissions stay false.

Coordinated recovery passed original SteamVR's native UI gate, confirmed the old
compositor had exited, and found no diagnostic mappings in all 15 observed
current managed processes. Twelve archived evidence files were copied and
hash-verified before removing all 45 exact owned session/input files. Native
files were preserved; no additional automatic restart, image/shader change or
PC Steam sender modification occurred. Private evidence is retained under
`build/private-compositor-prestream-integration-20261014/`.

## Selected P010 import belongs to the current Steam Link process

The authorized v5 attempt on `cbd9894` excluded 55 public image creations before
claiming one P010 (format 1000156013), 1152 by 4608, one-layer Facet import. The
native image/bind/view/send/reply path completed with a successful exact reply.
Private acquisition was 16 parent, 388 scalar and 21 wire bytes (425 total),
with no earlier private Begin/Early replay. Only two worker slots were reserved.

The captured client value was 3091578. A subsequent read-only public `/proc`
snapshot found that current PID executing
`/opt/steamvr/tools/vrlink/bin/linuxarm64/vrlink`, with comm `vrlinkrunthread`.
This identifies the current process class, not a birth/endpoint epoch, accepted
socket, source PC session, particular frame, physical eye or native UV mapping.

The same owning journal delivered a complete receipt with zero errors/loss and
`current_after_drain=true`, matching the raw image/view/memory handles and their
actual generations. Nevertheless the raw consumer's single later `join()`
returned false. Its old serialization omits some comparison fields and the
individual validation outcome, so the exact failed conjunct is **not proved**
by this capture. Source inspection establishes that the final check can also
refuse an unchanged resource when unrelated publications arrive between receipt
delivery and consumer revalidation. Global cursor advancement alone is not a
generation mismatch. The next repair must report the exact decision and retry
only bounded transient nonquiescence, using the same immutable receipt and
unchanged strict generation/loss checks within the original deadline.

None of the 32 retained descriptor snapshots references the selected image
generation 2993329, memory generation 2993330 or view generation 2993331. They
all sampled other RGB/UI textures. The import gate reserves time but does not
select a requested resource. Exact-generation filtering at ordered replay is
required before spending a full snapshot slot; a late mutable descriptor lookup
or dimensions-only match is insufficient.

The final journal replayed all 811,507 public publications without recorded loss
or reaching its byte cap. The native UI stayed functional, as confirmed by the
operator. Coordinated recovery passed the original startup gate, confirmed old
compositor exit and absence of own mappings in 15 observed managed processes,
then hash-verified the archive and removed all 45 owned files. Native/system
files and the PC sender remained unchanged. Private evidence is retained under
`build/private-compositor-stream-image-integration-20261014/` and the independent
analysis under `build/private-compositor-stream-image-join-review-20261014/`.
Passthrough remains disabled; neither the recorded import nor the refused final
join establishes frame, eye, UV or coherent GPU association.

## Bounded currentness repair and exact-resource draw probes

The next v6 offline package retains the v5 public image selection, one private
candidate, 757-byte acquisition bound, 32 raw records, 64-KiB raw output,
1-MiB journal output and original absolute 120-second capture deadline. Its
FCMRAW05 / envelope version 5 marker authenticates the changed diagnostic
contract; the four module profiles and all eight envelope limit words remain
unchanged. No new launch environment getter, native provider or PC sender
modification is introduced.

The owning journal now reports its actual pre/post validation stages and exact
refusal reason. Before initial delivery, only transient active publication,
pending replay or changed publication can retain the same immutable pending
receipt for up to 64 validations. A permanent generation mismatch, recorded
loss, closure, halt or bridge retirement is never retried into validity. An
exhausted invalid delivered receipt cannot subsequently be relabelled current.
Pending queue waits and exhausted initial delivery are separately counted.

The raw consumer retains all 31 original immutable compatibility checks. It
rechecks runtime currentness before and after the unchanged owning-journal
validator. Only the same three typed transient journal outcomes permit another
check of the same raw record and receipt: at most 64 validations, 1-ms pauses
and the same original absolute capture deadline. The final report includes all
comparison fields, initial delivery decisions, final consumer decisions and
which stages were actually observed. These sampled CPU decisions are neither
an atomic native-resource lease nor GPU-completion evidence. The source-proven
race is a possible explanation for the v5 refusal; that capture does not prove
which conjunct failed.

The FCMSMP02 collector submits at most 4096 lightweight ordered public draw
probes. It retains draw metadata in 256 fixed slots (less than 512 KiB) while the
existing journal replays each probe at its exact observation order. Only a match
to the selected tag, cookie, device and exact image/view/memory generations and
generation links spends one of the 32 full snapshot slots. Unrelated UI probes
release their slots without an output placeholder or full snapshot. Filtering,
unknown state, quota/slot omissions and output cancellation are separately
reported; deliberate sampling is not an exhaustive draw history.

The inspected descriptor subset is explicitly set 0, bindings
`{0,2,3,4,6,9,25,31}`. A filtered result means no exact match in that subset,
not absence from every possible descriptor. No graphics callback reads mutable
Core state, allocates, waits for the background owner or performs file I/O.
Source-frame, eye, UV, accepted-submit, GPU coherence and mask admission remain
false even after a successful CPU or descriptor join.

Focused software and final artifact evidence are retained privately under
`build/private-compositor-join-target-integration-20261014/`, with worker evidence
and independent review beside it. Passing software checks do not demonstrate a
headset-visible result. The previous diagnostic has been fully removed; this
new package requires fresh operator permission and readiness before deployment.
