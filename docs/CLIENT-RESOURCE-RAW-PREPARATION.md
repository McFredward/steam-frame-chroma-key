# Reviewed raw resource transaction diagnostic

## Implemented scope

The [actual cold-thread capture](CLIENT-RESOURCE-THREAD-CAPTURE-20261012.md)
established current-thread ownership at the six first-trace resource boundaries.
The diagnostic is implemented at `0f53189` using that finite prepared
phase. It captures **one raw import transaction**, without relaxing subsequent
ownership refusal or entering the strict resource carrier.

It reuses the original native route decoder, entry/CFI implementation, native
read/write forwarding, existing born resource-pipe token and bounded writer.
The new pure copy-admission check runs **before private copies**, requiring the
right source state, unchanged paired entry, current thread/module, original
successful result and remaining transfer/byte budget. Stack copies contain
only fixed scalar spans, not full frames or saved-pointer walks. Pending fields
are copied before native unlock and reused afterward; Begin/Reference copies
require successful original locks. Transfer fields and protocol prefixes are
copied only after successful original I/O.

The bounded raw interpreter retains owned headers and scalar facts only. It
requires an exact request/reply sequence, opcodes, length, full token, native
pending result and following reference. It never calls strict `Observer::begin`
or `publish`, invents a connection generation, or treats reference-lock success
as successful completion of the following reference RPC.

Limits are **24 transfers total, eight per kind, 2,068 scalar bytes and 33 wire
metadata bytes**, hence **2,101 additional private bytes**. Request payload is
counted up to 512 bytes, never copied. Record/file/time caps remain **32 records,
64 KiB and 120 seconds**. A single terminal aggregate avoids per-transfer queue
growth. There are no additional kernel queries, pixel/plane-descriptor/shader
reads, native writes or rendering changes.

All mutable raw scope state belongs to its one actual callback thread.
Retirement and background finalization touch atomics only. The writer may wait
at most 32 one-millisecond pauses for callback completion, never for a blocked
native read/write. The footer explicitly reports whether totals were quiescent
and whether a started transaction was abandoned. An interpreter must require
the actual matched terminal record, no loss and a quiescent complete footer;
`resource_raw_completed` alone supplies no proof.

## Exact reviewed candidate

The sole scope marker is hidden, read-only, non-executable **`FCRAW001`**, schema
**5**, envelope **11,800 bytes**. Main retains one own public bridge, ten private
156-byte entry veneers and **18-byte static TLS**. Native providers are the
actual `read/__read` at `0xdf330` and `write/__write` at `0xdfee0`, size 204 and
version `GLIBC_2.17`. Alias names share the native PCs. Audit has no provider
imports; its immutable preparation is 216 bytes, with five relative string
fixups and appended read/write RVAs at offsets 200/208.

| Own artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Main ARM library | 364,672 | `98221ae5a31e4aef77bae50818e1759347664b07059c6d23f3881d7a16de4696` |
| Audit library | 69,080 | `aa1f16df2bf0b7a0c3105a9950e9da5a826b860f04bd89b42aa225dd0d3b10df` |
| Envelope | 11,800 | `c84832bf3e62de6f0f6549e6418fe42ba61a4db275ec6bc3cab75952eb7f699f` |

The plan hash is
`7456b0f349c38c58d1c6af78c17201eecfa661bcd37ef8d30a44df9a288c97e7`.
The **six own inputs total 557,815 bytes**, including the plan and
both exact session helpers. Kernel inspector helpers are unnecessary in this
scope. The final expected libc loader name is the historically observed
`/usr/lib/libc.so.6`; it is not inferred from canonicalized file paths.

An initial offline preparation used `/lib/libc.so.6`, which was not the observed
loader name. It was rejected in integration review and never deployed. Only
the plan/audit/envelope were regenerated with the actual recorded name; main
and its executable evidence were unchanged. The old candidate remains private
historical evidence and must not be staged.

## Validation and review

- The complete local host gate passes **56/56 CTest checks**, including existing
  wire/protocol, GPU, recovery and all legacy audit/ABI modes. This was one
  complete run; later bounded repairs reuse unchanged passing evidence.
- The pure raw interpreter/copy admission passes **370,370** normal and
  ASan/UBSan controls, including all tested partial partitions, native failures,
  malformed/contaminated replies, ownership loss and the eighth/24th transfer.
- Sparse/full existing-decoder equivalence passes **3,150,996** normal and
  ASan/UBSan controls over all request totals 13..512 and their offsets. A
  separate actual pthread-owned fixture passes **410** bounds, foreign-thread,
  budget and loss-during-copy controls. Neither fixture proves live native
  lock/module ownership on a headset.
- The actual final ARM library passes **72** native read/write ABI, real pipe,
  errno and blocked-cancellation/RAII checks, plus **39** native epoll checks,
  using the pinned Frame libc/loader under QEMU. Those are actual final-ELF
  controls, not rebuilt toy entries or a Frame-kernel/headset test.
- Actual canonical native profiles, mutated descriptors, full offline bundle,
  actual session-envelope validation and 216-byte generated audit compilation
  pass. Focused preparation/session and actual loader controls are retained.
- Independent review passes **96** source/final-ELF controls and **45** separate
  callback/copy-confinement source controls. The exact writer formatter remains
  valid at **3,305 / 4,096 bytes** for its widest realistic raw record.

Review caught and repaired three pre-deployment issues: source/pair admission
had to precede private copies, already-admitted eighth/24th transfers needed
correct limit handling, and wire copies needed immediate pre/post ownership
checks. A copied last scalar span is counted even if its postcheck fails.
Scope state is never read by the writer; inconclusive abandonment stays explicit.

Small receipts live privately under ignored
`build/private-resource-raw-integration-20261012/`. Owned fixture/compiler/QEMU
groups are reaped and generated build trees removed after each run. Current
deliverables, source/manifest and hardware evidence are retained.

## Separate authorization and recovery

The [separately authorized hardware run](CLIENT-RESOURCE-RAW-CAPTURE-20261013.md)
completed and was fully rolled back. It captured one matching transaction
using 393 additional private bytes; no diagnostic remains installed. The
preceding register-only/cold-thread approvals allowed zero extra resource
private bytes and did not authorize this scope. Any future run still needs
its own agreed scope and restart readiness. Stage/start/activate require
`--ack-resource-raw-transaction-scope` alongside the four preceding scope
acknowledgements, exact reviewed inputs and coordinated Steam-restart readiness.

Only the owned user-directory inputs/session and exact runtime Steam
user-service override may change. Document and verify fresh native/service
pins before activation. The deadline stops recording only, never restarts Steam
or rolls back automatically. After separately coordinated manual readiness,
use `client_startup_session.py rollback --ack-steam-client-restart-readiness`,
archive capture/inventory/recovery receipts, verify original native/environment/
service state and mappings, and remove only exact matching owned files.

No native PC Steam sender modification is part of this implementation.
Connection/resource namespace continuity, source video/eye, native UV/foveation/
reprojection and coherent GPU consumption remain unproven. Mask shader
activation remains disabled; this is not working streamed genuine-mask
passthrough. See [the remaining proof boundaries](CLIENT-RESOURCE-NEXT-SCOPE-20261012.md).
