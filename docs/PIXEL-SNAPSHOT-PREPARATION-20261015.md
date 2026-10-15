# Decoded image snapshot preparation, 2026-10-15

## Current evidence and next question

GloomhavenVR build676's application-owned diagnostic was observed in both eyes
on the Frame, and opening the menu restored the game immediately. Its PC logs
also show both eye outputs without fallback. This proves visible carriage of a
static test pattern. It does not establish numeric decoded samples, a native
video-frame identity, real game coverage or a usable passthrough mapping.

The next experiment is one immutable P010 snapshot of the live sampled image
selected by the already observed `PS_DISTORT` fragment fingerprint. No native PC
Steam sender modification is added. The new layer changes no shader or rendered
pixel, and does not enable passthrough. It adds a bounded GPU readback operation.
The actual Frame pixel-capture scope has **not** been authorized or executed.

## Production acquisition path

`FRAME_MASK_BUILD_PIXEL_SNAPSHOT=ON` builds the uninstalled
`libVkLayer_frame_pixel_snapshot.so`. It is an ordinary public Vulkan implicit
layer with three private loader entry exports. It has no native executable
detour, preload/audit library, initial-exec TLS, private memory observer, socket,
or render-thread file I/O. Only a process named `vrcompositor` with an exact
private config and a fresh exclusive launch claim enables capture tracking.

The synchronous existing binding Core identifies the current full P010 view and
its exact object generations at a supported direct distortion draw. Ordinary
exclusive sources have sampled/transfer-source usage only, one mip/layer, no
protected/disjoint/sparse/alias flags and a bounded even extent. Valid native
Vulkan use in the same primary rendering scope is the layout/queue-ownership
precondition; this is not an external decoder resource lease or frame identity.
Unsupported binding routes and tracking loss refuse acquisition.

Immediately after the original rendering scope ends, before following native
commands can release the image, the readback transitions to transfer-source,
copies both planes and restores the original shader-read layout. Dependencies
cover all command stages, including vertex sampling. The native submit waits,
signals, fence and return value are unchanged. An owned zero-submit fence follows
the exact original submission on the same externally synchronized queue.

The original compositor uses reusable, zero-flag command buffers. Its recorded
copy may therefore execute again until that old generation is retired. **Only
one final artifact is saved; there is no claim of one GPU execution.** CPU
invalidation, hashing and file output wait for both owned fence completion and
the exact successful native reset/begin/free/pool retirement. Immutable pre-call
command/pool generations prevent a later reuse of the same handle from completing
or retiring the selected old recording. A discarded recording cannot publish an
unwritten buffer. Storage remains owned until joined device teardown establishes
GPU quiescence; failed quiescence is never a successful release proof.

The staging budget is 64 MiB, allocated cold before arming; admitted output is
also limited to 12 million pixels and 8192 per axis to match the offline PNG
reader. The previous native 1152×4608 extent uses 15,925,248 raw bytes. Background
status output contains a few lifecycle events, not per-frame samples.

## Reversible session and next device preflight

See [the exact file staging and rollback sequence](pixel-snapshot-session.md).
Preparation and its sandbox tests never access the Frame. A new permission is
required before any new Frame staging, pixel acquisition or runtime restart.
There is no automatic expiry, rollback or restart.

Before activation, verify the exact ARM64 artifact/hash and its public runtime
imports against the current device; perform a cold isolated `RTLD_NOW` library
load against its exact currently mapped public C++ provider (not only its
`LD_LIBRARY_PATH`, which cannot reproduce native RPATH/already-loaded-provider
precedence). Verify that provider's identity and required exported versions
before explicitly loading its exact path in the isolated preflight. The previous
Frame libc is 2.39, but that alone does not verify the current C++ provider or
process namespace. Do not install a manifest if this preflight fails.

Take a fresh `NativeEvidenceSource` baseline before the agreed SteamVR restart
and run its native UI health gate afterward. A PID or a live SSH connection is
insufficient. Only then request a PC VR stream, ask for the build676 pattern to
be visible, and arm the prepared one-shot run. At completion collect the two
snapshot files/status, remove exact activation files, and perform the separately
agreed manual restart and health verification. Preserve all capture evidence;
remove the owned library only once it is no longer mapped.

## Offline interpretation and limits

`tools/pixel_payload_recovery.py` validates whole-file and plane digests, tight
P010 offsets/extent, generation metadata and 10-bit padding. The observed Vulkan
conversion is `RGB_IDENTITY`, not evidence for a guessed BT.709 conversion. Use
`--luma-diagnostic` for normalized stored-Y grayscale. It does not claim RGB
conversion. Unknown conversion values stay explicit rather than being guessed.

Optional recovery of all three CRC-checked source headers requires the trusted
original manifest/PNG, explicit expected session/content/eye and an explicit
pixel-center mapping. It never auto-fits a warp or infers eye placement. Static
header recovery still cannot authorize arbitrary gameplay frames: native frame
identity, UV/foveation mapping, actual scene coverage and integration into the
existing mod setting remain subsequent work. Every missing association stays
opaque.

## Validation scope

Retained ignored evidence: `build/private-pixel-prep-20261015/`.

- 186 readback controls and 33 real Intel GPU checks pass after the all-stage
  synchronization repair, including actual P010 planes, two native executions,
  original-fence/source preservation and retirement after native reset.
- 14 real Vulkan-loader cases pass on the final host layer: normal native
  query/image/queue/reset/teardown behavior, invalid config/link refusal,
  unrelated-process inactivity, stale claims and armed unmatched-draw refusal.
- 129 causal selection controls pass against the production helper, covering
  discard/reset/new-submit and delayed original-submit return races.
- Reuse unchanged evidence for 826 normal/sanitized live-tracker controls,
  20 offline recovery tests, 23 session sandbox tests and the private I/O test.
  The affected existing binding-Core test also passes.
- The exact production sources cross-compile for ARM64 with strict warnings;
  independent source and package reviews are retained separately.

These are composed focused checks, not another complete fork/mod gate. There
is no positive native compositor readback, headset pixel result, current device
deployment or working streamed passthrough result in this preparation.
