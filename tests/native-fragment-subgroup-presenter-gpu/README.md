# Offline subgroup guard proof

This is an optional generator and owned GPU experiment. It installs nothing and
does not change the existing guarded presenter, receiver, CMake or Frame. The
small measured gain does not presently justify native feature-chain changes.

`run.py` privately borrows the existing guarded/SSA Vulkan fixture, renaming only
its unused entry point in a temporary header. It pins those inputs in its report.
Original private shader bytes must be supplied explicitly and are never embedded
in tracked files. All generated modules are checked by `spirv-val`.

The actual owned test device enables
`VK_KHR_shader_subgroup_uniform_control_flow` and
`VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR` only after checking
the extension, feature and Fragment BASIC/BALLOT/SHUFFLE properties. Unsupported
Lavapipe is an explicit refusal, not a second GPU correctness pass.

The Intel pass compares original, old guard and new guard using one float output.
It reuses recorded commands while changing the same source ON/OFF/ON. Ninety-one
source/corruption/geometry cases run at 1×1, then across an odd 17×19 triangular
raster, and across that raster with an **owned test mutation** alternating the
original metadata selector in adjacent columns. The same mutation is applied to
all three oracle modules. It is absent from the production generator. Mixed R1
therefore exercises different validity within an actual subgroup, rather than
merely dispatching uniform cases one at a time.

An extra diagnostic output counts shared and per-invocation fallback pixels. A
separate QUAD-capable GLSL witness observes helper invocations at the same raster
boundary. This establishes that the owned geometry exercises helper boundaries;
it is not a count or claim about the native compositor's helper distribution.
The witness needs QUAD only for the test; the candidate does not.

The timing mode measures Original / old guard / subgroup guard, each with valid
and first-cell-OFF content, at 512² then 1152×4608. It uses one warm draw and three
samples. GPU timestamps cover the pass and attachment transition; CPU fence time
also includes a full float-image readback, so it must not be compared to older
one-pixel CPU measurements. Validation is off only for timing. This is an Intel
host experiment, not Frame latency or throughput. Concurrent unrelated host test
work was reported during the retained run; clocks, ordering and compilation also
limit inference from small differences.

```sh
python3 -B tests/native-fragment-subgroup-presenter-gpu/run.py \
  --original /private/distort_ps_achromatic_se2.spv \
  --record /private/subgroup.json --part both
```

Use `--part witnesses` for only the new routing/helper controls after the main
comparison has passed. Khronos validation and sync validation must be available
through an installed or explicitly isolated `VK_LAYER_PATH`. The runner invokes
the shared completed-run retention API after children are reaped and removes
only its private generated modules, headers, binaries and validation files.
