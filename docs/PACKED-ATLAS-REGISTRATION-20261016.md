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
No unchanged whole mod gate is rerun. Original modules/generated derivatives
remain private and are deleted with owned compiler/package caches after use;
small source pins, positive/negative results and reviews are retained in
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
