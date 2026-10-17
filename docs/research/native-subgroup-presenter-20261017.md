# Offline subgroup sharing of the synthetic-v2 format guard

This experiment does **not** modify the default guard or receiver. It provides
an optional private-artifact generator, CPU structure tests and a new owned GPU
fixture. Native device capability/feature enablement, native creation and Frame
latency are not proved. No hardware installation or restart was performed.

## Source reasoning

The exact pinned original fragment stage has three structured selections, no
loops and no invocation termination before its final SSA994 expression. Its two
implicit samples and complete expression remain instruction-for-instruction
unchanged. The inserted work follows that expression, so no new divergent branch
encloses an original derivative-dependent sample. Additional synthetic samples
retain the bounded single-mip explicit-LOD0 contract.

All twelve guard coordinates depend only on the frozen profile and current R1;
both sampled-image and sampler values are the original statically uniform native
binding loads. Within the same draw, equal R1 therefore permits sharing the
same twelve tests against the same current image. No past image, frame counter,
CPU signature cache or shortened fingerprint is involved.

The final algorithm ballots `!HelperInvocation`, finds the first real lane, and
uses SHUFFLE with that exact lane ID for R1 and the computed signature. All lanes
remain in common control flow before the leader selection. A helper is never
the elected source even on implementations that vary helper participation in
subgroup operations. Each real lane compares all four own R1 components with the
leader's using ordered equality and separately checks its own geometry. Unequal
R1 and helpers run the complete original guard, including first-cell fast OFF.
An all-helper group has no framebuffer contribution and cannot elect a real
source; helpers still cannot select the shared output path.

The first draft excluded helpers with an outer branch and used Elect/First. An
Intel pass alone could not justify its nested reconvergence premise. That draft
was superseded offline before proposing any native package. The final approach
requires stronger, explicit reconvergence and a named nonhelper source.

## New requirements

Vulkan 1.1 and Fragment BASIC/BALLOT alone are insufficient. The eventual actual
device must support **and have enabled**
`VK_KHR_shader_subgroup_uniform_control_flow` with
`VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR::shaderSubgroupUniformControlFlow`.
Its Fragment subgroup properties must include BASIC, BALLOT and SHUFFLE. The
module is SPIR-V 1.3, declares `SPV_KHR_subgroup_uniform_control_flow`, and sets
`SubgroupUniformControlFlowKHR`. Physical-device support without the actual
device-create extension/feature-chain facts is not permission to use it. Existing
strict device-journal admission must not be relaxed for this experiment.

The official [SPIR-V extension](https://github.com/KhronosGroup/SPIRV-Registry/blob/main/extensions/KHR/SPV_KHR_subgroup_uniform_control_flow.asciidoc)
specifies the selection-merge reconvergence premise. Khronos' [subgroup guide](https://docs.vulkan.org/guide/latest/subgroups.html)
describes querying supported stages/operations. The [shader chapter](https://docs.vulkan.org/spec/latest/chapters/shaders.html)
documents helper participation limitations; the design deliberately chooses a
nonhelper rather than assuming helpers stay active. Small primary-source pins
and the actual local capability results are retained privately.

## Focused software result

Intel RPL-P, subgroup size32: 273 complete actual GPU cases compare new output
with the old guard bit for bit, including odd triangular boundaries and actual
alternating metadata/R1. The uniform cases include84 invalid/OFF comparisons
with all four original output words bit exact. All twelve single-code ±2/±3
controls, nonfinite/out-of-range geometry, two images, source changes and partial
coverage are inherited as inputs of this **new** focused comparison. Existing
whole suites were not repeated. Khronos/synchronization validation reports zero
errors. Lavapipe lacks the required uniform-control-flow feature and is refused.
The routing witness observes271 shared pixels for a uniform triangular chart,
and173 shared plus98 fallback pixels for mixed R1. A separate same-geometry quad
witness observes23 covered pixels adjacent to helpers. Twelve focused Python
tests pass. A single additional mixed-R1 GPU case passes ASan/UBSan/leak checks
with validation; the fixture also compiles strictly for ARM64. These are bounded
new-profile checks, not a repeated complete repository gate.

At 5,308,416 output pixels the retained Intel GPU medians in milliseconds were:

| Source | Original | Old guard | Subgroup guard |
| --- | ---: | ---: | ---: |
| Valid | 2.320 | 2.519 | 2.301 |
| First-cell OFF | 1.879 | 1.915 | 2.096 |

The valid difference is approximately0.22ms, while OFF is approximately0.18ms
slower. This is not decisive evidence for production complexity or an explanation
of the user's Frame head-turn lag. Three samples, differing compiled code, clocks
and reported concurrent unrelated test work limit the result. Older host numbers
must not be treated as an A/B comparison with this run. Raw samples, failures and
source pins live under ignored `build/private-native-subgroup-worker-20261017/`.

The retained capability and output evidence does not establish a native sampler,
source session/frame, physical eye, body authenticity, headset image result or a
Frame performance improvement. Copied valid signature plus altered body remains
accepted, exactly as for the original format guard.
