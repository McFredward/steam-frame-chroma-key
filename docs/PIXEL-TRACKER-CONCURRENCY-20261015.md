# Native pixel tracker concurrency, 2026-10-15

The subsequent approved [complete native capture](PIXEL-SNAPSHOT-CAPTURE-20261015.md)
now verifies progress beyond this refusal on the actual Frame, followed by full
restoration. The local preparation and limits below remain the original record.

## Actual second attempt and restoration

The approved first-refusal package (`59cd2f688e311b32b39dc6ec10d82bf81b0f7db57090c49daa5bfeb1298023e0`,
228,680 bytes) loaded on the Frame after its mapped providers, isolated immediate
library load and native UI health were checked. The maintainer confirmed that
the streamed build676 test pattern was visible with the Steam menu closed.

The pre-arm record now identifies the actual first refusal:

```json
{"failure_code":1,"failure":"contention","site":"traceBindImageMemory","core_lost_state":0,"armed":false,"selected":false}
```

Every Core reason and capacity counter was zero. `LiveTracker::locked` used
`try_to_lock`; failure to acquire that internal mutex immediately and permanently
poisoned observation. Valid parallel resource observations can trigger this
policy, and `try_lock` may also fail spuriously. The record does not identify
the competing owner. It does identify the failed internal lock acquisition,
rather than a missing source pattern, unsupported descriptor or lost Core entry.

The arm token was never created. No diagnostic GPU image copy was admitted,
and neither `snapshot.p010` nor `snapshot.json` exists. Following separate manual
rollback approval, activation config/manifest were removed, SteamVR restarted
once, and native UI health returned `ready`. The complete same-user mapping
inspection, including the approved protected-map read fallback, found no owned
or foreign diagnostic mapping. Run-library removal completed with no errors.
Small inactive package and evidence files remain. The maintainer was told that
standby was safe; no automatic restart, rollback or expiry was introduced.

Private evidence: `build/private-refusal-run-20261015/`, capture ID
`221bea80a49fd76a80e9b4223ec4e86b`. `arm-result.json` retains the complete frozen
failure; `rollback-result.json` retains removal and native health evidence.

## Targeted repair and limits

Serialize finite internal CPU metadata observations instead of dropping a
parallel observation and permanently refusing the session. Keep native Vulkan
calls, GPU completion work, file output and driver waits outside this mutex.
Preserve same-thread recursive-entry refusal, resource/generation admission and
the original Core loss/capacity/unsupported guards. Do not introduce TLS storage,
a deferred journal, a guessed source binding or a timer-based retry deadline.

This deliberately permits waiting for a short internal CPU critical section.
It does not promise a wall-clock deadline under OS scheduling. Actual callbacks
operate on bounded fixed tables and bounded shader fingerprints, without native
callbacks, I/O or allocation while holding the tracker mutex. Native command
retirement, submission and readback remain separate lifetime checks.

## Completed focused validation

The actual production-loader fixture first ran against the OLD library. Three
threads independently created, bound, viewed and retired 24 native image/memory
resources each. The old policy failed with `contention` at `traceAllocateMemory`,
with zero Core counters. This reproduces the observed failure class in actual
production wrappers, without a native Steam binary or a selected image copy.

The corrected library passes all 14 loader cases, including those simultaneous
resource lifecycles, ordinary queue/command retirement, armed negative controls
and inert invalid/unrelated sessions. No active case reports tracking refusal;
no unmatched image is copied. The tracker separately passes 5,225 controls
normally and the same 5,225 under ASan/UBSan: overlapping image binding retains
the original allocation generation/offset, parallel draw/reset and delayed
native submit retain their saved identity, recursive entry cannot hang, and
real first failures remain sticky. TSan compiled but its host runtime aborted
before the tests with `unexpected memory mapping` (exit66); no TSan pass is
claimed. Six actual refusal-formatter tests also pass.

Independent source review has no open blocker. The strict final ARM library is
229,232 bytes, SHA-256
`def9445f8ec5690150026a9b35e782e424efe0b61bc13dc90dd067f5efdef5e4`.
It retains exactly three private exports, no own TLS, no Vulkan linked dependency,
immediate binding and RELRO. The public `pthread_self` import is GLIBC2.17;
required versions remain within the previously checked Frame providers' range.
Fresh actual provider/mapped-process checks still precede any new activation.
The two session/health helpers are unchanged.

Evidence and the current small package live under
`build/private-concurrency-prep-20261015/`. Unchanged readback GPU, source-output,
session filesystem and health-gate evidence is inherited, not rerun; this is
not another complete GloomhavenVR gate. No new main DLL or ModBuild is needed.
Source and local Vulkan checks do not establish successful Frame pixel
acquisition. No new device activation, restart or capture is authorized by this
local repair; a fresh readiness agreement is required for the reviewed package.

The goal remains independent scene coverage with the camera on the headset.
Decoded image acquisition, source-header recovery and exact source-frame/eye/UV
association are still prerequisites. Streamed passthrough is not enabled.
