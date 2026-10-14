# Bounded raw compositor preparation

Historical single-renderer preparation. The current source also admits the
exact pinned Facet path, with 96 instead of 80 scalar bytes per creation boundary
and a separately reviewed 720-scalar/757-total-byte maximum. See
[the current artifact contract](COMPOSITOR-RAW-ARTIFACT-20261013.md).

The [actual client capture](CLIENT-RESOURCE-RAW-CAPTURE-20261013.md) closes the
local request/reply/reference question. This checkpoint implements the next
offline components for the compositor. It is **not a deployable diagnostic**:
the pinned loader/provider adapter, immutable Vulkan listener/owning-journal
bridge, final ARM artifact and recoverable deployment manifest remain to build
and review. No new Frame installation or rendering change occurred.

## Implemented boundaries

`compositor_resource_sparse.*` copies only fixed native scalar spans into a
zero-filled, 1,344-byte scratch window and reuses `native_import_route`.
The scratch holes are never private reads. Before calling it, the root must
authenticate the source, paired original call, current thread/module/provider
and remaining budget. Requested and confirmed bytes are separate; the root
must additionally count real copies whose ownership postcheck fails.

`compositor_resource_raw.*` is a separate raw-only state machine. It records
one import begin, successful original payload-read/unlock, one synchronous
public image/binding/view chain, send lock, contiguous positive reply writes
and successful native finish. Native device handles and owning-Journal device
identities remain separate. Public result types come from the existing
compositor attempt code; native parsing comes from the established decoder.
It never constructs a lease or enters the strict carrier.

Private-copy admission is pure and precedes acquisition. Early and final
unlock scalars are copied while their native locks are still held and reused
after the original result. Vulkan stack scalars are copied before the call;
public Vulkan results are copied after it. Successful lock and write scalars
are acquired only after the genuine positive result. A terminal scope never
restarts or emits another result. Loss, nesting, altered entries/snapshots,
foreign ownership, changed routes, failures and exhausted budgets refuse it.

The raw cap is **672 stack-scalar bytes and 21 reply bytes**, including at most
eight positive partial writes. The exact `0x235` reply must contain the native
sequence, length nine and a nonzero fixed64 token. It does not copy the received
`0x234` request header. All connection continuity, native namespace, source
frame, eye/UV, accepted-submit and GPU-consumption proof flags remain false.

## Experimental thread gate

`compositor_resource_thread.*` implements a distinct, finite compositor
thread gate. It admits one source-shaped poll candidate, prepares the current
thread through an externally authenticated cold helper and checks 32 fixed
caller-link bytes. The first successfully locked import requires another
16-byte parent-link check. Only then can `current()` expose the usable stack
region. Retirement touches atomics only; another thread's marker is never
cleared or recycled. Failed candidates cannot reset their claim or budget.

The full experimental scope would therefore be at most **741 private bytes**.
This is a source-specific test foundation, not permission to read those bytes
on hardware. The poll loop is generic for IPC workers: its first candidate
may be a different worker from the desired import thread. The gate deliberately
refuses that mismatch. A green fixture does not establish correct production
worker selection, root bootstrap, native forwarding or current module ownership.
Do not deploy this candidate unchanged on the assumption that it necessarily
selects the receiver's connection. A more direct first-import bootstrap needs
its own non-render-thread and preparation review.

## Focused validation

- Sparse/full native-decoder equivalence: **62,790 controls** each in normal
  and ASan/UBSan runs. Every byte of the 1,552-byte fixture window is perturbed,
  including holes; all source dependencies, read failures and write offsets
  are checked against the original decoder.
- Raw state machine: **20,306,608 controls** each in normal and ASan/UBSan runs,
  including all **137,980** partitions of the 21-byte reply into at most eight
  positive writes. The final eighth write and 672-byte boundary are admitted.
  Producer allocation traps report zero allocations.
- Sparse-to-raw integration uses the real sparse helper and native decoder on
  owned synthetic frames. Admission precedes each reader call; the actual-copy
  ledger remains truthful when a copy succeeds but its postcheck fails.
- Thread gate: **2,185 controls** each in normal and ASan/UBSan runs with real
  pthread stack bounds and synthetic source frames. Wrong threads, foreign
  markers, altered links/entries, partial failures, competing claims, cancellation,
  retirement and repeated acquisition are checked. Sanitized native-stack
  fixtures use the existing `detect_stack_use_after_return=0` configuration;
  an ASan fake stack is not the real pthread stack being exercised.

These are focused software results. They are not a new complete local gate,
real native compositor execution, GPU evidence or a headset picture test.
The four affected CMake-integrated compositor checks also pass together,
including the unchanged original native-route decoder.
Unchanged earlier complete-gate evidence is retained separately. Small results
and source checks live under ignored
`build/private-compositor-raw-integration-20261013/`. Completed compiler/test
processes are reaped; regenerable binaries, borrowed build copies and temporary
CMake caches are removed while preserving private source and hardware evidence.

See [the current source contract and remaining artifact work](COMPOSITOR-RAW-NEXT-SCOPE-20261013.md).
An additional [native registry audit](COMPOSITOR-RESOURCE-NAMESPACE-20261013.md)
finds current-entry uniqueness across one resource-manager instance. A distinct
registry-based association may avoid general descriptor-lifetime machinery,
but its incarnation, entry lifetime, actual peer and GPU generations still
need independent proof. This does not weaken any existing lease or raw contract.
The native PC Steam sender remains unchanged.
