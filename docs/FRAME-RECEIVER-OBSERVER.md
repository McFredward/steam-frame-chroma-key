# Receiver observer without private executable probes

## Feasibility result

A narrow receiver scalar observer can use **ordinary `LD_PRELOAD` interposition
of imported `pthread_mutex_lock` and `pthread_mutex_unlock`**, without a kernel
probe, an executable detour, a private method replacement or a disk binary patch.
The inspected receiver calls those functions through versioned dynamic PLT
entries. Copy the selected tuple after successful lock acquisition and before
the original unlock, at exact version-checked caller sites.

This is a feasible observation boundary, not an installed observer. It still
reads private native records and changes the receiver's launch environment, so
it requires the additional narrowly scoped runtime permission described in
[the adapter proposal](FRAME-NEXT-ADAPTER.md). No Frame access, preload, private
record read or runtime interposition was performed by this audit.

The complete resource/import, PC source-session and GPU eye/UV bridges remain
open. The result replaces the receiver part of a proposed private-function
probe; it does not make those remaining links disappear.

## Exact build and public ABI

Only the private `vrlink` build already described in
[the receiver report](FRAME-ASSOCIATION-DATAFLOW.md) is supported:

- SHA-256 `0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b`;
  ELF build ID `0a51addb6a9ef05dc52c055b1076947908f20acc`.
- ELF64, little-endian AArch64, position independent, no dynamic `SYMBOLIC` flag.
- `pthread_mutex_lock@GLIBC_2.17` is an undefined global dynamic import with
  `R_AARCH64_JUMP_SLOT` at `0x24b8f0`, through PLT `0x1b4f0`.
- `pthread_mutex_unlock@GLIBC_2.17` is an undefined global dynamic import with
  `R_AARCH64_JUMP_SLOT` at `0x24b6e8`, through PLT `0x1b0e0`.
- Both PLT stubs tail-branch to their resolved function. The native caller's
  return address therefore remains the instruction after its `bl` call.

Interpose only the documented C signatures `int (pthread_mutex_t *)`. Use the
wrapper's direct caller return address relative to the validated executable's
load bias. Do not read AArch64 caller registers, unwind private stack frames,
guess virtual method signatures or inspect compositor C++ objects.

Resolve and verify the original versioned libc/pthread functions before enabling
observation. Verify the real symbol provider and preload resolution in a local
fixture, including constructor/reentrancy behavior. Do not run `dlsym`, `dladdr`,
file reads, namespace discovery or source hashing inside each mutex callback.
An enabled observer needs precomputed executable bounds and call-site checks.
An unsupported secure-execution/loader configuration must remain uninstrumented.

## Protected selected tuple

| Callback | Native call | Exact direct-caller return address | Action |
| --- | --- | --- | --- |
| Lock | `0x34a4c` → public lock PLT | `load_bias + 0x34a50` | Forward the original call. Only when it returns zero, open a bounded thread-local selection scope and copy the tuple. |
| Unlock | `0x34d70` → public unlock PLT | `load_bias + 0x34d74` | Before forwarding, require the same thread, mutex and active scope; copy/compare the tuple again, then close the scope and forward unchanged. |

The actual mutex argument `M` equals selected capture record `R + 0x38`.
Consequently the three scalar fields are:

| Field | Relative to `M` | Read size |
| --- | --- | --- |
| Native shared-resource token | `-0x18` | 8 bytes |
| Stored received key | `-0x10` | 4 bytes; require a valid 16-bit value |
| Decoded content generation | `-0x08` | 8 bytes |

The caller loads the key from that protected record at `0x34a90`, then invokes
the selected-key metadata lookup at `0x34aa4`. It passes exactly that record to
the presentation helper at `0x34d40`/`0x34d50`, retaining its lock until unlock.
Between successful acquisition and metadata lookup, the caller is straight-line:
there is no other call or conditional branch. The audited caller does not store
through, or pass an alias of, the selected record before the helper. The helper
only reads the record's first word and shared token, then replaces its record
argument with the copied token; it does not write the selected fields or pass
the record pointer to its private compositor call.

The normal decoded-content writer uses the corresponding record mutex while
storing key/generation at `0x34440`/`0x34444`. These facts support copying the
protected selected tuple at the public mutex boundaries. Still compare all
three fields at scope entry/exit and reject a changed tuple. Resource teardown,
reconfiguration or unsupported mutation must invalidate evidence; a mutex is
not a source-session identity or a GPU ownership fence.

The selected unlock is also reached by the **missing-metadata** branch
`0x34f10` → `0x34d54`. Moreover, the presentation helper can return early when
its dimensions or layout are invalid. Therefore `0x34d74` by itself does not
prove that a private presentation happened or was accepted. Record this event
as a protected selection observation, never as an accepted video submit.

## Protected metadata without private stack/register reads

The metadata lookup has these public mutex call sites:

- Lock call `0x52008`, direct caller return `load_bias + 0x5200c`.
- Unlock call `0x52038`, direct caller return `load_bias + 0x5203c`.

Its argument `N` equals the native metadata-record base `B + 0x4248`. Required
fields therefore have these exact offsets:

| Field | Relative to `N` | Read size |
| --- | --- | --- |
| Metadata-valid flag (`B + 12`) | `-0x423c` | 1 byte |
| Stored full key (`B + 8`) | `-0x4240` | 4 bytes |
| First native metadata word (`B + 0x4278`) | `+0x30` | 4 bytes |

The full metadata block is 340 bytes starting at `N + 0x30`; initially copy
only the required first word. Its interpretation stays the native 32-bit
presentation field, not a mask frame/session key.

The metadata unlock site is shared by **successful, invalid and mismatched-key
lookups**. Do not mark it successful merely because its return address matches.
Instead use this conservative state machine:

1. Capture metadata only within an active selected-record scope. Forward the
   real lock and sample nothing on a nonzero result.
2. Pair the exact metadata lock/unlock mutex and thread. At pre-unlock, while
   still protected, require the valid flag and stored full key equal the scope's
   selected key, then copy the first metadata word.
3. Require exactly one metadata pair for the selection scope, no nesting,
   unmatched event or incomplete pair, and unchanged selected tuple at its end.
   Reject scopes with zero or multiple pairs.

The pinned selected caller unconditionally supplies its protected key to the
lookup before any other call. Requiring the one matching pair and the actual
valid/full-key checks avoids relying on the private function's return register
or on its escaped internal pointer. The lookup unlocks before returning that
pointer; retaining it for later copying would be unsafe. Keep only an owned
scalar copy. This still does not prove that native metadata could not change
after its lookup unlock and before later GPU consumption.

## Forwarding, limits and failure behavior

All unrelated mutex callers, including receiver decode/selector calls at other
sites and locks in dependent libraries, forward normally without a record read.
If an unexpected callback occurs while a selection scope is active, count any
matching metadata pair conservatively and reject ambiguity. Selection lock
failure takes the receiver's native exception path; do not suppress it, invent
a scope or change any return value. Noncanonical/unaligned pointers, pointer
arithmetic overflow, inconsistent mutex addresses, invalid key/generation/token,
constructor recursion or unsupported build state disable sampling.

Use fixed-capacity thread-local scope state and preallocated publication slots.
Copy before real unlock and publish only owned scalar values asynchronously;
do not add a second lock, dynamic TLS allocation, allocation, log formatting,
socket work or blocking I/O on the receiver thread. Preserve native `errno` and
function results. Any queue loss makes the capture incomplete. A recorder event
number can join its own scope records but cannot stand in for a native frame ID.
Cap sampled scopes and aggregate output; do not log every ordinary mutex call.

Before deployment, test actual PLT interposition, exact caller matching,
wrong-version rejection, negative-pointer arithmetic bounds, mismatched key,
missing/multiple metadata pairs, native lock failure, nested unrelated locks,
constructor reentrancy, queue saturation and observer unload/restart. A user-
directory-only preload manifest must be removable without touching the original
receiver binary or system image. No automatic SteamVR restart or rollback timer.

## Remaining gaps and validation scope

This observer can supply a protected selected key/content/token tuple and a
valid-key metadata-word copy. It cannot establish a compositor import namespace,
accepted private presentation, source-session/wire-key reuse generation, physical
eye or coherent GPU-consumed uniform/rectangle lifetime. Those require their own
observations and gates; missing columns retain opaque rendering.

Offline validation passed 32 pinned instruction checks, dynamic symbol/version/
PLT relocation checks, and bounded dataflow checks for the straight-line key
lookup and selected helper's record accesses. The private validator and small
result remain ignored under `build/adapter-audit/`. No proprietary binaries or
instruction listings are tracked. No interposer was implemented or loaded by
this documentation audit.
