# Optional original-invocation receiver bridge (2026-10-16)

`FRAME_PIXEL_SSBO_TRANSACTION` is a build-time diagnostic option, off by default.
It adds no mask presentation or passthrough activation. The existing pixel
snapshot path is unchanged when this option is off. Definitions, original Vulkan
calls and application feature requests remain authoritative; no caller-provided
boolean is accepted as proof of a native lifetime.

## Selected recording and evidence

The cold worker retains the actual successful original shader, Set0, layout,
render-pass and pipeline definitions. It accepts only the pinned original
fragment shader and the separately pinned **Sparse32** count1 transformation
(9,888 bytes), whose original color expression is preserved. Only an invocation
whose floored X and Y coordinates both lie on a 32-pixel grid attempts the SSBO
winner election. Full-fragment atomic election is deliberately not selected;
its measured cost is unsuitable for a stereo-sized target. If no grid invocation
wins, the zero header refuses publication instead of inventing a sample.

The actual original device queue creation request and original queue retrieval
must satisfy the transaction profile. Queue retrieval may legally happen after
pipeline creation: selected owned definitions wait until that queue exists.
The bridge never assumes queue zero or the queue used by a different device.

After manual arming, a valid source candidate identifies its actual drawn
pipeline and native command buffer. The cold worker looks up and clones that
exact creation generation, independently of creation order and the four-receipt
logging budget. The bridge then waits for that command buffer's **next actual successful
Begin**. The production tracker mints opaque recording and target tickets from
its observed generations; the bridge cannot construct those tickets. Reset is
recorded before any render pass. Prior ordinary direct/indexed draws are
preserved; zero-count calls still advance both observers' matching ordinals. Only the exact final original target draw is replaced once with the
owned original-output observer, and the original pipeline is immediately restored.
Unknown routes and later draws invalidate publication. No allocation, shader
compilation, file output or wait occurs in draw callbacks.

Raw, sampled and optional uniform copies remain in the same original recording
as the SSBO. A source execution counter is incremented by each successful
**original** submit containing that exact generation, including serial replays.
Its value must independently match the transaction's accepted completion marker
execution. The writer does not read the transaction's latest counter and label
unrelated source bytes with it. Exact command/pool retirement and completed
same-queue work are required before either source pairing or record export.

Successful output adds `ssbo-original-invocation.bin`, exactly 128 bytes, plus
bounded metadata in `snapshot.json`. The claim, magic, version and word-count
header are checked before export. Metadata retains source image/view/memory
identities and generations, extent/layout, command/pool generations, execution,
draw kind/arguments/ordinal, original submit shape and actual queue facts.
This is **not** a video-frame association, an eye association or native visual
verification. Those authority fields remain false.

## Cancellation and teardown

Original pipeline/layout/render-pass destroy entry advances a sticky cold epoch,
including during preparation. Target descriptor changes, destruction of the
selected descriptor pool and selected source resource destruction invalidate an
open generation. Known unrelated descriptor pools and resources do not. An
already retired immutable record is unaffected by later reuse/destruction of
native handles. Only the adopted command/pool lifecycle reaches the single
transaction controller; unrelated native command buffers do not enter it.

The writer must observe the bridge's closed-generation acquire boundary before
calling its transaction. Merely observing the older raw-copy retirement flag is
insufficient. This avoids concurrent retirement/publication callbacks being
misclassified as invalid overlap.

At original `DestroyDevice` entry the writer is stopped and joined. Actual
successful `DeviceWaitIdle` or terminal device loss is observed, the exact device
is marked as destroying, and every owned Vulkan child is explicitly released
before forwarding the original destruction. No destructor performs an implicit
Vulkan wait or release. No automatic restart or rollback timer is added.

## Focused verification

`test_pixel_ssbo_receiver_bridge.cpp` runs public refusal guards without private
shader files. Optional private original VS and PS arguments exercise the real
cold canonical transformer and pass the same original layout/pipeline/shader
creation inputs to both production observers. Vulkan work is recorded by a
finite fake dispatch: these controls prove callback/state integration, **not**
actual GPU execution or a headset picture.

The private controls cover late queue lookup, prior UI draw plus indexed target,
legal flags0 serial replay pairing, zero-count preceding direct/indexed calls, missing raw-copy rejection, malformed SSBO header, exact
source/pool cancellation, unrelated lifecycle filtering, the publication
retirement boundary, replacement-generation reuse and explicit terminal cleanup.
Actual Sparse32 shader color/record and cost evidence comes from the separate
canonical original-shader GPU fixture; it is inherited rather than rerun here.
No Frame installation or native activation is part of this integration.

The later [pipeline selection review](NATIVE-PIPELINE-SELECTION-20261016.md)
records the actual six-variant evidence and the corrected post-arm selection.
