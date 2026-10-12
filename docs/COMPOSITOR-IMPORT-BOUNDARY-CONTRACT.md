# Compositor import observation: concrete native boundary contract

## Scope and result

For the retained SteamOS 0.4.5 compositor, a bounded **successful import reply
to local Vulkan image candidate** can be observed through public mutex,
`write`, and Vulkan entry points. Hooks for `malloc` or `memcpy`, a private
executable detour, arbitrary heap inspection and generic unwinding are
unnecessary. Connection credentials/lifetime need additional public lifecycle
observations; they cannot be fabricated from the pipe pointer or FD number.

The proposed route still needs an implemented and independently reviewed
assembly entry adapter, a current-provider export check, fixture tests and a
separately authorized compositor capture. This document is an offline contract,
not a claim that an observer has already run on the Frame. It does not authorize
any installation or broaden the existing receiver permission.

The exact compositor is 6,026,096 bytes, SHA-256
`0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96`,
ELF build ID `f608305627cd1579a5bbd714d4209922c892da0a`. All addresses below
are its ELF virtual addresses, relative to a separately validated load bias.
The companion receiver has SHA-256
`31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153`.
The broader import audit is in
[FRAME-IMPORT-UPDATE-20261012.md](FRAME-IMPORT-UPDATE-20261012.md).

## Correct the initial-lock interpretation

The import dispatch function at `0x153f70` allocates a 320-byte frame, sets
`FP = SP`, stores the request sequence at frame `+108`, and saves its pipe
pointer at `+128`. Its public lock has return address `0x153fc0`.
However, the public unlock with return address **`0x1540b0` releases this lock
before** the method call at `0x154190`. Import creation and registration are
therefore not protected by that pipe mutex for their whole duration.

The dispatch frame's `+128/+136` slots are later overwritten with a retained
typed client-context pair. Do not reread them as the original pipe. Copy the
pipe identity at the genuine begin boundary and compare it against native
retained registers and the later send argument. The client references remain
live during the method and nested creation, but are released beginning at
`0x154194`, before response serialization. Never enqueue a private client
pointer for later dereferencing.

Native code ignores the public mutex return values. The observer must preserve
those results unchanged and refuse its own candidate after any nonzero lock or
unlock result. It must not make native locking behavior depend on diagnostic
success.

## Entry ABI and safe active windows

Capture native `SP`, `x29` and `x30` at the **actual public function entry**,
before the observer's prologue or compiler-generated helper calls. Copy native
`x0..x8` and `x19..x30` into fixed observer storage: `x19..x25` hold several
required native scalars, while `x8` remains an argument/result ABI register.
Leave the platform register and the ABI's callee-saved vector state intact.
Preserve arguments, return values, cancellation behavior and `errno` across
observer callbacks and original provider calls. Do not synthesize a native
caller context from a C++ callback's own frame.

A loader trampoline which changes the preserved caller context is not an
alternative route: refuse it unless its exact ABI is independently established.
Direct return-address equality alone never establishes the whole route.

The layouts below permit a narrower active-window proof instead of scanning
thread stacks. Every next frame address is **computed from checked allocation
constants**. Within the current source-proven live frame, compare its saved FP
against that computed address and its saved LR against the pinned return PC;
only then inspect that next window. Do not chase an arbitrary saved FP. Reject
misalignment, overflow, changed frame links, unknown intermediate frames,
signals/nesting and unsupported routes. This proof applies only to genuine
pinned native callers; it is not a portable OS mapping or unwind contract.

An implementation which cannot maintain that exact entry/window proof needs
pre-established per-thread stack bounds. `pthread_getattr_np` in a rendering
callback, a fresh `/proc` read, a generic backtrace or a start-time thread list
which omits subsequently created threads is unsuitable. Thread-start wrapping
would require its own reviewed forwarding/cancellation/startup contract and is
not necessary for the strict fixed-window variant below.

## Vulkan creation route and client scalar

Let `C` be the concrete Vulkan creation method's native FP at an actual
`vkCreateImage` entry whose LR is `0x2c75ac`. Its allocation is 752 bytes, with
FP at native `SP + 96`. The exact outer windows are:

| Window | Computed FP | Required saved LR |
| --- | --- | --- |
| Concrete creation, entry `0x2c71f0` | `C` | `0x70bc8` |
| Renderer wrapper, entry `0x70ad0` | `C + 656` | `0x13bbdc` or `0x13bc8c` |
| Import helper, entry `0x13bb80` | `C + 816` | `0x13beb0` |
| Native method, entry `0x13bd78` | `C + 912` | `0x154194` |
| Import dispatch, entry `0x153f70` | `C + 1136` | No guessed outer caller required |

For each preceding window, its saved FP must equal the next computed FP. The
complete source-proven live span from creation SP to dispatch-frame end is
1,552 bytes. The dispatch FP, request sequence at `+108`, retained pipe and
observer invocation ID must match the begin record. Both supported helper
branches use the same synchronous wrapper and scalar ownership.

There is an exact **no-private-heap-read** route to the client identifier. The
native method loads the actual identifier from its retained context's `+8`
before calling the helper. The helper copies it into `w24`; the renderer
wrapper leaves `x24` unchanged. The concrete method saves its caller's `x24`
at native SP `+152`, which is **`C + 56`**. Copy the low 32 bits from that live
save slot. This is the actual scalar passed to registration, not the process
PID or an observer-created namespace. No later private context access is needed.

The saved caller `x25` at `C + 64` also identifies the method's context-pair
address, if an adapter chooses to cross-check the pair's location. Such a
pointer remains an opaque local ownership identity; it is not a connection
epoch or cross-process namespace. Recording or dereferencing additional private
client fields is outside this minimal contract.

Copy public image-create metadata and its result at their own public call
boundary, use a real local image generation, and attach this invocation ID to
the existing bounded Vulkan resource journal. Creation is provisional until
the matching successful reply completes. Require the expected memory/view
events and preserve destroy events; `VK_SUCCESS` alone precedes native resource
registration and does not prove it succeeded.

## Reply phase without allocation/copy hooks

The outer send helper at `0x172e70` allocates 80 bytes, initially `FP = SP`,
and is called synchronously from dispatch with saved LR `0x1542b0`. Its public
lock has return PC `0x172ea0`. At that lock entry the native callee-saved
registers retain:

| Register | Native value |
| --- | --- |
| `x19` | Actual pipe pointer |
| `w20` | Serialized payload byte count |
| `w21` | Response opcode |
| `w22` | Original request sequence |
| `x23` | Serialized response payload, owned until send returns |

Only opcode **`0x235`**, payload length **9**, and the exact begin sequence are
eligible. Validate the outer saved FP as `sendFP + 80`, its saved LR as
`0x1542b0`, and dispatch sequence while that frame is still active. A native
method false result or response opcode zero bypasses this route; a failed
registration can still send an empty response. Do not treat either as success.

For the eligible nine-byte payload, the helper creates an exactly 21-byte
packet in an aligned **32-byte stack allocation below its FP**. This is not
the response's `malloc` buffer. The packet is:

| Offset | Exact encoding |
| --- | --- |
| `0` | Little-endian `u32` response opcode `0x235` |
| `4` | Little-endian `u32` original request sequence |
| `8` | Little-endian `u32` payload length `9` |
| `12` | Byte `0x09`: protobuf fixed64 field number 1 |
| `13` | Little-endian `u64` complete registered resource token |

Reject other lengths/tags, extra/unknown fields and ambiguous image candidates.
The native method sets the token's presence bit only after helper registration
success. Native namespace construction remains separate; copy the entire token.

## Public write, partial writes and final native outcome

The inner write helper at `0x172b98` has an 80-byte frame, `FP = SP`; its public
`write` return PC is `0x172c00`. For this exact 21-byte packet:

* `writeFP + 112 = sendFP`, because the outer helper has a 32-byte packet VLA.
* The inner saved LR must be `0x172f34`; the outer saved LR must be `0x1542b0`.
* `sendFP + 80 = dispatchFP`, matching the begin invocation.
* The source-proven packet base is `sendFP - 32`.
* Native `x22` is the pipe; `x23` is that packet base; `x19` is bytes already
  written; `w20` is total length 21. The public arguments must agree:
  `buffer = base + offset`, `count = 21 - offset`, and FD matches the protected
  native pipe FD. Reject an offset outside `0..20`.

Copy the fixed packet while those live ownership checks hold. Match each
positive result against exactly that requested suffix; advance only by its
actual result. A zero result, `-1`, oversized result, changed byte, changed
pointer, changed invocation, descriptor invalidation, overlap, observer loss or
unexpected nested call permanently rejects this attempt. Preserve the native
partial-write/error loop; do not add a retry, wait or syscall.

At the final outer unlock with return PC `0x172fbc`, the live **`w20` is the
native send outcome**, not the payload length. On this successful shape SP is
still `sendFP - 32`; saved parent FP/LR still identify dispatch. Require native
outcome 1, successful observer-visible mutex returns, all 21 bytes positively
written, exact invocation and unaffected connection lifetime. Then publish an
immutable successful-import candidate. The bytes were written to the native
channel; the receiver's acceptance and a submitted draw remain unproven.

Do not use the frame's saved old `x20` at `+24` as the current outcome. Capture
the live register before calling the original unlock and preserve it for the
post-forward check. Flush no output or perform I/O in this callback.

## Cleanup and lifecycle gates

Use fixed thread-local attempt state plus a fixed bounded output queue. Every
genuine import begin gets a new observer invocation ID and invalidates any
uncompleted same-thread predecessor; a later sequence/FP match is not enough
to revive stale TLS. At the first unlock `0x1540b0`, native `w25` holds the
payload-read result: false rejects immediately. A method failure, response
suppression or exception may have no final-send callback. Such an incomplete
scope must never be counted as a successful import or reused. A fresh begin,
an observed unwind/cleanup invalidation, or a bounded observation cutoff can
retire it diagnostically without modifying native execution.

Reject same-thread nested imports, conflicting socket writes and missing begin
events. No record is successful merely because an old TLS value remained.
The 32-record cap applies to completed protected attempts, not to unbounded
native side events; all tables and events also need independent fixed caps.

The initial read-phase mutex is released before creation. Native pipe identity
and FD equality alone therefore do not prove a continuous connection. Public
`getsockopt` at return PC `0x172960` supplies actual `SO_PEERCRED` for the
native request, while successful accepted sockets are assigned to their pipe
at `0x17254c`. The close path calls `shutdown`/`close` and writes FD `-1` at
`0x17241c`. Use separately observed connection creation and invalidation,
including FD reuse, before claiming a complete epoch. Unknown inherited
connections or missed lifecycle callbacks remain unresolved.

The send path holds the same recursive native pipe mutex through its nested
write loop, so ordinary native close operations cannot concurrently replace
the pipe FD while those successful locks are held. Direct external descriptor
operations, observer loss and reconnect still require invalidation; do not
assume the process cannot close/reuse an FD outside this function. A local
monotonic observer connection ID is only meaningful when backed by observed
native creation/retirement, not a fresh ID assigned on every write.

The current receiver resource-manager library is not among the retained inputs.
Its own carrier validation, lifetime and namespace mapping are still a blocker
to joining these candidates to the receiver's selected tokens. Equal numeric
tokens or peer PIDs are insufficient.

## Forwarding and evidence gates

The compositor imports `write@GLIBC_2.17`. A distinct link-time
`__write@GLIBC_2.17` forwarding alias is a useful candidate, but the exact
current Frame libc's **export has not been verified** by these retained inputs.
The retained libc hash is
`be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b`;
its bytes/export table are absent from this audit. Check the real provider and
symbol kind/version before deployment. A sysroot export, a recursive call to
interposed `write`, or hot-path `dlsym(RTLD_NEXT, ...)` is not proof.

Private `build/import-boundary-audit/{validate.py,validation.json}` checks
**97 exact instruction/branch points** and the derived frame/packet arithmetic.
It stores no native binary or disassembly in tracked files. This proves the
static layouts for these exact references, not an adapter's machine code,
socket lifecycle, native execution, headset behavior or frame association.

Before any hardware request, implement and independently inspect the actual
entry assembly and forwarding artifact; exercise real public-boundary fixtures
for every failure above, including two threads, nested entry, malformed frame
links, VLA shapes, partial writes, empty failure replies, cancellations,
missing lifecycle, descriptor reuse and exact provider results/errno. The
successful candidate must use the existing public resource generations and
report namespace/receiver acceptance/source-frame/UV/GPU joins as **false**
until separately established. Keep opaque rendering and all native pixels
unchanged throughout this diagnostic scope.
