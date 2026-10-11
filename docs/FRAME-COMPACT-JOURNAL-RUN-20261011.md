# Lossless compact-journal Frame run, 2026-10-11

The authorized PC-VR-stream capture retained all 703,494 binding lifecycle
observations and all 32 selected CPU resource snapshots without journal loss.
This closes the diagnostic overflow found in the earlier
[ordered-journal run](FRAME-BINDING-JOURNAL-RUN-20261011.md). It does not establish
source-video identity, GPU-consumed uniform values or working mask passthrough.
Manual restoration and exact-file cleanup are complete.

## Artifact, activation and limits

The deployed source checkpoint is `95ddb65`, with documentation checkpoint
`92847f6`. The strict AArch64 library is 230,272 bytes, SHA-256
`9100902dfa240427523099062ccf4f77fd647d48979b37eb966d391943dc175d`.
The [complete 17-suite CI gate](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38173292576),
Windows sender cross-build and protocol sanitizer passed before deployment.
This native run is separate hardware evidence, not another software-gate run.

After the maintainer confirmed restart readiness, the helper requested SteamVR
restart at **22:46:09 CEST** and reported launcher readiness at 22:46:13. A
read-only process check verified the diagnostic in compositor PID 703052.
Following explicit PC-VR-stream readiness, arming was requested at **22:47:32**.
The subsequent inspection found the actual receiver process executing
`/opt/steamvr/tools/vrlink/bin/linuxarm64/vrlink`. Its executable was not hashed
in that post-stream inspection; the pinned receiver fingerprint used by static
analysis is separate evidence.

Recording stopped at the configured byte cap, without a timed restart. The
1,043,047-byte private capture has SHA-256
`ad68d34b2a3aa7c2b8e56c0947cd82bc4d134b1cd2f937f5213547dfd934f413`.
Its owned writer lock was verified closed before copying. No image, mapped
uniform content or private native metadata record was read; no shader or mask
was activated. Private captures, process inventories and reproducible analysis
remain in ignored `build/private-frame-compact-run-20261011/`.

## Measured completeness

| Evidence | Result |
| --- | --- |
| Binding publications / replayed observations | 703,494 / 703,494 |
| Publication and replay losses, every recorded operation | 0 |
| Header/byte queue, registry, order-gap/wrap, batch and closed-call losses | 0 |
| Binding table capacity and pipeline-state losses | 0 |
| Event-queue dropped events | 0 |
| Selected resource snapshots | 32/32 source-complete, reason 0 |
| Required bindings 0, 2, 3, 4, 6, 9, 25 and 31 | Complete in every snapshot |
| Descriptor layout binding 31 | Absent in all 32 observed layouts |
| GPU-consumed coherence / video-frame association | False in every snapshot |

The capture contains 212 shader records, 109 pipelines, 660 recorded draws,
660 accepted-submit records and 32 resource snapshots. The footer reports
1,676 emitted events; 1,674 non-header/footer records were serialized before
the byte cap. Two trailing output events were omitted at that explicit bound.
Consequently this is not an exhaustive event transcript. All 32 requested
resource snapshots are present; bounded output truncation is distinct from
lost binding observations, which would invalidate source completeness.

Each selected resource snapshot is paired with its preceding complete recorded
draw and a later complete accepted submit using the same device, command and
command generation. Its draw/snapshot pipeline generation and stage records
also match. The observed shader pair remains `VS_DISTORT` (9,460 bytes,
FNV-1a `9cc04d186b9c8fe5`) and `PS_DISTORT` (6,620 bytes,
`f333a137b4e79ced`). Accepted submission is not GPU completion, immutable uniform
content or a source-video-frame identity.

The 32 snapshots belong to 16 command recordings, with two snapshots each.
The ordinary draw event deduplicates a command/pipeline pair; matching does not
require a separate draw output record for each snapshot. An independent
read-only review passes 1,296 checks over the capture, aliases, submit relations,
loss counters and restoration evidence. Its small validator/results are retained
privately beside the recording. This is captured-public-metadata validation,
not native private-ABI or headset-image proof.

## Resource facts and remaining mapping

All 32 snapshots retain the imported binding-9 10-bit two-plane image metadata:
`VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16` (the P010 route),
extent 1152 × 4608 × 1, explicit DRM modifier and two
memory-plane layouts. Binding 25 retains custom border color and YCbCr
conversion metadata. This certifies the supported API metadata in this capture;
it does not interpret the image extent as physical eye halves.

The fresh alias checks use buffer handles **and generations**, memory bindings
and effective ranges:

- Bindings 0 and 4 share the same 272-byte slice in every snapshot. Effective
  offset is 0 in 16 snapshots and 640 in 16 snapshots. Slice number is not an eye
  identity.
- Bindings 2 and 6 share the same 2,800-byte range at effective offset 0 in every
  snapshot. A shifted fragment descriptor therefore does not explain the
  vertex/fragment record difference in these observations.
- Binding 3 has effective offset 0 and a 400-byte effective range. Its selector
  contents remain unread.

The next coherent-eye check must establish the actual selected vertex record
`K`, eye route, rectangle fields and their protected GPU lifetime. Assuming
`K == 0` from matching buffer ranges would manufacture proof. The next native
resource adapter must separately observe the selected receiver key/content
generation and successful compositor import. These concrete boundaries and
their authorization limits are recorded in [the Frame adapter plan](FRAME-NEXT-ADAPTER.md)
and [the Windows sender plan](PC-NEXT-ADAPTER.md). Repeating a descriptor-only
capture cannot produce the missing identities.

## Background cost

The writer measured an 86.895-second window and 1.490 seconds of its own thread
CPU time: approximately **1.715% of one core** averaged over that window. Replay
took 1.047 seconds in aggregate, about 1,488 ns per observation. Serialization
took 7.154 ms and file output 22.052 ms in aggregate. The longest observed replay
batch was 5.134 ms; the largest single output operation was 0.308 ms.

One lane reached 255 headers and 201,632 payload bytes, within the new independent
2,048-header / 256-KiB limits. Its simultaneous header count exceeds the former
128-record lane capacity. This is useful native burst evidence; it is not a
controlled before/after replay of the old implementation or total hook CPU cost.
The recording does not measure stream FPS, GPU time or total SteamVR overhead.
The separate [throughput fixture](PASSIVE-BINDING-THROUGHPUT.md) supplies controlled
software comparisons and still documents its extreme-startup limit.

## Manual restoration and cleanup

After the maintainer confirmed manual rollback, the helper removed activation
files at **22:49:05 CEST**, requested native SteamVR restart at 22:49:06 and
completed recovery at **22:49:10**. The original compositor fingerprint remained
`9b1bd5fe4463592cbc71b2517b9ceb0a0fcd66cd6885a19dca4be0784bf6c265`.
Native compositor PID 710607 had no diagnostic mapping or diagnostic environment.

After verifying copied evidence, closed-writer locks and the exact owned inventory,
cleanup removed only `/home/steamos/.local/share/frame-mask-trace` at 22:50:09.
The 22:50:10 final check verified that directory, the owned implicit-layer
manifest and the owned runtime service drop-in were absent; SteamVR was active
and the rollback watchdog inactive. Cleanup caused no additional restart.
The maintainer was told the Frame could enter standby. No diagnostic installation
remains on the headset.

The procedure remains [the temporary session and rollback contract](FRAME-TRACE-SESSION.md).
Any new activation needs a reviewed concrete scope and restart readiness. This
successful public-metadata capture does not authorize private metadata hooks,
mapped-memory reads or a replacement shader.
