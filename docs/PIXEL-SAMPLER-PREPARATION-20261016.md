# Same-image sampler comparison preparation, 2026-10-16

## Reason for this experiment

The [first complete native snapshot](PIXEL-SNAPSHOT-CAPTURE-20261015.md) is secured
and its temporary Frame installation has been completely removed. Both source
PNGs recover their three repeated CRC-protected headers in positive controls.
The captured stored components recover none under the bounded tested mappings.
Visible discontinuities cluster on block boundaries; simple tile reorders did
not improve them. No source header, eye mapping or dynamic frame association is
therefore established.

Historical public API metadata identifies a Qualcomm compressed DRM image and
physical plane pitches consistent with Venus TP10 UBWC packing. This does not
establish the current image's modifier or a driver defect. Physical memory
pitches must not replace the logical tight P010 `CopyImageToBuffer` strides.
The next diagnostic compares that original plane copy with sampling through a
matching YCbCr sampler from the **same native view and command generation**.

## Bounded implementation

The optional diagnostic reserves two disjoint regions in the existing one
64-MiB staging allocation: tight P010 uses `3 * width * height` bytes and the
sampler control uses `4 * width * height` bytes. Both must fit before recording
any command. For the previous 1152x4608 extent, their combined output would be
37,158,912 bytes. There is no second image allocation or second resource lease.

The original, source-generated compute shader samples logical texel centers at
LOD0. It writes RGBA8_UNORM **RGB_IDENTITY components**, not reconstructed scene
RGB, after the raw region. Shader, pipeline, conversion, immutable sampler,
descriptor pool/set and buffer are prepared cold. Only the successful raw-copy
selection owner updates its private set once and records compute. No native
graphics state, push constants, shader, reprojection or source image is changed.

Live admission requires the actually enabled YCbCr feature, a compute-capable
native queue, a complete current converted full2D view, the exact native
RGB_IDENTITY/FULL/identity/MIDPOINT/LINEAR conversion profile and no previously
observed compute state in that recording. Cold public queries establish exact
tiling/modifier features, extent/sample limits and the descriptor requirement.
No driver query or descriptor allocation is performed in a render callback.

The finite API envelope is generated from pinned public Vulkan-Headers revision
`c46850864f4661461b0f6cb9922c058ffea4915e`. Unknown extensions/types and unobserved
binding/dispatch/query-begin routes are refused. Conditional rendering and
inherited command state are excluded. Any actual query begin permanently blocks
sampling in that recording; an enabled but unused statistics feature is valid
only with complete query-route observation and a clean recording history.
Inserted compute reads are ordered before all later native GPU commands;
separate host-read visibility remains intact. Ordinary native creation and
forwarding are unchanged when this optional comparison is unavailable.

Both outputs become readable only after the same owned fence and, for reusable
recordings, exact successful command-generation retirement. The apparent
descriptor-update/replay lifetime counterexample was independently rejected:
admitted layouts exclude the update-after-bind exemptions, and updating an
ordinary bound descriptor invalidates its recorded command buffer. Destruction
of referenced objects likewise invalidates it. These are valid-native-API
assumptions, not permission to relax the generation or retirement guards.
See the [descriptor update rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html)
and [command-buffer lifecycle rules](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html#commandbuffers-lifecycle).

The writer records bounded paired hashes, capture/image/view/conversion/command
identities, logical extent and actual native image tiling/modifier/plane metadata.
The offline paired reader checks these facts before emitting component previews
or comparison statistics. Missing, mismatched or stale pairs are rejected.

## Validation and limits

Focused evidence composes existing unchanged checks with the affected repairs:

- Readback: 200 controls and 33 actual Intel GPU checks.
- Sampler: 314 normal and 314 ASan/UBSan controls; the old HOST-only barrier
  mutant fails the added ordering regression control.
- Exact format admission: 485 controls normally and under ASan/UBSan.
- Device admission: the original48 controls are superseded by180 focused
  controls normally and under ASan/UBSan, including exact first-refusal reasons
  and explicit query-route coverage. Six API envelope tests cover unobserved
  current/future and feature-only routes.
- Original generated shader: three provenance/validation tests; 246 actual Intel
  RGBA checks and 247 software-Vulkan P010 checks with an expressly different,
  fixture-only supported COSITED_EVEN/NEAREST profile. Production rejects that
  alternate profile. The local GPUs do not support the exact native
  MIDPOINT/LINEAR P010 profile; no native-profile GPU pass is claimed.
- Integrated original-loader forwarding/concurrency smoke and strict host/ARM
  builds. This fixture does not issue the pinned native distortion draw.
- Thirteen paired-reader cases are covered by the focused initial pass and the
  two affected reusable-retirement controls. They include exact integrity,
  generations, missing retirement/fence, code differences, trusted-header
  recovery, input links and output collision refusals. The earlier unpaired
  actual capture is correctly refused as a pair before its raw data is read.
- Independent final source review found no remaining known blocker after the
  targeted route, query and GPU-ordering repairs.

Private records are under `build/private-sampler-prep-20261016/`,
`build/private-sampler-validation-20261016/`,
`build/private-sampler-barrier-repair-20261016/` and
`build/private-sampler-arm-20261016/`. Owned test processes are reaped and
regenerable caches removed; source, small receipts and current deliverables
remain. No complete GloomhavenVR suite or new mod DLL/build is required for this
fork-only diagnostic. PC source build676 remains unchanged.

## Next device run

The first package was subsequently installed, refused cold admission and was
[completely restored](PIXEL-SAMPLER-ATTEMPT-20261016.md). The query-history and
exact-reason repair subsequently completed a [real paired capture and strict
static source recovery](PIXEL-SAMPLER-CAPTURE-20261016.md), followed by verified
restoration. The procedure below records that experiment's approved scope;
any future device run requires a fresh agreement. Request fresh readiness
and approval only after the complete local package, rollback, review and hosted
checks are ready. The new scope includes the paired component output. Exact
current native-provider imports/isolated loading and native UI health must pass;
`prepared.sampler_ready` must be true before requesting the PC-VR stream. A
refusal remains a refusal and does not authorize a relaxed profile.

After the maintainer confirms the pattern in both eyes, arm one bounded saved
pair. Transfer and independently verify **all three files** (`snapshot.p010`,
`sampler-components.rgba8`, `snapshot.json`) before requesting manual rollback.
Check the paired offline result without guessing UV transforms. No automatic
expiry, rollback or restart is introduced. The Steam PC sender stays untouched.
Frame/eye/foveation binding, real game coverage and working passthrough remain
separate later milestones; an interpretable static sample does not establish
dynamic source-video frame association.

Offline inspection after exact transfer, using a new private output directory:

```sh
python3 -B tools/pixel_sampler_recovery.py \
  --snapshot /private/run/snapshot.p010 \
  --sampler /private/run/sampler-components.rgba8 \
  --metadata /private/run/snapshot.json \
  --png /private/run/sampler-components.png \
  --green-png /private/run/sampler-green.png \
  --record /private/run/sampler-validation.json
```

The optional header stage additionally requires the exact original source PNG,
trusted source manifest, explicit tested mapping and expected session/content/eye.
It never infers those expectations from the captured image itself.
