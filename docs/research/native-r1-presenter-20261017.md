# Inert output candidate using the current original R1

`tools/native_fragment_r1_presenter.py` creates a private, offline fragment
candidate from the exact retained original PS profile. It installs nothing,
activates no native pipeline, and supplies no live source-validity guard. All
frame, physical-eye, native-mapping and passthrough authorities remain false.

The caller declares two disjoint, positive axis-aligned panels inside a unit
synthetic canvas, straight RGB, and an independently owned linear numeric
coverage source. The strict schema accepts no host `B`, `W`, copied `R1`, or eye
selection. The implementation uses the existing atlas transform with identity
`W`: the original fragment's normalized `q883` is mapped into each panel and
then through **that invocation's** original `R1`, original reconstruction and
sampler. It adds no native UBO/sampler loads or descriptor bindings.

```text
q = original normalized coordinate from current u/R1
uc = current R1.min + current R1.span * Tc(q)
ua = current R1.min + current R1.span * Ta(q)
rgb = nativeDecodedColor(uc) * originalAperture * originalTint * coverage(ua)
a = coverage(ua) * originalAperture
```

The two reads retain independent native reconstruction weights. Coverage is
clamped raw sampled component-G of the explicitly owned synthetic source, not
decoded scene green or a key color. Output is one premultiplied RGBA contribution;
zero coverage contributes zero RGB. This first profile does not support additive
emission, a general premultiplied source, refraction, or proven game-wide alpha.
It does not establish a linear transfer through native P010/video conversion.

Enabled output carries an explicit `OpModuleProcessed` marker beginning
`MRMASK/`, so the existing legacy override guard refuses it. The original module
is SPIR-V 1.0; this debug instruction requires 1.1. The tool explicitly changes
the enabled header to 1.1, records both versions, and validates against Vulkan
1.1. Do not describe the enabled module as the identical original, strip its
marker, or install it through the legacy override path. Disabled output is the
exact original byte sequence, including its original version and interface.

The default count1 variant uses the existing bounded **owned** B9 interface
specialization. It proves all original descriptor0 accesses and creates fresh
constant/array/pointer IDs; it never changes the shared original length constant,
an executable access, a descriptor decoration, or a native descriptor layout.
An explicit count3 variant remains available for the independent owned oracle.
Neither variant carries an SSBO or an extra descriptor.

Example mapping (owned synthetic diagnostic only):

```json
{
  "schema": "native-fragment-r1-presenter-mapping-v1",
  "scope": "INERT_OFFLINE_CURRENT_R1_SYNTHETIC_SOURCE_ONLY",
  "chart": "ORIGINAL_CURRENT_R1_NORMALIZED_CANVAS",
  "source_encoding": "OWNED_LINEAR_RGBA32F_COMPONENT_G_COVERAGE",
  "rgb_convention": "straight",
  "color": [0.4, 0, 0.03, 0, 0.6, 0.2],
  "coverage": [0.4, 0, 0.56, 0, 0.6, 0.2]
}
```

```sh
python3 -B tools/native_fragment_r1_presenter.py \
  --original /private/exact-original.spv --mapping /private/mapping.json \
  --output /private/new-candidate.spv --record /private/new-candidate.json
FRAME_PRIVATE_ORIGINAL_PS=/private/exact-original.spv \
  python3 -B tests/test_native_fragment_r1_presenter.py -v
```

Four public methods cover the schema, panel domain, switches and marker/version
framing. Six optional private methods cover current-coordinate dataflow, single
output, unchanged native descriptor reads/layout, fresh count1 interface IDs,
exact OFF, exclusive validated CLI publication, and actual C++ legacy-guard
call-through. Three deliberately wrong, still valid SPIR-V candidates (wrong
chart, shared panel weight and double aperture) fail their corresponding
independent structural controls. Those controls establish dataflow, not a GPU
image result; independent host-GPU comparisons are a separate integration lane.

The first local run passed nine methods but exposed a test-mutator assumption:
the original result-ID bound is 1010, not the hardcoded 1013. The mutant therefore
had not altered the intended instructions. The repair derives the bound from the
original header, and only that affected method was rerun successfully. No
production transform or previously passing method changed. Failure evidence is
retained, and the focused result is explicitly composed rather than advertised
as a fresh full old-suite replay.

Before live use, a separate adapter still needs current-source/mapping admission,
all matched native draws, an alpha-consuming native presenter, reusable-recording
lifetimes and a real OFF/invalid-source path for already recorded commands. A
CPU toggle or an old successful header is insufficient. Physical eye names do
not need to be inferred if the admitted implementation preserves the native
per-invocation route for both source panels.

Private source pins, bounded logs and cleanup receipts are retained at
`build/private-r1-presenter-worker-20261017/`. Generated shaders and compiler
outputs remain private temporary files, removed after their child processes
finish; no Frame or sender is modified by these checks.
