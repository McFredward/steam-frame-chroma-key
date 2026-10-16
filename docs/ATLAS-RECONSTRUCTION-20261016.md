# Inert dual-panel reconstruction

This developer checkpoint extends the original-fragment observation prototype.
It is not an installed SteamVR layer or a live passthrough feature. It does not
modify the PC Steam sender, access the Frame or change GloomhavenVR Build677.

## Separate scene and mask lookups

The in-band source canvas contains separate scene-color and numeric coverage
panels. Reading only a mask while leaving the original scene lookup unchanged
would keep displaying the packed canvas. Both panels must be retargeted into
the original eye view, with native reprojection and packed reconstruction intact.

Let W map the application's normalized source canvas into the decoded full-eye
chart; Tc and Ta place a scene coordinate inside the two panels. For the
original fragment's post-reprojection coordinate q, the desired reads are:

```text
qc = W * Tc * inverse(W) * q
qa = W * Ta * inverse(W) * q
```

These are homogeneous affine operations; q remains in the decoded full-eye
chart. Each mapped coordinate then independently follows the native R1-to-packed
mapping, R0 relocation, original sampler and reconstruction weight. Neither
coordinate is already a packed texture UV. The two panel weights may differ.

The diagnostic requires explicit caller-supplied finite, nondegenerate affine
matrices. It does not infer W, reuse a historical fitted map or declare those
matrices correct for a native stream. The public OpenXR contract allows submitted
view/FOV differences to be mapped by the runtime; dimensions alone do not specify
that mapping. See the [OpenXR projection-layer contract](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc).
An affine diagnostic is not a proof that all runtime composition is affine.

## Preserve the original computation

The transformer accepts only the exact privately supplied original fragment
profile used by the previous observation tool. It clones the required native
dependency cones from that private input at execution time. Native instruction
bodies, modules and disassembly are never stored in repository source files.
Original descriptor loads are reused only when their SSA values dominate the
splice. No second native UBO or image/sampler descriptor loads supply those values.

The original instruction stream and output0 remain reconstructible unchanged.
The extra work writes only four owned offscreen diagnostic attachments. It does
not replace the native output, install a pipeline or extend a live descriptor
layout. The disabled diagnostic output must be bit-identical to the same
instrumented invocation's original RGBA expression.

An isolated Intel control found that the expanded shader's original aperture
expression rounds differently despite identical original instructions. A
constant neutral-chroma/unit-luma texture and unit tint separate that difference
from sampling or decoding: stock and the smaller observation module agree,
whereas the atlas module's original aperture changes by about 1.49e-6. All produce
the same UNORM8 code in this control. The specific backend optimization is not
observed. No native instruction or floating-point decoration is changed to hide it.

Consequently the inert stock-versus-atlas GPU comparison records actual bit
agreement, absolute/ULP differences and UNORM8 codes, with a fixed 2e-6 absolute
per-channel limit. It must not claim universal bit identity. Disabled MRT1 still
must exactly equal that same module's original MRT0 expression; integer metadata
remains exact. A future live OFF path must bind the unmodified stock module,
not merely select a branch of this observation shader.

Retargeted color uses its own decoded/reconstructed RGB, then applies the
ORIGINAL-q aperture and native tint once. A panel's own edge weight must not
replace the output-eye aperture or attenuate color twice.

The numeric coverage result is separately reconstructed sampled RGB_IDENTITY
component-G. It uses its own panel weight and no color decoder, tint or aperture.
This deliberately matches the measured component-code investigation; it does
not establish linear game opacity, video conversion equivalence or a production
straight/premultiplied composition rule. Chromatic test inputs must distinguish
this result from decoded RGB.green.

| Attachment | Meaning |
| --- | --- |
| 0, RGBA32 float | Original RGBA expression; measured rounding differences reported |
| 1, RGBA32 float | Retargeted color RGB and raw reconstructed coverage-G |
| 2, RGBA32 float | qc.xy and qa.xy |
| 3, RGBA32 float | Independent color/mask weights, original aperture, enabled |
| 4, RGBA32 uint | Original tint bit patterns and raw flags |

## Verification boundaries

The host fixture reuses the existing validated Vulkan resource and lifetime
code. It supplies owned asymmetric textures and explicit diagnostic transforms.
Direct execution of the privately supplied original shader at the independently
retargeted inputs is the color oracle; a CPU rewrite alone is insufficient.
Coverage additionally needs independent raw component sampling and its own
reconstruction weight. Causal controls must reject exchanged panels, a shared
weight or duplicated aperture attenuation.

## Completed host evidence

Intel RPL-P and llvmpipe each pass 48 enabled cases / 3677 checks and 48 disabled
cases / 3341 checks with Khronos validation active and zero errors. Each enabled
run includes 48 direct original-shader color oracles and 48 inverse-decoder
original-shader raw-G oracles. There are 22 unequal reconstruction-weight cases
and 16 partial original-aperture cases. Chromatic inputs distinguish raw-G from
decoded green. The explicit W rotates/shears the source chart; it is not fitted
to a capture or claimed to be the actual receiver mapping.

llvmpipe preserves stock output0 bits in all 48 cases. Intel preserves them in
40/48, with maximum absolute difference 1.35601e-6 and maximum 98 float-word ULPs.
Both drivers have zero changes to the computed clamped UNORM8 codes across the
tested values. That quantization is CPU clamp/round of the FLOAT32 readback,
not a separate GPU UNORM8 render or a headset result. Same-module disabled
MRT1/MRT0 equality is bit-exact on both drivers. The earlier strict-bit failures
and neutral-texture diagnosis are retained rather than rewritten as passes.

Three valid deliberately corrupted private modules exchange the panels, reuse
the color reconstruction weight for coverage, or apply the original aperture
twice. Both drivers reject every variant at the corresponding color/raw-G
oracle, without validation or synchronization errors. Four expected warnings
concern unwritten extra attachments of the unmodified original fragment; those
attachments are not read by its oracle. Full messages remain in private evidence.

All six focused Python methods pass on the integrated tree with the explicit
private original. CMake compiles both new owned host helpers; CI can run the
three owned-only Python methods while skipping the private class. It never
downloads a native module or runs the private GPU experiment. Independent source
and evidence review finds no blocker within this inert numerical contract.

No new complete mod gate or headset visual result is implied. The official
Ubuntu 1.3.275.0-1 validation package was extracted privately, never installed
globally, and removed after testing. Completed owned compiler/package caches
are removed after processes stop, preserving original inputs, source pins,
failed controls and small result records. Private evidence lives in
`build/private-atlas-splice-20261016/`, `build/private-atlas-gpu-20261016/` and
`build/private-atlas-integration-20261016/`.

Remaining work includes live application-to-decoded registration for the exact
selected image generation, physical-eye/frame association, real game coverage,
the final premultiplied/additive composition convention and an admitted native
presenter. Unknown native state keeps normal opaque rendering. The Frame stays
restored and there is no automatic device restart or rollback timer.
