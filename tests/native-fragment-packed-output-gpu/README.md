# Offline packed-opacity output integration

This fixture closes the **owned offscreen output** end of the explicit-opacity
chain. It does not install a layer, connect to a headset, activate a native shader
override, authorize passthrough, or assert native frame/eye/UV bindings.

`tools/native_fragment_packed_output.py` reuses the exact privately supplied
native shader and the existing `native_fragment_atlas_probe.py` dependency-cone
transformer. Both native sampled reconstructions retain their own weights and
same-invocation metadata. Explicit caller-owned `B`, color and coverage maps
retarget the original packed coordinate directly. The generated fragment shader
has exactly one color output at location 0. The caller must explicitly declare
one of two source conventions:

| Owned source RGB convention | Output RGB | Output alpha |
| --- | --- | --- |
| `straight` | decoded color × native tint × linear coverage × original aperture | linear coverage × original aperture |
| `premultiplied` | decoded color × native tint × original aperture | linear coverage × original aperture |

`premultiplied` does not multiply RGB by coverage a second time. Nonzero RGB
at alpha zero is retained for additive/emissive source content. The convention
is an explicit owned-input contract, never an inference from the sampled color.

Coverage is clamped raw component-G from an **explicitly owned linear RGBA32F
source**, independent of scene RGB; it is not inferred from a key color or decoded
scene green. The tool refuses other source-convention declarations. This caller
contract is deliberately not evidence that native video samples carry linear
game opacity. Disabled mode returns the original shader bytes exactly.

The fixture reuses the approved Vulkan allocator/harness, replacing its test
render pass and pipeline with actual single-attachment equivalents. The source
sampler is explicit nearest/clamp on owned 16×16 RGBA32F texels. Test inputs cover
both eyes with asymmetric metadata, both native decoder branches, enabled and
disabled inner reconstruction, independent color/coverage weights, and partial
original apertures. The expected output is independently calculated, then also
checked against separate executions of the unchanged native fragment shader.

For overlapping transparency, two owned transparent layers are actually rendered
on the GPU using ordinary over blending. Their resulting premultiplied RGB and
coverage are verified against independent analytic expectations. The GPU result
is converted to straight color and encoded into the owned packed color/coverage
source before the composed output is rendered. An opaque black source, and the
same nonblack color at zero and partial coverage, distinguish opacity from key
color. This is an integration proof for explicit input encoding, not a video
transfer or native source-rendering proof.

With `--rgb-convention premultiplied`, the same fixture also actually renders
additive emission over transparent alpha zero, additive emission over overlapping
glass, and glass over emission. The original premultiplied GPU RGB is encoded
without dividing by alpha. Independent analytic blend equations check the
producer and expected final output; separate unchanged-original native shader
executions check decoded RGB, tint and original aperture. Both the ordinary
count3 interface and the bounded owned count1 specialization exercise these
cases. A causal mutant that incorrectly multiplies premultiplied RGB by coverage
again must fail the RGB comparison, including cases that would erase emission.

Original → enabled → original output must recover bit for bit. Disabled output
must match unchanged original output bit for bit. Positive cases require zero
Khronos validation and synchronization errors. Mutants that omit RGB coverage or
apply the aperture twice must fail the corresponding output comparison.

The valid owned descriptor layout has sampled-image binding 9 count **3**, as
required by the pinned shader's declared array. A separate count **1** negative
stops after pipeline creation, before any draw: both original and composed
modules must produce `VUID-VkGraphicsPipelineCreateInfo-layout-07991`. Constant
index 0 accesses do **not** establish a legal count-1 layout. An observed native
count-1 descriptor therefore remains an unresolved profile/binding discrepancy;
this fixture never silently expands or rewrites native layout state.

A distinct enabled-only `--owned-descriptor-count 1` output variant specializes
the **owned clone** interface. It first proves the exact original hash, original
array/pointer/variable declarations, set/binding decorations, and exhaustive two
constant-index-0 accesses. It adds a fresh constant1, image-array type and pointer
type, changing only the B9 variable type; all executable image access instructions
remain unchanged. GPU mode `specialized_count1` compares this valid count1 output
against a separate unchanged-original count3 Vulkan instance. Disabled mode still
returns the original bytes and requires the original count3 interface. This proves
a bounded owned shader-interface adapter, not a live native binding or permission
to substitute the shader in a compositor.

Run with a privately retained exact original shader and a locally available
Khronos validation layer (no Valve modules are distributed):

```sh
FRAME_PRIVATE_ORIGINAL_PS=/private/exact-original.spv \
  python3 -B tests/test_native_fragment_packed_output.py
python3 -B tests/native-fragment-packed-output-gpu/run.py \
  --original /private/exact-original.spv --record /private/new-proof.json
# New premultiplied-only integration cases; no old straight GPU suite is repeated.
python3 -B tests/native-fragment-packed-output-gpu/run.py \
  --original /private/exact-original.spv --record /private/premultiplied-proof.json \
  --rgb-convention premultiplied --mode enabled --mode disabled \
  --mode specialized_count1 --mode double_premultiply
```

`--driver software|intel|both` selects owned host devices. Repeatable `--mode`
selects a bounded subset after a limited repair. Generated modules, fixture
executables, source inputs and layer logs live in a temporary private directory
that is removed after processes complete. The JSON proof retains source hashes,
case/oracle facts, complete validation messages, commands and results.

Before any live output activation, separately establish: the exact native
shader/layout profile (including the count-1 discrepancy), the actual source
sampler/conversion and linear coverage transfer, native packed chart B, physical
eye and exact source-frame association, safe original native state/lifetime
restoration, and a native passthrough presenter accepting this premultiplied
output. This offline proof supplies none of those authorities.
