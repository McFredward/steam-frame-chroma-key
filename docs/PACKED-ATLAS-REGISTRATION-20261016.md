# Inert registration in the original packed-image chart

This follows [the explicit dual-read prototype](ATLAS-RECONSTRUCTION-20261016.md).
No Frame installation, native pipeline replacement or live alpha output is enabled.

## Direct coordinates, without a CPU uniform join

Let B map the normalized complete application canvas to one complete copy in the
packed sampled image. Let Tc/Ta place scene and coverage inside that canvas. The
original native fragment already computes its first packed coordinate u. Target
the two reads directly:

```text
uc = B * Tc * inverse(B) * u
ua = B * Ta * inverse(B) * u
```

Both reads still use the original fragment's already loaded metadata, native
reconstruction, descriptor and sampler. Target-q values exported by the probe
are diagnostics derived from the same invocation's R1, not CPU inputs. The color
uses the ORIGINAL output-eye aperture and tint once; the numeric panel uses its
own reconstruction weight and neither the RGB decoder nor output aperture/tint.

For a native mapping N from normalized q to first packed u, `W=inverse(N)*B`
makes this algebraically equivalent to the earlier q/W conjugation. It removes
the need to join a post-draw CPU copy of R1 to a previous fragment. It does not
make an arbitrary B correct or turn raw sampled component-G into game coverage.

The transformer has a separate strict packed-B schema and `transform_packed`
entry point. The q/W API remains strict and unchanged. B, Tc and Ta are explicit
finite nondegenerate diagnostic affines: binary32 inputs, binary64 host
composition, binary32 generated constants. Unknown native originals refuse.
Original native instructions, output0 and loaded descriptors remain in place;
only owned offscreen MRT outputs are added. A future live OFF path still needs
the stock native module rather than the diagnostic's disabled branch.

## Completed direct-packed GPU evidence

Intel and llvmpipe each pass48 enabled cases/3665 checks and48 disabled cases/
3341 checks with active Khronos validation and zero errors. Actual output0 is
bit-identical in all48 packed cases on both drivers. There are44 direct original
color GPU oracles and48 raw-G inverse-decoder oracles,24 differing reconstruction
weights and16 partial original-aperture cases. Four zero-aperture color cases
cannot supply the inverse-aperture comparison; they are not counted as color
oracles. Actual shader output is also compared with the owned arithmetic model.

Two asymmetric original R1 profiles exercise q-route equivalence separately,
24 cases each. Their original-q variants retain the previously recorded Intel
rounding differences within2e-6; they must not be called globally bit-identical.
Valid wrong-coordinate-chart and shared-mask-weight mutants are rejected at
their specific RGB/raw-G checks on both drivers, without validation errors.

Ten Python structure/schema/CLI methods pass with the explicit private original.
Four before/after old-q binary and record comparisons agree byte-for-byte.
No unchanged whole mod gate is rerun. Original inputs remain private and are
preserved; generated derivatives and owned compiler/package caches are deleted
after use. Small source pins, positive/negative results and reviews are retained in
`build/private-packed-atlas-20261016/`.

## What a registration result must mean

Discrete pixels can establish only a candidate B within sampling/search
uncertainty, not a unique exact continuous transformation. A locator must not
accept the previous fitted coefficients as inputs or hide that uncertainty.
Header locations establish the candidate; disjoint dense markers and canonical
source artifacts validate it afterwards. Multiple complete copies cannot be
ranked into an assumed outer band. Exhausted/ambiguous searches refuse.

A result belongs to its immutable sampled-byte digest, capture identity,
image/view generations and complete recovered source tuple. Object generations
describe lifetimes, not new decoder contents. Reusing a VkImage handle or finding
the same periodic body does not establish a video-frame epoch. Encoded source-eye
labels remain distinct from physical-eye proof.

### Actual current-pixel locator

The new bounded positive axis-aligned locator takes no historical map or fit
coefficients. Current sampled-G magic edges produce header-band hypotheses; a
fixed coarse grid, CRC-recovered full-header edge fit and fixed fine grid produce
the candidate. All three unchanged strict headers must agree. Only afterwards
do canonical source/DLL/artifact provenance, disjoint dense markers and
source-selected homogeneous coverage interiors validate it. False source hashes,
wrong/stale tuples, changed bytes/extents, duplicate complete copies, cropped
canvases and exhausted work budgets refuse. Caller records cannot upgrade the
false native/live authorities.

The exact immutable capture `b5ff764e7af8867a94aff07a56c555db` independently
recovers encoded source-eye labels left and right, session
`a0bff9cdfd7e4deda4181c33a88df645`, pair682/phase41. Both final CLI runs validate
the full original P010/sampler pair and the original C# emitter provenance;
all six headers agree with canonical artifacts. Each eye passes221 background
plus220 color held-out marker cells with no incorrect/ambiguous bits. Minimum
contrasts are75/74.5 for left and74/76.5 for right. Six source-selected7x7-uniform
coverage levels have at least846 observations each: maximum code error0 left,
1 right. This is not a boundary-quality or alpha-transfer result.

Candidate pixel-chart parameters `(sx,sy,tx,ty)` are approximately
`(1.67465992,2.10234236,-.13776314,1151.96478594)` and
`(1.67449444,2.10231527,-.09805465,3455.96825984)`. Feasible sampled-grid ranges
and edge residuals remain in the records. These are not exact continuous
transforms or outer-band labels. Only a post-discovery comparison reads the
previous fit; maximum source-centre receiver-coordinate differences are
(.277,.049) and(.351,.037) pixels. The previous fit never seeds the locator.

Ten public controls are composed from six passing controls before a bounded
header-cache repair and four remaining controls afterwards. The original240s
timeout is preserved. The cache reuses only byte-identical complete three-header
inputs to the unchanged parser, within one immutable locate/session/eye, bounded
to128 entries (about13.4MB). It never merges physical copies or hypotheses. No
fresh full-ten-method local replay is claimed; the existing successful controls
are inherited. Final strict actual CLIs take about30/28 seconds: this is an
offline diagnostic, not a real-time GPU registrar. Private records remain at
`build/private-canvas-locator-20261016/`.

Even a complete-copy candidate must match the same original invocation's first
lookup/R1 domain before any native use. Current post-render-pass acquisition is
appropriate for immutable offline evidence; moving it before the draw can precede
native layout/ownership acquisition, and dispatching compute inside an active
render pass is invalid. No such relocation is implemented.

The next device package needs a legal bounded original-SSA observation route,
prepared cold and tied to exact selected-image lifetime. A fragment SSBO requires
observed enabled features and compatible owned layout, not physical support
alone. A visible opaque diagnostic strip would instead require fresh agreement
and a proven legal output readback route. Neither is activated here.

Real coverage production is a separate dependency. See the bounded
[game-shader prototype](GAME-COVERAGE-PROTOTYPE-20261016.md); it enables no live
tap or game-wide opacity guarantee. No native presenter or streamed passthrough
is established by these offline tests.
