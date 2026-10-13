# Next bounded resource transaction diagnostic

Status, 2026-10-13: the separately approved
[actual raw capture](CLIENT-RESOURCE-RAW-CAPTURE-20261013.md) completed and was
fully rolled back. It confirms one matching local request/reply/publication
timeline. The proposal below records the reviewed scope and proof limits;
it is not a pending hardware request or permission for a later run.

## Decision after the cold-thread hardware run

The next useful step is **one raw client import transaction on the already
prepared decoder thread**, rather than broader thread discovery or a renderer
hook. Keep native rendering unchanged and do not modify the PC Steam sender.
The separately reviewed candidate is now implemented offline under
`FRAME_CLIENT_RESOURCE_RAW_TRANSACTION`, requiring cold preparation, census and
connection scopes. Its explicit marker is `FCRAW001`, schema 5, with the existing
11,800-byte envelope extent. These identify a separate mode, not an extension
of the old zero-private-read permission. This document is an implementation and
approval proposal, not authorization to perform additional live private reads.

The integrator reports the completed cold-preparation capture at source
`8aa8535`: all six resource-boundary observations had current-thread stack
ownership, followed by a terminal `WrongThread` refusal (`Reason 9`). A separate
contemporaneous peer snapshot used four kernel metadata queries and scanned 405
candidate descriptors. These facts have different meanings:

| Hardware fact | What it permits us to conclude |
| --- | --- |
| Six owned Begin/Pending/Reference observations | The cold preparation can precede one complete register-only client import/reference timeline on its actual executing thread. |
| Subsequent `WrongThread` retirement | Another invocation reached the first-wait route without this prepared thread's ownership. The diagnostic stopped safely; later private acquisition must remain unavailable. |
| Exact current peer pair from the kernel snapshot | Those authenticated process/descriptor/inode/cookie endpoints were paired at the snapshot. It does not identify an earlier captured descriptor epoch or certify uninterrupted ownership. |

The report is supplied by the integrator; this planning worker has not read the
private hardware capture or accessed the Frame. The existing
[integration plan](CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md) and
[carrier audit](FRAME-RESOURCE-CARRIER-20261012.md) remain the source-route
references. This proposal specifies the smaller executable next scope and its
remaining gates rather than repeating those audits.

## Use the initial prepared phase without weakening retirement

The initial phase is sufficient for a bounded attempt. Arm only at the existing
admitted `la_preinit`, prepare the actual current pthread once at receiver return
`0x328d0`, and claim the first exact ImportDmabuf Begin on that prepared thread.
Do not register whichever thread later produces a convenient import.

`Gate::current(marker)` and `region_current(marker)` must pass before **every**
private stack or wire copy. Require the exact born pipe, pinned module/provider
lifetimes, true entry SP/FP/return, active native invocation and relevant lock
ownership. Another thread never reads or mutates the claimed scratch state.
Do not borrow a `pthread_t`, infer usable bounds from SP or rearm after refusal.

At `WrongThread`, unload, observer loss or any other retirement, stop all new
acquisition. Already copied observations remain historical evidence, with an
explicit cutoff and all connection/namespace/frame/GPU flags false. A completed
raw transaction copied before the cutoff can still be interpreted as a local
native call sequence; it is not converted into a live lease. An incomplete
transaction is abandoned and never resumed on a replacement thread. If a
parallel refusal races acquisition, preserve copied bytes as inconclusive and
do not announce successful raw completion. The adapter must prove in offline
controls that retirement cannot cause a borrowed stack pointer to escape.

There is no hardware evidence that a different future run will give the same
initial window. If the first transaction cannot finish before retirement, keep
that result and investigate the additional first-wait caller offline. Do not
broaden registration, suppress `WrongThread` or repeat a live run merely to
select the most convenient trace.

## Exact proposed read scope

Acquire one transaction through existing true-entry Before/After forwarding.
Decode owned scratch with `frame_client_native::decode`. Every address below
is relative to the actual original entry FP; addition and usable-stack bounds
must be checked before copying. Saved FP/LR values verify fixed arithmetic and
must never be used for general stack walking.

| Boundary | Sparse scalar slots | Copied scalar bytes |
| --- | --- | ---: |
| Begin | Import `+0:16`, `+16:8`, `+136:8` | 32 |
| Write | Write `+0:16`, `+32:4`, `+40:4`; send `+0:16`, `+24:8`, `+64:8`; import `+0:16`, `+16:8`, `+136:8` | 88 |
| Header read | Reader `+0:16`, `+16:8`; waiter `+0:16`, `+116:4`; import `+0:16`, `+16:8`, `+136:8` | 76 |
| Payload read | Header-read slots plus waiter `+156:4` | 80 |
| Pending | Begin slots plus import `+224:4`, `+232:8` | 44 |
| Reference | Reference `+0:16`, `+16:8`, `+104:8`, `+224:8` | 40 |

Here `+offset:length` counts bytes, including saved FP/LR slots. Send frame is
write FP `+80+round_up(request_total,16)`; its import frame is send `+80`.
Waiter frame is read FP `+96`; its import frame is waiter `+192`. Scratch
extents remain 144/816/432/432/240/232 bytes respectively; unobserved scratch
holes are zero. Those extents are not permission to copy complete frames.

The acquisition ordering is mandatory:

1. **Begin After:** copy 32 scalar bytes only after original lock result zero,
   while native lock ownership exists. Preserve the exact import/receiver/pipe/
   record tuple. Sequence remains unavailable until the actual outgoing packet.
2. **Write Before:** copy and validate the 88 scalar bytes under that active
   native lock. **Write After:** require the matching original entry and a
   successful result within its remaining span; copy only the newly successful
   request-header overlap. Count the remaining request bytes without reading
   them. The request header is exactly 12 bytes, opcode `0x234`.
3. **Read Before:** copy and validate 76 or 80 scalar bytes on the same executing
   thread, exact invocation and FD. **Read After:** copy only its successful
   header/payload prefix from the still-active native buffer. A payload buffer
   can be native heap storage: its source-proven live call span, not the stack
   bounds alone, must authorize that nine-byte copy. Retain no borrowed pointer.
4. **Pending Before:** copy 44 bytes before original unlock, while the lock is
   still held. After original unlock returns, use only that owned copy and the
   original result. Require native wait success and parsed presence/full token
   equal to the exact matching reply. This is pending output publication.
5. **Reference After:** copy 40 bytes only after its genuine successful lock;
   verify the same receiver parent/record, reference frame relation, live token
   and saved token. This observes the following reference invocation after
   output publication, not reference-RPC success.

Before copies of transport geometry are consumed only by the matching After.
Cancellation, no After, changed entry, failed transfer, interrupted prefix,
extra/mismatched reply, parsed-token contamination or loss produces an explicit
inconclusive record. Never use a later matching reply to repair contamination.

Proposed immutable limits for this single-transaction mode:

- One claim; at most 24 native read/write calls combined, with at most eight
  Write, eight HeaderRead and eight PayloadRead calls; no retry after failure.
- Request total at most 512 bytes, counted without copying request payload.
- At most 12 request-header + 12 reply-header + nine reply-payload bytes copied:
  **33 wire-metadata bytes**. No FD-plane/protobuf request payload or pixels.
- At most 2,068 additional private scalar bytes cumulatively. The declared
  sparse sets require at most `8×88 + 8×76 + 8×80 + 32 + 44 + 40 = 2,068`
  for the 24 transfers plus Begin/Pending/Reference;
  full native scratch windows must never be read to fill that allowance.
- Existing 32-record, 64-KiB, 120-second diagnostic bounds stay in force.
  Record capacity includes startup evidence. Exhaustion makes the transaction
  incomplete; the proposal does not silently raise the capacity or byte limit.
- No extra pipe-field, object/heap scan, stack discovery, decoded image,
  descriptor, pixel, shader or renderer reads. No new kernel queries are needed
  for this client-only attempt. No timer-triggered restart or rollback.

Before scratch is bounded owner-local state, not an additional unbounded queue.
Aggregate at most 24 owned transport observations into one closed result,
retaining only the current call pair and required header/scalar facts. Individually enqueuing every transfer alongside existing
startup evidence can exceed the unchanged 32-record cap; aggregate rather than
silently raising that cap. The output worker may interpret immutable copies,
never revisit native addresses. The combined additional private read maximum
is **2,101 bytes**: 2,068 scalar bytes plus 33 wire-metadata bytes.

Copying only the three mutex-boundary frames would require 116 scalar bytes,
but would leave the known native parser-contamination issue unresolved. The
small request/reply header and token capture is the minimum additional evidence
needed to distinguish an actual matching reply from stale parsed state; a
second stack-only hardware round would not close that question.

## Baseline gaps and implemented repairs

The route decoder and general forwarding implementation already existed, but
the tested cold-thread artifact at `8aa8535` did **not** expose the required
transport scope. That baseline audit preparation bound Init/Shutdown, four mutex entries,
Connect and receiver EpollWait. It had no client Read/Write audit functions; the connection assembly also omitted
their veneers. The temporary cold-thread binary was fully removed afterward.
The [new raw candidate and final evidence](CLIENT-RESOURCE-RAW-PREPARATION.md)
implement the repairs below; it remains uninstalled.

Finish the separate explicit raw-transaction mode, marker, envelope schema and
session acknowledgement. Extend hidden client-to-actual-libc Read/Write binding
and its immutable provider cache; receiver-to-libc cold EpollWait stays separate.
Authenticate the actual pinned public and distinct forwarding `read`/`__read`
and `write`/`__write` exports offline and again through actual mapped provider
geometry in sealed MAIN. Do not guess a read RVA from a neighboring symbol.
Unrelated Steam/UI/native libc callers must keep their original provider PCs.
Use the existing entry/CFI macro, signed ssize_t result/errno preservation and
cancellation-aware aliases; never add public interposers, raw syscalls or hot
`dlsym`/attribute queries. Freeze provider selection before sampling threads.

Keep the raw record type, queue and interpreter separate from the strict
`client_carrier_core`. Its `Observer::begin` marks `InvalidRoute` when lifetime
proof is absent, but returns that a scope was claimed; that boolean does not
mean acceptance. Moreover, `Observer::publish` is a queue primitive and does not
validate routes. **Do not call either method with raw diagnostic records.**
Prove the separation and retain the strict contract unchanged, rather than
setting `continuous_connection_owned` or `immutable_lifetime_lease` to invented
values to reuse its queue.

Focused controls required before asking for the hardware scope:

- Each sparse fixture yields the same decoder result as the owned full fixture;
  exact scalar-byte and cumulative budgets are checked at every copy boundary.
- Wrong source/provider/module/ObjectToken, FP/LR, pipe, receiver record, FD,
  offset, sequence, token and successful-prefix geometry refuse before reads.
- Fresh/reused thread markers, parallel foreign waits, callback recursion,
  retirement during each phase, unload/fork, no After and queue loss never
  permit cross-thread adoption, continued copying or false completion.
- Original arguments/results/errno/native output and cancellation forced
  unwind pass through the actual final ARM ELF. Old modes remain unchanged;
  true hidden entry set, export/TLS/CFI and actual audit binding are checked.
- Partial reads/writes, zero/error results, duplicate/mismatched/malformed
  replies, missing reference and unsupported request sizes stay inconclusive.
- False lifetime/namespace/frame/GPU fields remain false in every raw output;
  raw records cannot enter strict publication. No payload/image bytes or native
  pointers reach a deferred reader. Normal native execution remains untouched
  when the new scope is disabled, invalid, refused, canceled or exhausted.

## What this advances, and what it still cannot prove

A successful raw result would establish the actual outgoing request sequence,
matching wire reply, full parsed token and following receiver reference from
one source-checked client invocation. That closes the client half of a future
resource bridge; it answers whether native parser contamination or a different
runtime route invalidates the static audit. It does **not** establish that a
compositor registry allocation belongs to that token namespace.

The later peer snapshot cannot retroactively bind this earlier transaction to
its socket epoch. Even a numerically equal sequence/token in a compositor
capture is insufficient. The next cross-endpoint stage needs actual compositor
request/import/reply evidence and an endpoint relation tied to the relevant
descriptor lifetime, with replacement/duplication/unload/loss invalidation
coverage. Client mutex ownership and cached peer PID do not supply that coverage.
Resource generation/retirement, selected source/video frame, eye/extent and
native UV/reprojection/foveation/GPU consumption remain further independent
gates. The current candidate makes no native PC Steam sender changes. The remaining
source-frame adapters still require their own feasibility and association proof.

## Deployment and manual recovery boundary

Offline packaging now has a distinct `--resource-raw-transaction` opt-in which
requires all three preceding connection/census/thread options. It checks the
single hidden read-only raw marker, ten hidden entry veneers, sole public bridge
and actual pinned public/alias provider pairs: read `0xdf330`, write `0xdfee0`,
each 204 bytes at `GLIBC_2.17`. Alias and public names resolve to the same native
PC; they are distinct names, not different provider addresses. The generated
216-byte audit preparation appends these two RVAs after the cold-thread tail.

The session mode is `resource-raw`. Stage/start/activate require
`--ack-resource-raw-transaction-scope` alongside the four preceding scope
acknowledgements; activation additionally requires coordinated restart readiness.
The owned manifest records this fifth scope explicitly. Old modes reject a raw
ELF or transport routing even when its plan is downgraded, and the raw mode
refuses altered byte/call limits, provider RVAs, scope markers or proof flags.
These packaging controls do not establish that a raw runtime trace is complete
or authorize installation on the Frame.

After implementation and focused review, generate fresh exact artifact hashes,
sizes and native/provider pins, plus the reviewed mode and limits above. Obtain
explicit approval for the additional private scalar and 33 wire-metadata bytes;
the completed register-only/cold-thread approvals do not cover them. Stage only
the owned user-directory inventory and exact temporary Steam user-service
drop-in. Document recovery before activation and coordinate the initial restart.

After the user confirms normal Frame UI and the actual PC-VR stream, collect
the bounded owned-copy output. Save the complete footer, source/budget receipts
and refusal/cutoff outcome. Request readiness before manual removal/restart;
there is no automatic restart at deadline. Verify original service/environment,
native hashes and absence of diagnostic mappings, archive compact receipts,
then remove only the exact own input/session inventories. Tell the user standby
is safe only after that verification. No current-run installation or additional
live private acquisition is authorized by this proposal.
