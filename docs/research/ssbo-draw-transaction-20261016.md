# Inert SSBO original-draw transaction (2026-10-16)

This component is an offline foundation. It does not install a native layer,
activate receiver shaders, identify video frames or eyes, or present passthrough.
The original receiver hook bridge is a separate integration boundary.

## What is proved here

`layer/pixel_ssbo_draw_transaction.{h,cpp}` owns a finite CPU state machine and
one explicit completion fence. It takes the immutable `Clone::preparedRecipe()`
snapshot actually used by the existing cold clone owner. A different caller
Recipe or a public all-true Admission structure cannot grant command permission.

The synchronous original `DeviceCreate` queue infos must describe exactly one
ordinary queue; its actual `GetDeviceQueue` result establishes the only accepted
queue identity. Unknown queues, protected/multiple queue declarations, unknown
submit shapes and overlapping execution are refused. The native submit, native
fence and native command-buffer flags are never changed.

The native integration path adopts an opaque `RecordingTicket` minted by the
existing full `LiveTracker` at an original successful Begin. It carries the exact
existing primary command/pool generations, device and family, so no creation API
history is replayed or fabricated. Consume it synchronously at that callback;
it is not a transferable or asynchronously replayable native lease. The separate
one-pool/one-command lifecycle methods exist for owned proofs and already
observed history; do not broadcast the receiver's entire allocation stream into
that deliberately narrow observer. The full tracker remains the native
inventory owner.

Explicit arm marks the Clone referenced before emitting any command. The reset
is a 128-byte GPU Fill with incoming and outgoing record-buffer barriers,
recorded before native render passes in the original primary command buffer.
Every serial execution therefore resets the claim; recording once or clearing
mapped CPU bytes once cannot authorize stale replay data.

Preceding unrelated direct/indexed draws and legacy render scopes stay original.
The controller counts them and can select the real target at ordinal 4. Only one
matching native target draw is replaced: bind owned compatible Set1, bind probe
pipeline, execute the exact original direct or indexed parameters once, then
**immediately restore the original pipeline**. Set0, original push constants,
vertex/index bindings and dynamic state are not rewritten. Native integration
can supply exact live target state through the opaque `TargetTicket` minted by
the full tracker. Journal provenance and live Core generations are distinct
namespaces; the adapter must not numerically equate them.

After forwarding original EndRenderPass, `endRenderPass()` emits the owned
shader-write/transfer-write to HOST_READ buffer barrier outside the render pass.
Later draws, another target render scope, subpasses, secondary execution,
unknown routes or retirement of a matching resource invalidate publication.
Typed retirement avoids confusing different Vulkan object kinds with the same
raw handle bits. Consumed shader-module/layout provenance is definition history,
not a reason to retain or resurrect those original objects.

Private submit tickets match owner instance epoch, original command generation
and single-use nonce. A controller's reused address never reuses its authority
epoch. Native integration must retain the original queue's external lock across
beforeSubmit -> untouched native submit -> afterSubmit. Only then is an empty
owned-fence marker submitted. A reusable recording may execute serially again
only after the previous marker actually signaled; concurrent/pending reuse or
unknown queues fails closed. No queue or device wait is issued by this code.
The bounded native-submit profile accepts at most 32 ordinary submit infos,
64 commands/waits/signals per info and exactly one occurrence of the selected
command across the entire original call. Other original commands stay untouched.
A single canonical `VkTimelineSemaphoreSubmitInfo` is supported when its counts
match the original semaphore counts; unknown, chained, device-group or protected
extensions fail closed with the observed batch/count/type recorded as scalars.
Valid unrelated batches create no marker and do not poison optional capture.
Submit2 still requires an explicit adapter or unknown-route refusal.

Publication requires the actual owned fence's success **and** successful native
retirement of the saved old reusable generation. The independently completed
source-copy owner must provide matching image/view/memory identities, layout,
extent, command, pool, both generations and execution serial. Matching raw image
handles or an earlier replay's bytes are insufficient. The input is raw metadata,
not GPU authorization and not source video-frame/eye evidence. Noncoherent memory
is invalidated only after these conditions. A failed/default/foreign/replayed
ticket cannot consume another admission or retire a newer generation.

The Clone and owned fence release only after referenced recordings retired and
an actual original DeviceWaitIdle success or terminal device loss was observed.
A marker fence alone and idle alone are each insufficient. One Clone has one
externally synchronized cold owner. Sequential accidental second-controller
arming or release of another controller's referenced Clone is refused. Refusal/error never
loosens this requirement. The actual original `vkDestroyDevice` entry is a separate explicit terminal
event: after device/all-queue external synchronization and actual idle/lost, it
retires the adopted generation and cancels obsolete tickets before freeing owned
children and forwarding untouched native destruction. A wrong device or an idle
observation without that destruction event cannot certify final retirement.
This follows the [Vulkan DestroyDevice contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html),
which externally synchronizes device/queues and requires destroying owned
children first. The C++ destructor performs no Vulkan calls or waits.

## Focused evidence

New `tests/test_pixel_ssbo_draw_transaction.cpp` reuses the existing finite
owned-clone driver fixture without running its old test main. Causal controls
exercise the real transaction command dispatch, exact indexed/direct parameters,
prepass reset on three serial executions of the same recording, original restore
adjacency even on reentry/thread contention, opaque adoption, queue declaration
checks, later draws/scopes and exact old-generation retirement. Source metadata
mismatches cover command/pool/serial/image/view/memory/extent independently.

Final local focused results:

- 2,151 transaction checks and 5,777 invoked owned-factory fixture checks pass.
- The same checks pass under ASan + UBSan with leak detection.
- AArch64 C++17 cross-build succeeds (ELF64 AArch64); not executed on Frame.
- `-Wall -Wextra -Werror -pedantic` passes for all three builds.

Private run records are in `build/private-draw-transaction-20261016/`. Each final
reaped run called the shared `cleanup_completed_run` API, followed by removal of
its exact private compile cache when that API correctly did not recognize this
fork's separate evidence layout. Logs/results/source remain; build caches and
executables do not. No Frame contact occurred and no native visual success is
claimed. Initial harness issues (borrowed main return warning, a missing borrowed
standard include and one harness retirement after an intentionally unresolved
submit) were corrected in the harness; they were not native driver outcomes.

Existing real GPU prefix-restore/cohort/owned-factory proofs remain inherited
separate evidence. This focused transaction suite uses fake dispatch; it does
not substitute for a real GPU transaction integration or receiver history audit.
Before a native trial, the integrator must establish original queue shape,
source capture cohort, actual indexed/draw ordinal routing, compatible prefix,
supported shader profile/features, callback coverage, command/source lifetime,
return/failure paths and original opaque fallback. Real frame/eye association
and alpha/passthrough presentation remain separate work.
