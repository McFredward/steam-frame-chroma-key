# First complete native pixel snapshot, 2026-10-15

## Actual result

The maintainer approved the corrected package and one manual SteamVR restart,
then confirmed the GloomhavenVR build676 test pattern in both eyes and normal
headset operation. The exact current native providers satisfied all library
imports; an isolated immediate load against those mapped providers passed.
Startup passed the existing native UI health gate before the stream request.

The corrected ARM layer (`def9445f8ec5690150026a9b35e782e424efe0b61bc13dc90dd067f5efdef5e4`,
229,232 bytes) no longer refused ordinary parallel metadata observations. Its
actual event chain was `prepared`, `armed`, `commands_recorded`, `complete`.
The [causally reproduced lock repair](PIXEL-TRACKER-CONCURRENCY-20261015.md)
therefore progressed beyond the earlier pre-arm refusals on the actual device.
No native Steam PC sender modification or shader replacement was introduced.

One immutable P010 artifact was saved:

| Property | Recorded value |
| --- | --- |
| Capture ID | `f6267827c7462ea93972a012278288e5` |
| Extent / format | 1152 × 4608 / Vulkan format1000156013 |
| Raw bytes | 15,925,248 |
| Raw SHA-256 | `1eb48d4cff6480c5983193558a9b3711ec061d05ddab554e3f413fc2fba3a850` |
| Metadata bytes | 1,251 |
| Metadata SHA-256 | `410ec9f619478902c859ff4e689010a5d93ed3e1988b6cd12c127aedcfd36670` |
| Source image / view generations | 2616557 / 2616559 |
| Native command generation | 31088 |
| Observed conversion | RGB_IDENTITY, full range, identity components, midpoint / linear |

The native command buffer was reusable (zero begin flags). Readback recorded
owned-fence completion **and** retirement of that exact command generation
before publishing the artifact. This is one immutable saved result, not a
claim that the native recording executed only once. Whole-file and both plane
digests were independently recomputed after transfer and match the metadata.
The tight plane sizes and P010 padding also pass offline validation.

The multi-source SCP invocation returned1 after copying the complete raw file.
Its exit status was not accepted as success. The small metadata file was then
copied exactly through a bounded read-only SSH operation; both final local
files were checked against the device's byte lengths and SHA-256 values.

## Restoration

After a separate manual rollback approval, the exact activation manifest,
config and arm token were removed. SteamVR was restarted once in its original
configuration. A fresh native UI health invocation returned `ready`; the full
same-user map inspection, including authorized protected-map reads, found no
owned or foreign diagnostic mapping. Run-library removal completed with no
errors. The recorded image and small inactive package/evidence files remain.
The maintainer was explicitly told standby was safe. There was no automatic
rollback, restart or test expiry.

## What is established, and what remains open

This establishes actual native GPU pixel acquisition from a tracked sampled
P010 image and complete removal of the temporary diagnostic afterward. Source
pattern visibility is separately confirmed by the maintainer. It does not yet
join the acquired numerical samples to a recovered source payload.

The stored-Y grayscale diagnostic contains structured but discontinuous regions,
rather than an immediately usable uniform full-frame image of the source canvas.
Its interpretation must be established from the original source headers and
actual native sampling layout. RGB_IDENTITY is not evidence for a guessed
BT.709 transform; the grayscale preview explicitly makes no RGB claim.

Still required: validate the trusted source session/content and repeated CRC
headers in the actual samples, establish native frame/eye/UV/foveation mapping,
and then bind real game coverage without altering native reprojection. Static
header matching alone cannot prove arbitrary gameplay frame association. Missing
association must remain opaque. Streamed genuine-mask passthrough is not enabled.

Private evidence: `build/private-concurrency-run-20261015/`. This contains native
provider/load/startup, arm/progress, exact local image and metadata, transfer
verification, independent evidence review and complete rollback records. Native
binaries, shader data and the capture are not redistributed in this repository.
