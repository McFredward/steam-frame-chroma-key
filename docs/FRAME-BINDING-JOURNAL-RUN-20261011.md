# Native ordered-binding diagnostic: 2026-10-11

This was an authorized passive diagnostic, using source checkpoint `a822f2c`.
No shader replacement, mask activation, mapped-memory read or image readback
was performed. Native Frame recovery and exact-file cleanup are complete.
The recording does **not** establish a complete resource/frame/eye association
or working Steam Link passthrough.

## Session and evidence

The deployed AArch64 library was 223,408 bytes, with SHA-256
`66a279e25e79e6cfc34fb1e90d7ecf723d9c8d21be1dccd9a74432ee43c8d0f0`.
An isolated library/manifest probe succeeded before activation without creating
a Vulkan instance or device. The initial native compositor had no diagnostic
mapping, environment or activation files.

Times below come from the Frame's session ledger, in CEST. The helper requested
the approved restart at 22:06:33; SteamVR was active at 22:06:40, with diagnostic
compositor PID 626057. After the maintainer confirmed a visible PC-VR stream,
read-only inspection also found the actual native `vrlink` process. Arming was
requested at 22:08:26. The writer recorded one arm event and all 32 selected
resource snapshots, then closed at its explicit byte limit. Recording completion
did not restart SteamVR.

Private evidence is retained in
`build/private-frame-journal-run-20261011/`, including the session ledger,
deployment plan, bounded capture, reproducible analysis, copied-file inventory
and recovery/cleanup checks. The 1,045,594-byte capture has SHA-256
`57b9d3408fbd6770635b82762982a47287e980ab370f792076912554e1931148`.
No native binary, shader instructions or disassembly is published.

## What improved and what failed

| Observation | Result |
| --- | --- |
| Resource snapshots | 32 retained; 0 globally complete |
| Pipeline/event loss | 0 state losses; 0 dropped events |
| Binding contention | 0 |
| Journal publications/replays | 824,246 / 824,246 |
| Journal queue losses | 201,483 |
| Unknown binding-state losses | 9,222 |
| Other journal transport losses | 0 registry, order-gap, batch, wrap or closed-call losses |
| Object-table capacity losses | 0 across all reported tables |

The capture contains 196 shader records, 101 pipelines, 675 recorded draws,
675 accepted submissions, 32 binding snapshots, one arm record and a footer.
Two trailing emitted events are not serialized after the byte cap; its footer
explicitly reports truncation. All selected snapshots are present.

The supported target records are locally complete in all 32 samples. Binding 9
retains a 1,152 × 4,608 image, its explicit DRM modifier/two-plane layout,
external handle types, allocation import fields and dedicated-image generation.
Binding 25 retains the custom-border components/format and the YCbCr conversion
attachment and parameters. Imported FD integers are metadata only: they were
not inspected or duplicated, and do not identify video frames. Uniform slices
at bindings 0/4 retain alternating effective offsets 320/960 and range 272;
their contents remain unread.

These individual records cannot override historical queue loss. The global
binding-31 presence remains unknown, despite locally absent target entries.
The 9,222 unknown-state losses have no exact API-operation attribution in this
version; missed earlier lifecycle observations are a possible explanation,
not an established cause for every loss. GPU-coherent uniforms, selected video
generation, source session and physical-eye association remain unproven.

## Why the local checks did not predict this run

The merged source passed the complete 15-suite CI gate, cross-build and protocol
sanitizer. Its focused GPU test repeatedly exercised concurrent producers and
an unthrottled selected-snapshot burst without loss. That validates the tested
ordering, bounds and fixture output; it does not reproduce this native workload's
API rate and object population.

The new journal removes the old binding-table try-lock loss, but moves all state
mutation to one background owner. Under the native workload, that owner replays
hundreds of thousands of records while producer queues still overflow. The source
currently scans producer lanes for each replayed ticket and uses linear object
table searches. Their contribution must be measured before choosing a repair;
the footer alone cannot distinguish lookup cost, lane-selection cost, burst
shape and scheduling delay.

The next source work is a native-shaped throughput fixture and profiling of the
background replay path, followed by bounded indexing or another measured
replay-cost reduction. Keep ordering, handle-generation invalidation and sticky
loss rejection intact. Do not repeat the same headset trial with only a larger
queue, erase pre-arm losses or activate a mask based on incomplete snapshots.

## Verified recovery

After the maintainer explicitly approved manual rollback, both activation files
were removed at 22:09:36. The native SteamVR restart completed at 22:09:42.
Read-only checks found compositor PID 633793 active, with no diagnostic mapping
or environment and no active rollback watchdog.

The capture and full session inventory were copied and hash-verified locally.
At 22:11:15, cleanup verified ownership, closed recording/rollback locks,
file inventory and restored native state, then removed only the owned diagnostic
session directory. A final read-only check confirmed that directory and both
activation files were absent and native SteamVR remained active. Cleanup caused
no further restart. The SSH credential session was closed, and the maintainer
was told that the Frame could enter standby.
