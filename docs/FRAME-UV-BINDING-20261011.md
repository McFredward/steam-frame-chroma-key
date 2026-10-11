# Frame mask UV and descriptor route, 2026-10-11

## Result and scope

The installed `distort_vs_reproject_layered` vertex shader provides a concrete
candidate for a full-eye mask route: it maps a reprojected source coordinate into
an eye rectangle, and the inspected fragment's `%883` reverses that rectangle
mapping. Native distortion and reprojection can therefore remain untouched.

This is a conditional, static dataflow result, **not a live binding proof**.
A subsequent [authorized passive session](FRAME-TRACE-SESSION.md#fifth-attempt-bounded-positive-pipeline-evidence)
established 39 complete recorded/submitted pairings of the layered vertex and
inspected fragment. Vertex/fragment uniform coherence and source frame/eye
association remain unobserved. Genuine-mask activation stays blocked. This
static inspection used privately supplied files only; the separately authorized
passive deployment and its verified removal are documented in that session.

The OS/compositor identity is recorded in
[the installed shader report](FRAME-SHADER-20261011.md). This document contains
interface/data-layout metadata and an original algebraic explanation, not Valve
shader code or disassembly.

## Which vertex stage can feed this fragment?

The inspected fragment statically reads XY at location 1; it also declares
three `vec4` locations 0–2, a flat unsigned location 3 and the `Layer` builtin.

| Installed candidate | Vertex output metadata | Consequence |
| --- | --- | --- |
| `distort_vs_reproject_layered` | Three `NoPerspective vec4` locations 0–2, flat unsigned location 3, `Layer` | Exact declared interface match. It supplies the rectangle-mapped middle source-coordinate channel to location 1. |
| `distort_vs_reproject_layered_nd` | Four `NoPerspective vec2` locations 0–3, flat unsigned location 4, `Layer` | A different mapping/layout. Its location-1 XY is not mapped through the same rectangle as the layered candidate. Do not inherit the layered mask contract. |
| `distort_vs_reproject_fv` and `_fv_dr` | Location 0 `vec2`, location 1 scalar float, flat unsigned location 2 | Location 1 does not provide both coordinates used by this fragment. These are not proven direct producers for this fragment. |
| `distort_tri_gs` | Geometry output location 0 `vec2` | Does not supply the layered candidate's declared interface. |
| `distort_tri_ptnorm_gs` | Geometry outputs `vec2`, `vec3`, `vec2` at 0–2 | A different interface/dataflow; not proof of an intermediate stage for the inspected fragment. |

An FV mismatch does **not** prove that a geometry shader is required or active.
The fragment's `Geometry` capability is also not such proof: it declares the
`Layer` builtin. The layered vertex writes that builtin directly using its
viewport/layer extension. The actual pipeline-stage tuple must decide the route.

Version fingerprints for the two important direct-stage candidates:

- Layered vertex SHA-256:
  `94abc0c24124e8b4a301e7f1df5a4c8c0e8b0eb50e4c6ea78f3521370a6897c5`.
- Layered-ND vertex SHA-256:
  `92e77ccba1475f7f2972c1c457d4f8d42c9913106b8efb373a41003c9c21c64a`.
- Fragment SHA-256:
  `f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2`.

Installed-file hashes identify candidates; they do not prove which modules were
bound for a particular streamed draw.

## Source-coordinate chain and the exact cancellation

Use these analysis labels: `u` is the fragment's first packed-color UV (`%538`),
`R0` is the inner reconstruction rectangle, `R1` is the outer valid rectangle,
and `q` is the layered vertex's reprojected normalized source coordinate for the
middle channel. They describe dataflow; they are not recovered Valve API names.

The layered vertex derives `q` from its channel UV, the native inverse/projective
transforms, eye matrices, perspective divisions and row-dependent interpolation
between two native transform states. It then maps `q` into selected-eye `R1`
before writing location-1 XY. Rasterization interpolates that varying without
perspective correction. The fragment uses its own selected-eye `R1` to normalize
the interpolated `u` at `%883`.

If both stages use the same finite, nondegenerate rectangle and source eye, the
following affine identity holds componentwise:

```text
u = R1.min + (R1.max - R1.min) * q
maskUV = (u - R1.min) / (R1.max - R1.min) = q
```

Because the rectangle is constant for the draw, this cancellation also holds
after `NoPerspective` interpolation. It retains the native vertex's reprojection
and distortion-mesh relationship. The mask must correspond to the **same source
raster/projection represented by `q`**, not to output-eye pixels or an unrelated
application submit. A mask exported before an additional PC composition/warp
would not become correct merely because it has the same dimensions.

Native color reconstruction still samples the packed image twice: once at `u`
and once at `%798`, a relocation/rescaling through `R0`, `R1` and a patch origin.
The two decoded colors are blended with the native reconstruction weight before
outer-edge attenuation and tint. `%883` is neither packed sample coordinate.

A full-eye, unfoveated alpha plane samples `q` once. An alpha plane stored in the
packed video layout instead needs both native packed-coordinate reads and the
corresponding reconstruction treatment. Dividing the decoder's 1152 × 4608
extent into assumed eye halves, or putting packed alpha at `%883`, is invalid.
Do not add native edge attenuation to alpha independently without proving the
RGB/coverage contract; an already applied factor can otherwise be counted twice.

## Exact metadata needed to establish coherence

All offsets below are **relative to the bound descriptor's effective buffer
offset**, including any dynamic offset. They are not absolute memory addresses.
The declarations are version-specific and do not justify reading an assumed
private compositor C++ structure.

| Stage/resource | Relevant declaration-derived fields |
| --- | --- |
| Layered vertex set 0/binding 0 | Eye selector `E` at byte 256; another native projection/depth selector at byte 264. |
| Fragment set 0/binding 4 | Eye selector at byte 256; it must agree with vertex `E` for the candidate route. |
| Layered vertex set 0/binding 2 | Current metadata slot at byte 0. Selected record `K` may instead come from a native fallback check. Metadata matrix 6 starts at `432 + 928*K + 448*E`. |
| Layered vertex set 0/binding 3 | Fallback-slot field at byte 384, consistency-check field at byte 388, depth field at byte 392. The selector/consistency fields are not proven source-frame IDs. |
| Fragment set 0/binding 6 | Metadata matrix 6 starts at `432 + 448*E`: this fragment reads record zero. |
| Fragment set 0/binding 9 | The actual color image view at array element zero, with subresource, decoded-frame provenance and generation. |

The matrix is row-major with a 16-byte row stride. `R1` is its **column 1**,
so its four components are at matrix-relative byte offsets **4, 20, 36, 52**.
Reading a contiguous vector at matrix offset 16 would read the wrong values.
`R0` uses offsets 0, 16, 32, 48; the relocated patch origin uses offsets 44, 60;
the packed reconstruction flags originate at offset 12.

The selected vertex record may differ from the fragment's record-zero binding.
One legitimate arrangement could bind the fragment buffer at an adjusted
descriptor offset; that arrangement has **not** been observed. Capture effective
buffer offsets and values, rather than assuming binding 2 and binding 6 share
the same base pointer. Numeric rectangle equality alone also does not establish
source-frame identity or buffer lifetime.

The layered vertex writes `Layer` from its eye selector and publishes selected
record `K` at flat location 3. This fragment does not read either input. `K` is
a selected metadata slot, not a mask key. Require native selectors to be 0 or 1
and separately prove their mapping to the PC's left/right source planes.

## Smallest correct descriptor route

Start with a verified **single-eye draw** using the exact layered/fragment pair,
coherent eye selectors/rectangles and an established decoded source identity.
The existing generator's 2D mask shape can then remain unchanged:

1. Export one unfoveated, per-eye R8 coverage plane from the actual PC encoder
   source composition, preserving its projection, raster origin, extent and
   declared straight/premultiplied RGB convention.
2. Select that plane by exact stream session, source frame, verified eye and
   extent. Import it as a non-sRGB `R8_UNORM` image. Source frames reused for
   several display reprojections must reuse their own mask.
3. Bind an independent combined sampler/image at set 0/binding 31 only after
   proving that slot is free in the actual layout. Use LOD zero and an opaque
   border. Do not reuse the native YCbCr sampler for this R8 image.
4. Keep all native shader stages, sample mappings and color reconstruction.
   Sample the mask at `%883` only under the proved full-eye route above.
5. Retain the descriptor/image until all GPU uses complete. Do not update a
   descriptor in flight or overwrite the mask when a newer packet arrives.

Use immutable per-frame/per-eye bindings or equivalent safe descriptor lifetime,
not an unsynchronized update to a shared current descriptor. If the live draw
can produce multiple source eyes, the existing single 2D binding is insufficient:
use separately proven per-eye draws or a new eye-indexed mask-array profile.
Do not quietly assign one eye's image to both eyes.

This requires only a descriptor/layout extension through Vulkan's public API;
it does not require inventing the private compositor presentation ABI. It still
requires linking the native sampled decoded surface to the PC source frame.
Missing pipeline, frame, eye, rectangle or lifetime proof keeps rendering opaque.

## Minimum additional evidence and acceptance

Collect one bounded active-stream record for each physical eye containing:

- The actual graphics-pipeline stage hashes, including whether a geometry or
  tessellation stage is present, and the bound image-view/subresource identity.
- Effective buffer ranges/offsets and the selected eye, slot, `R1`, reconstruction
  fields and generation for the draw. Values must represent the GPU-consumed
  record, not an arbitrary later read of mutable host memory.
- The source-frame/session key associated with that decoded view, and the PC
  source plane's projection/origin/extent and RGB convention.
- The framebuffer layer/viewport route proving how native eye selectors map
  to the PC's left/right planes.

Then perform an offscreen, private-native-shader check of asymmetric rectangles
and asymmetric per-eye alpha patterns. It can validate the static cancellation
and mask selection without changing the headset, but cannot prove live stream
association. Finally check frame reorder/reuse, foveation changes, resolution
changes, eye boundaries and fast head movement on hardware after authorization.
No new runtime hook, mask activation or headset test was performed here.
