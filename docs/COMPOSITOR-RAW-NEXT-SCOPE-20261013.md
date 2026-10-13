# Next compositor resource observation

The client capture on 2026-10-13 observed a real, complete import request/reply
and the receiver's following reference of the same nonzero token. That is a
useful local transaction result. It does not identify a compositor image, an
uninterrupted connection shared by the two processes, or a selected PC video
frame. A compositor observation must be acquired during a new, explicitly
prepared run; a token from the completed client run cannot be joined to a later
compositor capture by numeric equality.

This document is an offline implementation plan. No Frame access, new
installation, kernel query, native executable change or rendering change was
performed for it. The source decoder and copy helper are foundations; they are
not a deployable observer or hardware authorization.

## Useful result to acquire next

Record one exact compositor import invocation, its synchronous public Vulkan
image/binding/view results and its complete successful 21-byte native reply.
Resolve the tagged public creations through the existing owning resource
journal on its background worker. This can establish a **local** relation from
the actually sent token to the observed image, allocation and view generations.
It must remain a distinct raw record with connection continuity, cross-endpoint
namespace, source-frame, eye/UV, accepted submission and GPU-coherence proof
unavailable.

Do not enter `frame_compositor_attempt::Capture` with a fabricated
`frame_import_pipe::Lease`. Its stronger lifetime requirements are deliberate.
Reuse `native_import_route`, the immutable Vulkan notifications, tagged resource
publications and background journal validation without weakening the strict
capture. Neither a diagnostic invocation tag nor its sequence is a video-frame
identity.

The current `FRAME_MASK_BUILD_IMPORT_ENTRY` layer already forwards the three
Vulkan operations through its real per-device next providers. Its notification
listener is unset. A new root adapter must register its immutable listener
before the eligible native thread starts and arrange an explicit, owned bridge
to the same journal outside callbacks. Registering a second journal or looking
up the newest handle would break the join. Current resource lifecycle records
are retained before draw arming; preserve that behavior so startup image
creation is not silently missed.

## Current source contract and copy budget

All addresses here belong to the retained compositor SHA-256
`0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96`.
The current libc is
`be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b`.
Each requires a freshly validated process-specific loader mapping and provider
identity before runtime use. The older `0x137dd0`/`0x137e8c` helper locations in
the 2026-10-11 report belong to another build and are not current hook sites.

| Boundary | Current original return | Sparse private stack bytes | Acquisition |
| --- | --- | ---: | --- |
| Import begin | `0x153fc0` | 12 | After original lock succeeds. |
| First import unlock | `0x1540b0` | 12 | Before original unlock; reuse the copy after its result. |
| Image creation | `0x2c75ac` | 80 | Stack before original call; public result after it. |
| Image memory binding | `0x2c7684` | 80 | Stack before original call; public result after it. |
| View creation | `0x2c7804` | 80 | Stack before original call; public result after it. |
| Reply send lock | `0x172ea0` | 28 | After original lock succeeds. |
| Positive reply write | `0x172c00` | 44 per write | After original write, with its retained entry and actual positive prefix. |
| Reply finish unlock | `0x172fbc` | 28 | Before original unlock; reuse the copy after its result. |

The proposed raw attempt allows at most eight successful partial writes, 672
copied stack bytes and 21 copied reply bytes: **693 private bytes** in total,
excluding any separately declared cold bootstrap. There is no request-payload,
pipe-heap, image, shader or mapped-uniform copy in this minimal attempt.
The request opcode is supported by the pinned handler route; this scope does
not independently capture the compositor's received request header.

The 80-byte creation copy consists of four fixed FP/LR pairs, a four-byte client
scalar, the saved pipe and dispatch sequence. Its zero-filled scratch window is
1,344 bytes; the holes are not native reads. The write frame relation of 112
bytes and finish SP offset of 32 bytes are valid specifically for the complete
21-byte response's rounded packet allocation. They are not general layouts for
arbitrary writes or larger responses.

Before any private copy, the root must validate the original paired call,
current native module, same current thread capability, exact site/entry shape,
ordering and remaining budget. A reader must account for partial reads and
copies whose ownership postcheck fails. The sparse helper's requested/confirmed
counts are not a replacement for that actual acquisition ledger. No deferred
native-pointer chase, symbol lookup, allocation, socket wait, output or new
lock belongs in a graphics callback.

Completion requires one matching image/bind/view chain, unchanged native
invocation/client and public device identities, real native success, contiguous
positive reply prefixes, exact opcode `0x235`, matching sequence, nine-byte
payload and nonzero fixed64 token. Nested, reordered, incomplete, failed,
ambiguous, unsupported or lost observations terminate this raw attempt; they
do not reset it for a later favorable result. The footer must distinguish a
complete queued record from abandonment and establish quiescent producer totals.

## Narrow cold-thread bootstrap candidate

The actual libc exports `poll` and distinct `__poll@GLIBC_2.17` at the same RVA
`0xdec60`, size 332. A selective compositor-to-libc binding can therefore use
the same architecture as the reviewed client epoll bootstrap without wrapping
`accept`, `getsockopt` or `pthread_create`. This is actual-file source evidence;
the new poll veneer/provider forwarding and cancellation behavior have not yet
been executed.

The proposed entry is public poll with original LR `0x172388`, FP equal to SP,
`x0 == SP + 56`, `x1 == 1`, and `x2 == 200` (also live `w20 == 200`). The native
IPC dispatch loop supplies that exact timeout. After independently safe stack
preparation, two fixed pairs provide a **separate 32-byte bootstrap scope**:

| Copied pair relative to original poll SP | Required saved FP | Required saved LR |
| --- | --- | --- |
| `+0`, 16 bytes | `SP + 64` | compositor bias + `0x1727a4` |
| `+64`, 16 bytes | `SP + 160` | compositor bias + `0x24fc54` |

This distinguishes the IPC header wait from the accept thread and unrelated
socket polling. The originating dispatch frame is `SP + 160`. At the later
import begin, copy its actual saved FP/LR pair in a further **16-byte scope**:
the saved FP must be `BeginFP + 320`, equal to that dispatch origin, and saved
LR must be compositor bias + `0x24fee0`. That is the original dispatch's virtual
handler-call return. Comparing retained frame addresses alone would not prove
that their native caller frames remain active. These link copies likewise grant
no future read capability or socket lifetime.

Implement a dedicated compositor gate and pinned preparation. Do not repurpose
the receiver's role/module profile or treat the current client cold-thread
marker as compositor ownership. Stack helper identities, initial thread claim,
current-thread marker, callback recursion/loss, provider binding before native
startup and the exact non-render bootstrap route must be reviewed in the final
artifact. No preparation helper should run for the first time inside Image,
Bind or View. If those obligations cannot be established, record refusal before
private reads and leave native execution unchanged.

A one-thread/one-bootstrap candidate would therefore have a combined private
copy ceiling of **741 bytes**: 693 for the raw attempt, 32 for the poll links and
16 for the genuine import-parent link. Claim the single process-wide candidate
before any bootstrap copy. Source-shaped rejected candidates, partial reader
failures and ownership postcheck losses still consume the same acquisition
budget; a refusal cannot reset that claim for another thread or candidate.
Additional threads, retries or constructor/heap reads would increase the scope
and require a new explicit budget. Constructor init/destroy and close
observations are useful later for real lifecycle proof; they are not hidden
prerequisites granted by this minimal raw plan.

## Implement and review before another hardware run

1. Finish a separate raw-only compositor state machine and source-specific copy
   adapter with zero-read admission. Exercise failures, all partial-write
   partitions, native unwind, recursion/loss and all budget boundaries.
2. Prepare the new compositor loader/provider bootstrap, cold-thread gate,
   immutable callback registration and owned journal bridge. Execute the actual
   AArch64 artifact against the retained providers; inspect symbols, TLS, unwind,
   routing, cancellation and complete module/envelope identities independently.
3. Publish only immutable raw attempts to the existing background owner. Resolve
   tagged resource generations after journal draining and validate currentness
   again before publication. A valid CPU receipt does not pin future resource or
   GPU lifetime. Keep the fixed journal capacities, bounded 1-MiB output and
   recording deadline; neither capacity exhaustion nor expiry may affect native
   rendering or restart the headset.
4. Produce a concrete user-directory installation/recovery manifest, bounded
   raw limits, appropriate actual service target and exact rollback checks.
   Obtain the separate private-read/loading scope and coordinated restart
   readiness only when that complete artifact is reviewable. No timed rollback
   or native/system file overwrite is part of the plan.

After a successful local compositor result, a **contemporaneous** client/server
capture still needs supported descriptor/object lifecycle evidence and an
independent kernel endpoint relation tied to the actual live transaction. Equal
tokens, PIDs, counters or a historical peer snapshot cannot supply that proof.
Only then can it bridge the receiver's selected token/content generation to the
compositor resource. Exact PC source-frame/eye association, coherent uniforms,
native reprojection/foveation and mask GPU lifetime remain later requirements.
None of these steps modifies the native PC Steam sender.

## Offline review evidence

This review checked 40 exact current instruction words, the import-handler
relocation and two actual libc poll exports. It independently reviewed the
current sparse helper's offsets, zeroed holes, failure accounting and all eight
boundary budgets. No blocking helper defect was found. The root's focused
normal and ASan/UBSan sparse/full-decoder tests reported 62,790 controls each;
this worker did not repeat that unchanged suite or claim a complete local gate.

The small private validator and receipt remain in ignored `build/`; the generated
42-MiB complete disassembly was removed. Proprietary instructions, binaries,
packet contents and runtime addresses are not copied into this repository.
