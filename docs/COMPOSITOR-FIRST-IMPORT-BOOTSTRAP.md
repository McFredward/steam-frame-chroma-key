# Deterministic first-import thread bootstrap

The next compositor candidate uses `compositor_import_thread.*`, which selects
its worker at the first genuine import-specific public lock. It does not select
the first generic IPC poll worker. The older poll gate remains an uninstalled
experimental foundation; it cannot prove that its first candidate is the worker
which later imports the receiver's resource.

This checkpoint is source and software evidence only. It performs no Frame
installation, native-memory read, service restart, executable or shader change.
A diagnostic invocation is not a PC video frame or a resource namespace proof.

## Exact native route

The retained compositor is SHA-256
`0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96`.
The pinned import-handler vtable relocation resolves to `0x153f70`. This handler
allocates 320 bytes, sets its frame pointer to SP, retains the native pipe in
`x19`, supplies that same pointer in `x0`, and calls public `pthread_mutex_lock`
with return PC `0x153fc0`. Its caller is the message dispatch loop's virtual
handler call, whose return PC is `0x24fee0`; that loop has a 160-byte frame and
receives native IPC messages before invoking the selected handler. The actual
handler's saved parent pair must be `saved FP == original SP + 320` and
`saved LR == compositor bias + 0x24fee0`.

The supported preparation point is the genuine import lock's Before callback,
after the source-pinned metadata claim but **before the original native pipe
lock is acquired**, before payload dispatch, the import method and nested
Vulkan calls. The source route is an IPC message handler; the stack-bound
helper never runs for the first time in an Image, Bind or View callback.
Original lock success is essential: the native handler ignores that return,
but the diagnostic must refuse its own candidate on any nonzero result while
preserving native behavior.

Return-address equality is a candidate prefilter, not a complete caller-route
proof. The root authenticates the current ELF mapping/provider and the true
original public-entry register snapshot before this gate sees an event. After
independently safe current-thread preparation, the one fixed parent-pair copy
confirms the actual IPC dispatch caller. Unknown callers refuse the candidate;
stack bounds alone do not qualify it as the supported non-render route.

## Metadata before, private-read capability after

`Gate` is constructed with a pinned `PreparedImage` and an immutable root-owned
`CurrentMarker` callback. That callback returns the root's genuine existing
thread-local marker address for the current thread. It must be authenticated
outside the eligible callbacks; an arbitrary supplied byte or boolean cannot
replace it. The new gate adds no TLS variable of its own.

`before_first_begin()` claims one process-wide candidate at the source-specific
return PC. It checks the entry's aligned SP/FP and pipe-register shape, then
stages an immutable copy of the original public-entry registers and the verified
current TLS identity. It invokes the externally authenticated `Prepare` helper
once on this actual worker, before forwarding the original lock. The helper
supplies `OwnedThreadStack` from the actual thread's independently obtained
usable stack interval. This avoids cold libc setup while the native pipe lock
is held. No reader is called and **zero private bytes** are requested.
`pending_current()` means this metadata and SDK-provided usable stack are paired
to the current thread. It must never authorize a native stack or heap copy.

The paired `after_first_begin()` requires exactly unchanged original registers,
the same genuine current marker, still-current prepared stack and original lock
result zero. No preparer runs in After. The gate requests the fixed **16-byte**
parent pair at the original SP, within the source-shaped active frame. It checks
current ownership and unchanged entry before and after the reader, validates
the two parent values, and publishes read capability only after every check.

`current()`, `region_current()`, `window_current()` and `begin_frame_current()`
remain unavailable throughout staging and preparation. Even after admission,
the root still performs each boundary's source, current module/provider,
original-call pairing, order and copy-budget checks. The gate establishes no
socket continuity, lifetime lease, native namespace, video-frame, eye/UV,
accepted-submit or GPU-consumption association.

Root raw-observer Begin Before can carry the copied original entry and genuine
current-thread **metadata ownership**, so its Before/After pair is not lost.
Private scalar acquisition must wait for this gate's post-parent-check Ready
capability. Never treat SDK-provided usable-stack bounds as native-read
permission for the metadata event.

The cold helper's allocation/libc behavior and callback cancellation contract
remain part of the root's independently reviewed actual artifact. This gate
does not make a potentially cancelling helper safe merely by marking its
callback `noexcept`; the supported current pthread preparation path must be
verified before deployment. Preparatory libc work is allowed only at this
source-pinned non-render bootstrap, never a generic graphics entry.

## Bounded failure and accounting

Only one candidate and one preparation are allowed. The source-shaped claim is
consumed even if entry checks, original locking, preparation, the reader or the
parent comparison fails. Native failure, missing or changed Before, recursion,
wrong current thread, incomplete reads, ownership loss, repeated Begin, unwind,
fork, module unload and observer loss permanently terminate the gate. Calling
`arm_before_compositor_threads()` again cannot recover a claimed or failed
candidate. Lifecycle cancellation/retirement changes atomics only; it never
clears another thread's TLS marker or recycles native identity.

Requested and successfully confirmed reader bytes are reported separately.
A reader can have copied a partial span before reporting failure; a successful
copy can subsequently fail the ownership postcheck. The root's actual-copy
ledger must retain both, rather than deriving its total from the confirmed
count. A failed parent check consumes the same 16-byte ceiling, without retries.

The maximum additional bootstrap scope is **16 private stack bytes**, making
the existing 693-byte raw import scope plus this bootstrap **709 private bytes**.
There are no poll-frame copies, received request-payload copies, heap reads,
extra kernel queries or new graphics callback preparation in that ceiling.
Native return values and `errno` are preserved; the gate does no allocation,
file I/O, socket wait, dynamic lookup or lock acquisition itself.

## Focused offline evidence

The normal and ASan/UBSan fixtures each pass **1,916 checks** with actual pthread
stack bounds and aligned caller storage. They exercise zero-private-copy admission,
all original register mutations, native failure, missing Before, unrelated
mutex entries, forged/foreign marker references, foreign stack owners, every
partial-reader length from zero through 16, every parent-pair byte mutation,
entry/marker/retirement changes during acquisition, reentry, concurrent claims,
terminal unwind cleanup, loss/retirement and attempts to reset. Successful
reader calls which fail a later check remain visible in the copy totals.

The sanitizer fixture disables ASan fake-stack use-after-return allocation so
its source-owned aligned native-frame model actually lives on the pthread stack
whose bounds it obtains; address and undefined-behavior checks remain enabled.
The callbacks deliberately change `errno` to validate preservation. These tests
are not a headset run or a complete integration gate.

A separate offline validator checks 24 exact instruction points and the pinned
import-handler relocation in the retained native ELF. Its private source and
small receipt remain in ignored `build/`; no native disassembly or binary is
tracked. The root must still review the actual combined ARM artifact, original
forwarding, early loader admission, Vulkan listener/journal ownership and
recoverable installation before requesting a new hardware scope.
