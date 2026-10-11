# Steam Frame compositor shader inspection, 2026-10-11

This report describes static metadata from a private, read-only capture of an
installed shader on one headset. No live pipeline or descriptor capture has yet
established that this module is the one used for a particular streamed frame.
No Valve shader binary, source, instruction body or disassembly is redistributed.
The observations are version-specific, not a compatibility claim for every Frame
or SteamVR build. No replacement shader was enabled for this inspection.

## Captured build

- SteamOS: `0.3.0`, build `20260922.6101926`.
- `vrcompositor` ELF Build ID: `ba0d798c2694e05985421e848b67ced8984fe2d3`.
- Scene shader: `distort_ps_achromatic_se2.spv`, SPIR-V 1.0.
- Scene shader SHA-256:
  `f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2`.
- Entry stage: Fragment; entry name: `PS_DISTORT`.
- Private original module validation: 343 instructions, two image samples.

## Resource and coordinate metadata

| Resource | Set/binding | Observation |
| --- | --- | --- |
| Scene image array | `0/9` | Three sampled float 2D images; this fragment selects element zero for both color reads. |
| Scene sampler | `0/25` | Separate sampler combined with the selected image in the fragment. |
| View parameters | `0/4` | Uniform member 19 selects the first or second eye matrix; zero and nonzero take different paths. |
| Per-view metadata | `0/6` | Supplies the selected eye matrix and rectangle mapping used by the two color samples. |

The first native color sample uses `%538`, derived from the XY components of the
location-1 fragment input. The second uses `%798`, a remapping of that coordinate
through per-eye rectangle metadata. The shader decodes both samples and blends
them with a native, flag-controlled edge weight before applying the color tint.
This is consistent with two-region reconstruction of packed/foveated video;
the exact roles and values of those regions still require runtime uniform data.
Preserving one color sample alone would discard native processing.

Installed vertex-shader variants include `distort_vs_reproject_fv.spv` and
`distort_vs_reproject_fv_dr.spv`. Their presence does not prove which variant
produces location 1 on the live streaming pipeline. Reprojection, distortion and
foveation must be validated across the actual bound vertex and fragment stages.

`gl_Layer` is declared in the fragment interface but is not read in this captured
fragment. Selecting a mask by that declaration alone would not establish which
source eye the fragment is sampling. The relevant eye choice comes from uniforms.
For a bounded future capture, the selected-eye parameter is at byte offset 256
of binding `0/4`; the two metadata matrices are at byte offsets 432 and 880 in
binding `0/6`, with row-major storage and a matrix stride of 16 bytes. These are
declaration-derived offsets, not observations of runtime values or source IDs.

The current mask generator samples `%883`. This is **not either native color
sample coordinate**. It normalizes `%538` into another rectangle from the selected
eye metadata and supplies native outer-edge attenuation. Therefore:

- `%883` must not be treated as a direct UV into Steam Link's packed video image.
- A separate unfoveated, full-eye mask might use this normalized domain, but that
  association is still unproven. The rectangle must be matched to the PC source
  eye projection, valid extent and coverage convention.
- A mask transported in the same packed/foveated layout would require the native
  sample mappings and reconstruction weights, rather than one `%883` lookup.
- The adapter must establish the eye-specific texture/descriptor binding and
  correct source-frame association. A local submit count or latest received mask
  cannot supply either proof.

## Actual shader-generation defect and repair

The initial generator passed the original minimal synthetic fixture but rejected
the captured native shader during `spirv-val`: it added a duplicate non-aggregate
image type already present in the native shader. The validator prevented writing
a replacement output. This was a real compatibility defect hidden by the fixture's
absence of native image and sampler declarations.

The generator now reuses exactly matching native image, sampled-image and
UniformConstant pointer types. Ambiguous matching declarations and incompatible
existing image shapes are rejected. An additional **original synthetic** fixture
models separate image/sampler resources and a combined pointer type; it contains
no captured Valve instructions.

Local validation of the private captured shader now succeeds for both explicit
RGB conventions, `straight` and `premultiplied`. All generated modules retain the
`MRMASK/1 source-frame-binding-required` marker. The compositor layer rejects
these modules until a verified adapter supplies the matching descriptor layout,
same-frame mask, eye association and coordinate mapping. Successful assembly is
not a headset rendering test and does not remove that guard.

## Next adapter work

1. Capture bounded metadata for scene descriptor image views, selected eye and
   rectangle uniforms during actual Steam Link streaming, without changing the
   native service or dumping proprietary shader bodies into tracked files.
2. Establish an identity shared by the PC's composed source frame, transported
   mask and decoded Frame source image. Keep rendering opaque when it is absent.
3. Prove the full-eye normalized mask domain against the two native packed-color
   sample mappings, including eye boundaries, foveation transitions, reprojection
   and valid-rect edge attenuation. Preserve native processing throughout.
4. Bind an R8_UNORM mask with opaque borders and declare the proven RGB convention;
   validate descriptor lifetime and frame selection before enabling any shader.
5. Test fast head motion, thin antialiased geometry, eye boundaries, packet loss,
   stream restart and resolution changes on hardware. Camera pixels stay local
   to the Frame; they are not required on the PC for this design.
