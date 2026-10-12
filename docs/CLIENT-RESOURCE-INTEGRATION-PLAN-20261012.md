# Next native client resource integration

## Decision

The successful [connection capture](CLIENT-CONNECT-CAPTURE-20261012.md)
establishes one real native connect invocation, not a continuously owned socket
or a resource namespace. The next executable step should be a **register-only
resource-boundary census**, followed by a separately scoped **raw transaction
capture**. Both can reuse the existing true-entry forwarding, module/provider
authentication, startup ObjectToken, bounded queue and native client decoder.
Neither step should admit the strict resource carrier prematurely.

Do not build a second lifecycle registry or relax
`frame_client_carrier::Route::continuous_connection_owned` to get positive
records. The current `client_carrier_core` has the correct stronger contract;
its refusal is useful evidence that the remaining lifecycle gate is enforced.
The immediate diagnostic output is copied observations, not a carrier lease.

This plan reviewed the current sources at `0c632fd`, retained native-input hashes
and the successful capture documentation. An independent native-byte check
passed **103 hash, executable-range, instruction and address-arithmetic
checks** for the actual retained client/receiver. It performed no Frame access,
live memory read, native execution, binary modification or rendering change.
Native bytes and any disassembly remain private. Source-established paths are
described in [the carrier audit](FRAME-RESOURCE-CARRIER-20261012.md).

## Exact native object provenance

The existing born static pipe **is** the pipe used by ImportDmabuf and
RefResource. Derive its address from the actual global initializer, rather than
mistaking the compositor subobject address for the HMD singleton:

| Source-pinned object | Native arithmetic | Client-relative address |
| --- | --- | --- |
| HMD singleton | Global initializer passes page `0x647000` + `0xd78` to HMD constructor `0x1b9438`. | `0x647d78` |
| Embedded CVRCompositorClient | HMD + `0x1158` | `0x648ed0` |
| Embedded resource manager | Compositor base + `0x10a8`, equal HMD + `0x2200` | `0x649f78` |
| Resource RPC pipe | Compositor base + `0x1120`, equal HMD + `0x2278` | **`0x649ff0`** |

The HMD calls manager constructor `0x361d18` at `0x1b9518`, supplying that exact
pipe in x1; the constructor stores it at manager +8. It constructs this pipe
through `0x244da8` at `0x1b9578`, giving the saved parent return `0x1b957c`
already authenticated by `frame_native_client_birth::Reader`. Import at
`0x3623ac` loads manager +8 into x21, and Reference at `0x362d40` loads manager
+8 into x19. The embedded compositor's connect path also uses base +`0x1120`.
The receiver's internal secondary interface at HMD +24 reaches the same manager
via getter `0x1c31a8`, adding `0x21e8`.

No new ObjectToken or birth registry is needed. `BirthKind::GenericClient`
names the existing non-accepted birth category; it does not mean an unrelated
pipe or certify the resource connection namespace. Keep the full absolute
target and current native code/audit pins, not just an address page offset.

The resulting exact register expectations at the genuine public entries are:

| Boundary | True-entry scalar expectations |
| --- | --- |
| Begin, LR `0x3623c0` | x0 = x21 = client bias +`0x649ff0`; x20 is the native output pointer (receiver record +32); original SP = FP, Import frame size 352. Sequence is not yet assigned. |
| Pending, LR `0x3624c4` | x0 = the same resource pipe loaded from Import frame +136; x20 remains the output pointer; x24 is the normalized native wait result 0 or 1; original SP = the same Import FP. x21 is **not** required to remain the pipe because native deferred logging may change it. |
| Reference, LR `0x362d58` | x0 = x19 = the same resource pipe; x24 is the actual full token argument; x20 = client bias +`0x649f78`; original SP = FP, Reference frame size 240. |

If these methods return to the same receiver frame, Reference FP equals Import
FP +112. That arithmetic alone is only a register-route candidate: their saved
receiver return links/record still require the separately scoped stack copies.
The census must report candidates and original outcomes without promoting them
to native publication/transaction proof. Static source does not identify the
actual first import thread; record whether the already prepared startup stack's
owner marker is current, without discovering or reading another thread's stack.

## First integration: resource-boundary census

Keep the repaired hidden-only client-to-libc audit routing. Existing mutex
lock/unlock veneers already receive the true public entry snapshot and native
outcome, so this step requires **no new provider and no private memory read**.
Add a distinct opt-in sampler in `receiver/client_startup_runtime.cpp` for:

| Entry boundary | Authenticated client return RVA | Recorded fact |
| --- | --- | --- |
| Import begin | `0x3623c0` | Original lock argument and real lock result; live register-only pipe/output/frame candidates. |
| Import pending | `0x3624c4` | Original unlock argument/result and the entry's native wait-result register, without interpreting unobserved response fields. |
| Following reference | `0x362d58` | Original lock argument/result and the actual entry's token register; not proof that import publication or reference success occurred. |

Require the original mutex argument to equal the fully authenticated born
static client pipe, current client mapping/audit lifetime, valid original
SP/FP geometry, no notification/queue loss and authentic Before/After pairing.
Record the thread identity as an observation label, never a connection or video
generation. A plausible reference token and return site cannot certify its
saved receiver parent or record without the later private stack scope.

The existing connection sampler stops after its matched connect/unlock.
Its `connection_sample_completed` early return must **not** stop this new
resource sampler: imports occur after connection startup. Keep both samplers'
owner markers and pending states independent. Preserve the 18-byte TLS baseline
unless a deliberate small increase is independently reviewed; keep large
sampler storage process-lifetime and uniquely owner-claimed. Another thread
must never mutate a claimed scope, and thread-address reuse must not adopt it.

This census establishes whether the expected direct import route executes on
the already prepared startup thread or another thread. If it observes another
thread, record only scalar entry facts. Do not treat the startup thread's
usable stack bounds as that caller's bounds or discover a stack with
`pthread_getattr_np`, `/proc`, allocation or symbol lookup in a repeated hot
callback. A separately reviewed cold registration point is then required.

Use a new diagnostic schema/plan scope identifying these three sites and the
zero additional read budget. Keep a fixed record/byte/deadline cap, explicit
end reason and pending-abandonment indication. Do not enable by default, mutate
native state, restart on a timer or claim that a capped/incomplete census
established the absence of other routes.

## Second integration: raw transaction acquisition

Once the actual calling thread and its prepared stack ownership are known,
reuse `frame_client_native::decode` rather than writing another frame walker.
The existing decoder accepts an **owned copy**; it does not grant permission to
dereference any native pointer. The acquisition adapter must first establish
the exact pinned source route, current native module lifetime, same-thread
usable stack bounds and the relevant successfully acquired native lock.

Create a small zero-initialized scratch envelope containing only the decoder's
required scalar slots. Copy those slots at the genuine active boundary; leave
unread holes zero. Do not memcpy a whole native frame merely because the
decoder's maximum input window is 1,552 bytes. Parent frame addresses follow
fixed source arithmetic; saved FP values only verify those exact links and
must never become a general pointer-chasing backtrace.

The decoder's present minimum stack read sets are:

| Boundary | Exact scalar bytes read | Maximum scratch extent from native entry FP |
| --- | ---: | ---: |
| Begin | 32 | 144 |
| Write | 88 | 816, for the 512-byte native request cap |
| Header read | 76 | 432 |
| Payload read | 80 | 432 |
| Pending | 44 | 240 |
| Reference | 40 | 232 |

These byte counts sum distinct required slots, including saved FP/LR links.
They exclude separate wire copies and the already approved five startup words.
Write scratch extent is `304 + round_up(native_total, 16)`; the other extents
are fixed for this decoder. Sparse envelopes must be tested against the full
owned fixture to ensure no required slot was omitted or silently zeroed.

Acquisition/order requirements:

1. At Begin, read the 32-byte scalar set **only after real lock result 0**.
   Preserve the active invocation tuple `(import FP, receiver FP, pipe,
   receiver record)` returned by the decoder. Do not read or infer `w25` as a
   sequence: native code assigns the sequence after this boundary. No peer
   credential is required for this provisional diagnostic begin.
2. Add hidden-only `read` and `write` veneers by reusing
   `import/native_import_entry.S` and existing `import/import_forwarding.cpp`.
   Authenticate their exact native libc provider PCs outside callbacks. The
   actual provider's distinct versioned `__read` and `__write` exports permit
   unchanged cancellation-aware forwarding; no raw syscalls, public exports or
   hot `dlsym` are needed. Audit redirect only the authenticated client binding.
3. At each original write After, decode the exact active request/send/import
   chain. Require native result `0 < n <= remaining`, stable original buffer
   identity and an uninterrupted successful prefix. Copy at most the portion
   of those successful bytes overlapping the **12-byte request header**.
   Count the rest of the native request bytes; never copy plane descriptors,
   protobuf request payload or image contents. Header opcode must be `0x234`,
   length must equal total minus 12, and its real wire sequence is authoritative.
4. At each original read After, require the exact waiter/import chain, original
   FD, request sequence and buffer/offset geometry. Copy only the successful
   **12-byte reply header and nine-byte reply payload**, capped across partial
   reads. The payload helper's active native-owned temporary is not a saved
   heap pointer for background inspection. Copy it before native resumes and
   retain no borrowed pointer. Header must be opcode `0x235`, exact request
   sequence and length 9; payload must be fixed64 tag `0x09` plus nonzero token.
5. Copy the Pending scalar set **before forwarding the final native unlock**,
   while its native lock is still owned. After unlock returns, consume only
   those copied facts plus the original unlock result. Require native wait
   success, presence and parsed full token equal the exact observed reply.
   This boundary precedes the native output store: name it pending publication.
6. At the subsequent successful RefResource lock, decode the exact new frame,
   saved receiver caller/record and saved/live token. Require the same receiver
   invocation and output pointer-minus-32 relationship. This establishes the
   receiver invoked its following reference after publication, not that the
   reference RPC succeeded, a video frame was selected or GPU consumed it.

The nine-byte payload and 12-byte header are protocol metadata, not pixels.
Request payload is bounded by the existing 512-byte observation cap and
counted only. A larger actual request is an explicit unsupported diagnostic
route, not evidence that native itself is invalid.

The native waiter parses before checking sequence/opcode and ignores the parser
result. Never infer a valid reply from a later parsed token or wait success.
Repeated reads after a mismatched header, malformed payload, duplicates,
missing spans, buffer changes or queue loss make the transaction inconclusive.
Do not hide contamination by attaching the last convenient matching reply.
A closed-trace interpreter may describe an exact copied match only when all
observed route/span checks pass; it still cannot create a lifecycle lease.

Use raw diagnostic records carrying explicit
`continuous_connection_owned=false` and unavailable connection generation.
Do not call strict `Observer::begin` with invented flags or use its
`publish(Record)` to advertise these as admitted records. Keep the strict core
unchanged; prove by negative tests that the raw evidence cannot enter it.

## Why native lock ownership is insufficient for a strict carrier

The native mutex owns the supported pipe fields and its per-call operations.
It does **not** prevent another thread or linked module from closing, replacing,
duplicating or shutting down the same process FD without taking that mutex.
Client-only audit callbacks therefore cannot truthfully establish process-wide
descriptor invalidation coverage. An actual successful `connect` and the same
FD value at a later import are separate historical facts, not uninterrupted
ownership.

`Registry::connected()` marks a local socket lifetime as observed; it must stay
unused until an actual complete lifetime adapter establishes the obligation.
Neither an observation ordinal nor the born ObjectToken supplies a connection
generation. More client `close` callbacks can conservatively invalidate known
evidence but do not certify coverage of `dup*`, duplication-capable `fcntl`,
direct system calls, external replacements or fork/inherited FDs. Keep those
unknown routes unavailable rather than globally intercepting Steam/UI libc.

The client's cached peer PID at pipe +168 can survive reconnect or failed
credential lookup. It is not fresh `SO_PEERCRED`, UID/GID or a unique endpoint.
The first native credential lookup may occur after import begin; a future
complete adapter must begin provisionally and bind authentic same-lifetime
credentials before commit. This raw diagnostic needs neither the cached PID
nor pipe +172 and should add **zero private pipe-field bytes**.

A cold, separately scoped kernel snapshot using
`tools/inspect_unix_connection.py` can establish the exact contemporaneous
process-start/inode/cookie peer pair. It cannot retroactively lease the old
connect capture or fill an unobserved descriptor-lifetime gap. Equal sequence,
opcode or token on two endpoints is insufficient without that pair and lifetime.

## Concrete implementation ownership and checks

For an integration using independent worktrees, assign disjoint ownership:

| Area | Existing files to extend | Focused checks |
| --- | --- | --- |
| Register census and raw acquisition | `receiver/client_startup_runtime.*`, root diagnostic writer | Real Before/After results; target/module/owner rejection; no reads without prepared ownership; connection completion does not suppress imports; bounded records and terminal partial states. |
| Hidden transport routing | `import/native_import_entry.S`, optional entry selector, `import/import_startup_audit.*`, `tools/prepare_client_startup_observer.py` | Only exact client-to-libc binding redirected; all unrelated public libc PCs remain native; old broad plans refused; immutable provider/errno/result/cancellation/CFI retained; legacy entry modes unchanged. |
| Acquisition fixtures | new focused test and CMake test registration | Sparse windows equal existing decoder fixture; exact read budgets; partial successful transfer boundaries; malformed/contaminated replies; pending before unlock; reference frame/record/token mismatch; no admission when lifetime flags false. |
| Offline interpretation and recovery | explicit diagnostic schema/interpreter plus `tools/client_startup_session.py` | Genuine observation ordering and byte caps; no namespace/video/GPU claims; exact inventory/native hashes; rejection before staging for wrong scope; complete manual rollback. |

The register-only step should finish first and needs only the first and last
areas with existing transport unchanged. Keep new worker baseline preparation
private to each worktree. Review the actual ARM ELF, export table, PT_TLS,
audit redirect set and exact preparer output, not only host fixtures. Reuse
already passing unchanged evidence; do not repeat the full gate after a bounded
test repair. Remove generated build/disassembly caches after stopped processes,
preserving small reviewed receipts and exact current artifacts.

Required negative controls include the previously failed public-export
artifact, wrong native import site, another Steam/UI process, another pipe,
thread reuse/overlap, absent stack authorization, native cancellation without
After, failed lock/unlock/read/write, signal notification recursion, module
retirement, missing/extra/partial reply, duplicate exact transaction key,
parsed-token mismatch and missing reference. Cancellation must pass through
unchanged and leave only abandoned/inconclusive diagnostic state.

## Minimum next hardware scope and remaining gates

The next reviewed artifact can initially request only the three register-only
boundary observations, actual native outcomes and copied module/ObjectToken
labels. Its scope adds **zero private stack, pipe, wire, packet, image or shader
bytes** and **zero forwarding providers**. Retain the existing bounded
32-record/64-KiB/120-second diagnostic policy unless a concretely reviewed
reason changes it. Recording deadline does not restart Steam.

The full raw transaction follow-up separately requests the sparse scalar read
sets above and at most 12 successful request-header plus 12 reply-header plus
nine reply-payload bytes per admitted diagnostic transaction, with native
request payload counted only. It also lists the two hidden read/write provider
routes and exact per-thread cold stack-registration scope. No private-field
read, general memory scan, live debugger dump, renderer hook or PC sender
modification is needed for this step. Kernel endpoint diagnostics, if desired,
are a separately bounded cold read scope, not a callback action.

Any deployment must use fresh native/provider/service pins, exact uploaded
inventory, explicit stage-scope acknowledgement, coordinated initial Steam
restart and documented manual recovery. The successful connection scope does
not authorize private transaction acquisition. Do not autonomously reinstall
while the maintainer has put the Frame in standby. Steam systemd/PID activity
alone does not establish a usable Frame UI; obtain normal interface and actual
PC-VR stream confirmation before treating a run as successful.

All these diagnostic stages keep native rendering unchanged. The following
remain false: uninterrupted connection ownership, cross-endpoint resource
namespace, resource generation/retirement, selected source session/video frame,
eye/extent and native UV/reprojection/foveation mapping, accepted compositor
submit and coherent GPU consumption. No resource token, reference call or
kernel peer snapshot enables the explicit-mask shader or working passthrough.
