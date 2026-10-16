# Sampler comparison cold refusal and restoration, 2026-10-16

## Actual device result

The maintainer approved the locally prepared comparison package and one manual
SteamVR restart. Exact current native imports and isolated immediate library
loading passed. The291248-byte ARM library had SHA-256
`61c8016596da18047d5d85a8ae8eeb75c44b6c9bdfd8a6124ecb11d37a8cc8b3`.
Native UI startup health reached `ready` after the approved restart.

The background `prepared` record reported:

| Field | Value |
| --- | --- |
| Capture ID | `e4cafa386164bebe2f63d8c9e8345d97` |
| Instance API | 1.1 (`4198400`) |
| Sampler ready | false |
| Device admission complete | false |
| Reported YCbCr / compute-route flags | false / false |
| First unreviewed extension | empty |
| Format queries complete | false |
| Native compute queue | true |
| Reported sampler result | -8 |

The operator did **not** request a stream or create an arm token. No command was
selected and no raw image, sampler output or snapshot metadata was produced.

## What this does and does not identify

This is a cold admission refusal. Format discovery and sampler preparation were
skipped. In this version, `-8` is the field's initial
`VK_ERROR_FEATURE_NOT_PRESENT` sentinel; it is **not** a returned driver failure.
The parser leaves several booleans at their defaults until it reaches later
parts of the input chain. Their false values therefore do not prove that the
actual native device disabled YCbCr or lacks the covered compute routes.

The precise first refusal was not recorded. Possible source paths include the
blanket enabled-statistics feature guard, input bounds, and unsupported or
conflicting device/loader feature-chain records. An empty extension string does
not distinguish them. Neither enabled statistics nor another specific input
may be described as the proven cause of this device attempt.

The local smoke cases had not enabled the optional pipeline-statistics feature.
The prior helper negative control deliberately accepted its blanket refusal as
policy; it could not establish compatibility with SteamVR's actual inputs.
The diagnostic needs an exact first-refusal reason and input location, feature
observed/enabled distinctions and a real native enabled-feature control.

## Narrow follow-up design

Mere device feature enablement is not an active query. Under the existing
primary-only/no-secondary/no-inheritance envelope, a conservative per-recording
query-begin history can preserve the counter boundary without rejecting an
otherwise eligible idle feature. Both core and indexed query-begin routes must
be intercepted, unknown future routes remain excluded, and any prior query
permanently disables sampling in that recording. A new successful recording
starts fresh; ending a query alone never clears the guard.

This source/specification design is independently reviewed, but it does not
retroactively identify the actual failed input or establish a new headset pass.
The next package must distinguish a skipped preparation from an actual Vulkan
result and report the exact rejected chain/type/function when applicable.

The local repair now records all21 incomplete-refusal branches with fixed
reason names and bounded input indices/type/function values. It distinguishes
observed from unobserved feature values and skipped sampler preparation from an
actual Vulkan result. The initial result is now `VK_NOT_READY`, paired with
`sampler_prepare_attempted:false`, until an actual preparation call occurs.

Both native query-begin entry points conservatively dirty the existing exact
recording history before unchanged native forwarding. Two atomic route bits
permit at most two background observations per device; there is no per-frame
logging. The finite API envelope refuses unknown future query-begin routes.
Ending a query does not clear the guard, and ordinary native query counters and
results remain unchanged.

Focused helper validation passes180 controls normally and under ASan/UBSan.
New actual software-Vulkan loader cases enable only pipelineStatisticsQuery,
with no device extension in the minimal case and one explicitly supported
indexed-query extension in the second case. Both admit the valid device, retain
the original zero/available native query result and observe the actual core/
indexed history guards. The original b16 library fails the minimal new case
specifically at `device_admission_complete:false`, while the same original
native operations succeed. This establishes the overly broad old policy
causally for that valid input; it does **not** prove this was the actual Frame
input that failed. The14 unchanged loader cases, tracker history and shader/GPU
evidence are inherited locally rather than repeated for this repair.

## Complete manual restoration

The maintainer separately approved rollback. Configuration, manifest and arm
activation were removed before one manual SteamVR restart. A fresh native UI
health invocation returned `ready`; the complete protected same-user map scan
found no owned or foreign diagnostic mapping and was not uncertain. The run
library was removed, rollback completed without errors, and all three image
output paths were absent. Inactive private package/evidence files remain.

The maintainer was immediately told the Frame could be placed in standby.
There was no automatic expiry, rollback or restart. No further Frame access is
needed for local repair. New deployment/restart needs fresh approval and actual
current-provider/startup checks. PC source build676 remains unchanged; no Steam
PC sender modification, native shader replacement or passthrough activation
occurred.

Private exact native evidence and rollback records are pinned in
`build/private-sampler-run-20261016-r1/`. This attempt does not replace the earlier
successful raw-image capture or establish source/frame/eye/UV association.
