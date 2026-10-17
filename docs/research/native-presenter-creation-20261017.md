# Inert completion-scoped presenter creation

`layer/native_presenter_pipeline.h` is a cold primitive. The separate opt-in receiver now calls it during
actual native creation; neither the header nor its tests grant installation or
permission to change a Frame pipeline. The frozen private fragment
is18416 bytes, SHA256
`0aaf356d3789473cd32ff0d60cf5971bc1473813241657877db5c147bb1d4364`.
Its generated body and original Valve inputs stay private. Cold preparation owns
and hashes a copy; a caller-supplied digest cannot admit different executable code.

The intended integration point is **inside the same successful original
vkCreateGraphicsPipelines callback**, before its output is exposed to the caller.
The caller retains the actual creation inputs/dependencies through that synchronous
scope. A historical recipe, equal numeric handles, a former capture, or the
recipe's generation fields alone never establishes this lifetime contract.
Actual negotiated Vulkan1.1 or newer is required for the generated SPIR-V1.1;
a physical-device maximum or a nonnull downstream function is insufficient.

The primitive rechecks both exact original shader pins, matching current native
identities, complete descriptor/layout/pass definitions and the original graphics
shape. It admits the single original sampled-image binding9 at count1 or3 and
sampler25, with no new descriptors. The selected subpass must have one used,
Alpha-bearing color attachment from an explicit format whitelist; RGB-only and
unused references refuse. Blend/logic operations and static color-write disable
refuse, as do AlphaToOne, AlphaToCoverage, multisample extension chains and unknown
output-affecting dynamic states. The admitted native nine-state stencil/sample-
location/viewport family is retained unchanged. These conservative checks are
not a claim that every current/future native route is supported.

It creates only a private fragment module and one candidate pipeline. Every
original CI pointer and scalar remains the same except the copied two-stage array
and its fragment-module handle. The vertex stage, original native layout, render
pass, descriptor sets, push constants, blend/depth/sample/viewport state, dynamic
values and later draw arguments remain under the original application. No command
is recorded, descriptor copied/rebound, queue waited or GPU readback introduced.

Success substitutes a normal pipeline handle in the still-private result and
destroys the never-exposed original pipeline. The application then owns the new
pipeline through its ordinary destruction path. Failure retains the original
handle/result and destroys only successfully owned temporary resources, including
a partial pipeline returned on an error. Failed shader output is undefined and
is never destroyed. The short-lived shader module is destroyed after synchronous
pipeline creation, as allowed by
[Khronos shader lifetime](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyShaderModule.html).
The [creation API](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateGraphicsPipelines.html)
and [stage-module consumption](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineShaderStageCreateInfo.html)
provide the scope; they do not supply our missing runtime integration.

This removes the need for per-draw pipeline substitution, Set1/Set0 restoration,
SSBOs or retained replacement-command ownership. Every admitted native pipeline
can use the same path, including later views and reusable commands. Its GPU OFF
behavior remains the separately tested **current-image** guard, not a CPU choice
made once during recording. Changes become visible only when the next post-OFF
image arrives; frozen valid frames and copied guards remain indistinguishable.

## Evidence and remaining integration

Three public refusal checks and163 private positive/failure checks pass; the
latter also pass ASan/UBSan. Strict host and ARM compilation and the two fresh
CMake/CTest registrations pass. Controls exercise exact current shader pins,
sealed candidate bytes, count1/count3, all original CI/stage pointers, two
independent view pipelines, allocator/cache forwarding, partial failures, output
format, static/dynamic output gates and API-version refusal. The initial renamed
fixture-main compile diagnostic and an incorrect29-binding mirror-test choice
are retained with their bounded repairs. No unrelated existing suite was replayed.

An independent read-only review found the format, AlphaToOne/Coverage and static
color-write gaps in the first primitive; these were fixed before the final checks.
It also identified the negotiated-version gate, now explicit. The optional
[receiver integration](native-presenter-receiver-20261017.md) now tracks the
**actual returned replacement** honestly, retires the original journal identity
before native destroy, and never labels replacement handles as stock originals.
A later review reproduced a narrow race: sticky observer failure during native
candidate creation could skip the old retirement callback. Mandatory retirement
now runs despite that failed observation history and preserves the sticky refusal
for all later admissions. Its focused causal mutant fails at the exact old-
generation check; the repaired actual-hook control passes72 assertions. Partial
batch success and unknown definitions retain the original native result. Startup
health and reversible operator staging still need actual approved device use. No Frame change or alpha-consumer proof follows
from these tests.

Separate [guard GPU evidence](../../tests/native-fragment-guarded-presenter-gpu/README.md)
passes91 owned cases per Intel/lavapipe, including same-recorded-command ON/OFF/ON,
exact original fallback and premultiplied source-over, with zero validation/sync
errors. Indicative Intel GPU medians at5,308,416 pixels are4.62ms original and7.89ms
valid; all three samples and CPU waits are retained. This is a significant extra
cost, not a Frame prediction. It does not establish native vertex/video mapping,
real game opacity, physical eye labels or actual headset passthrough.

Private pins, logs, source/protocol comparisons and cleanup receipts live in
`build/private-native-presenter-creation-20261017/`; immutable hardware captures
and prior evidence remain preserved. Completed regenerable binaries and CMake
caches are removed only after their processes are reaped.
