# Next receiver mapping proof, 2026-10-16

The [successful sampler capture](PIXEL-SAMPLER-CAPTURE-20261016.md) establishes
static source identity and measured grayscale coverage carriage. It does not
establish the native eye/atlas/reprojection/foveation mapping. The next evidence
must use the actual native draw's GPU-consumed parameters, not fitted screenshot
coordinates or a later CPU read of mutable uniform memory.

## Existing observations and missing facts

The pre-companion public-API binding tracker already retained object generations,
descriptor revisions, effective dynamic offsets and buffer ranges. However,
the pixel candidate keeps only image binding 9 and selects the first matching
draw in a render scope. Its pipeline admission identifies the fragment but
accepts any ordinary vertex stage. It does not preserve uniform bytes, prove
their GPU consumption or identify the output's physical eye.

A private, fail-closed static audit of the exact original shader pair now
confirms that all stores target ordinary shader outputs (six vertex, one
fragment). Both originals validate for Vulkan 1.1; there are no resource writes,
atomics, image writes, memory copies, function calls or unknown pointer targets.
This only establishes those modules' behavior. The fragment output can still
alias a UBO through an attachment allocation, and live stage selection, other
draws/queues and source ownership remain independent checks. Private shader
instructions and disassemblies are not tracked or retained by the audit.

Historical October 11 records show feasible transfer-source uniform buffers:
bindings 0/4 use 272-byte ranges, 2/6 use 2,800-byte ranges, and 3 uses 400 bytes.
Five separate slices total 6,544 bytes. Binding 3 differs between the two
historical draws; do not deduplicate by binding number. These historical sizes,
usage bits, offsets and aliases are not fresh admission for another capture.

## Inert local foundations

The first candidate-contract helper and one-shot GPU-copy helper checkpoint
contained preparatory components only. Those independently tested helpers were
not linked into the previously installed pixel diagnostic. That checkpoint did
not enable extra native reads or authorize another device experiment.

The candidate helper compares selected and reobserved bounded metadata, requires
both known shader stages and complete, aligned, generation-bearing native UBO
slices, and refuses missing transfer usage rather than adding it. Reobserving
the same stale snapshot is not a lifetime proof. Buffer sharing mode, queue
ownership, alias exclusion and the actual final-draw relation need additional
live-tracker work before a candidate can authorize any native read.

The copy helper prepares only original public Vulkan function pointers and a
borrowed owned output buffer. Admission seals bounded source slices and disjoint
output ranges. Recording orders reads against preceding writes, copies each
slice and orders completion before following native work and eventual host
publication. A fence and exact reusable-command retirement remain separate
caller requirements. A synthetic GPU copy/overwrite test proves that operation's
ordering; it does not prove native draw or source-frame association.

Focused validation passes 1,261 metadata-contract checks normally and under
ASan/UBSan, 193 copy-recorder checks normally and under ASan/UBSan, and 33 actual
Intel GPU sequencing checks. Strict AArch64 syntax checks pass. The new CMake
registration also builds and runs these three new tests together successfully.
Earlier unchanged local suites are inherited rather than replayed; this is not
a new complete mod gate or Frame GPU validation. Small reports remain under
`build/private-uniform-{contract,copy,integration}-20261016/`; generated test
executables, CMake caches and completed worker worktrees are removed after use.

## Optional integrated diagnostic

`FRAME_PIXEL_UNIFORM_COMPANION=ON` now builds an explicit, uninstalled diagnostic
variant; it is off by default and cannot be selected without the pixel-snapshot
target. It does not replace a native shader or add a PC sender hook.

The tracker pins both original stages and retains a complete snapshot for the
last supported, nonempty direct draw. Every later draw, including a zero-count
call, invalidates that selection before the old first-image fast path. A later
supported final draw may select again. Render-scope end reobserves the current
pipeline, descriptor revisions, effective slices and image/view generations.
Legal shader-module destruction after pipeline creation does not invalidate the
retained pipeline's original stage facts. Unknown routes still refuse.

The live resource guard requires observed EXCLUSIVE buffers and ordinary current
allocations. Repeated references to the exact same buffer are allowed. Every
other buffer or image in that allocation is conservatively refused, even if its
logical byte ranges appear disjoint: the existing graph has no complete native
memory-requirements spans with which to prove nonaliasing. It never substitutes
logical buffer size for a Vulkan memory requirement. Queue ownership relies on
valid original Vulkan use of this same native draw/queue and the EXCLUSIVE
resources, not on an independently observed queue-transfer journal.

After the unchanged original render-scope end, the one winning recording appends
the existing raw copy and native sampler, then five bounded buffer copies to
unused tail space in the same owned staging buffer. Actual physical-device UBO
limits are queried at cold creation. No native flags, descriptors, pipelines or
host mappings are changed. Background publication requires the existing owned
fence and exact reusable-generation retirement. The optional fourth file is
`uniform-bytes.bin`, at most20,480 bytes; `native_uniform_control` in the bounded
snapshot metadata records each source range, stage, revision and SHA256.

The offline reader checks the production file contract and parses raw selectors,
the declaration-derived matrix columns, patch fields and raw integer flag bits.
It explicitly retains false physical-eye, video-frame, spatial-map and
passthrough authority. Its eight guard booleans are validated producer assertions,
not independent evidence that the original runtime mapping has been reproduced.
It validates uniform bytes, not the two large paired pixel files; their original
validation remains separate.

Focused new evidence includes118 live-resource controls,541 optional final-draw
controls and81 production-formatter controls, each normal and ASan/UBSan, plus
strict AArch64 checks. Four C++-to-Python bridge tests verify the actual emitted
6,544-byte synthetic file through the strict reader. The complete original5,391
tracker controls passed before the later optional-only refusal-label repair;
they were not unnecessarily repeated afterward. The real Vulkan loader passes
14 default-variant cases and two enabled-companion query cases; the latter also
report cold uniform preparation ready. Neither loader fixture contains the
original native distortion draw or proves an actual native uniform acquisition.

The final CMake metadata/reader integration passes separately. The actual ARM
library has exactly three private loader exports, no TLS segment or Vulkan
dependency, and NOW/RELRO. Sources, composed test evidence and independent
source/ELF/activation reviews remain under ignored
`build/private-uniform-{resources,native-tracker,reader,metadata,runtime}-20261016/`.
This is bounded fork validation, not a repeated complete GloomhavenVR gate or a
headset result. No Frame installation, connection or restart occurs during it.

Native integration must satisfy the following boundaries before deployment:

1. Pin the actual VS_DISTORT/PS_DISTORT pair, entries, byte sizes and generations.
   The version-specific original stages must be read-only for the target
   uniform memory. Unknown stages, specialization or unobserved routes refuse.
2. Select an actual final draw in its native render scope. Any later draw,
   subpass/secondary execution or unknown write route invalidates the companion
   candidate. Never attribute renderpass-end bytes to an earlier first draw.
3. Preserve all five original uniform bindings plus image/view/conversion and
   pipeline/command/set revisions. At the injection boundary, recheck actual
   buffer/allocation generations, bounds, queue sharing/ownership, and relevant
   memory aliases, including renderpass-end attachment writes. Descriptor
   identity alone cannot exclude legal intermediate writes or external producers.
4. Require freshly observed TRANSFER_SRC on every original source buffer, copy
   only its exact effective descriptor slice, and use explicitly bounded unused
   tail space in the owned staging allocation. Never change a native buffer's
   flags, bind a uniform-only buffer as storage, or inspect its host mapping.
5. Preserve source-read ordering and native graphics state. Publish copied bytes
   only after the existing owned fence and exact reusable-generation retirement.
   Include draw ordinal, shader identities, set revisions, original effective
   ranges and all source/output hashes. Missing facts must cause a precise
   refusal, not a fabricated coherence flag.

The first capture should establish one exact draw. A later bounded two-slot
capture can establish both native selector values independently. A selector's
integer value or a recovered source eye label is not yet its physical-eye route.

## What to prove from the bytes

Use the [version-specific declaration-derived layout](FRAME-UV-BINDING-20261011.md):
eye selectors, actual metadata selection K, both R0/R1 rectangles, patch origin
and reconstruction flags. Validate bounds, finite/nondegenerate values and the
actual VS/PS relation. Reproduce both original lookup paths and their blend
against asymmetric source coordinates, including the transition between regions.
One normalized coordinate or one original sample cannot replace this work.

Capture output-layer/viewport/framebuffer provenance separately to establish
physical eyes. Preserve those facts through actual native distortion,
foveation and reprojection. None of the preparatory components asserts them.

## Remaining app-to-stream boundary

Even coherent receiver parameters do not prove app-to-PC-compositor registration.
Color and coverage occupy different rays of the current app canvas. Under
nonlinear composition, adding a constant packed-image tile displacement cannot
prove that both samples represent the same scene point. The subsequent app-only
experiment needs asymmetric coordinate/coverage patterns and changing app-owned
session/frame/eye identity, with one immutable source generation per pair.

This approach continues to avoid a native Steam PC sender modification. Unknown
composition, crop, extrapolation, identity or mapping retains opaque rendering.
No live alpha composition, real scene coverage, translucent-edge correctness or
visible passthrough result is established by these foundations. A new Frame
install/restart requires a separately ready, reviewed package and fresh agreement;
no automatic expiry, rollback or restart is introduced.
