# Inert synthetic-v2 guard GPU proof

This fixture executes the **offline** guarded fragment candidate against owned
single-mip RGBA32F images. It does not install a receiver layer or establish native
source/session/frame identity, physical-eye placement, liveness, body integrity,
video-transfer equivalence, native activation or passthrough permission. The
frozen protocol is `docs/protocols/synthetic-presenter-v2.json`.

The original private PS is an explicitly supplied SHA256-pinned input; neither
it nor a derivative is distributed. The approved SSA Vulkan harness supplies
allocation and metadata infrastructure. This new fixture uses one float color
attachment, two distinct source images and separately recorded original,
guarded and premultiplied source-over command buffers. Original comparisons use
the unchanged array3 module and layout; the count1 candidate is a compatible
subset of that same owned layout. This proves guard semantics, not the production
native descriptor factory or original vertex stage.

The first three frames update **the same image** ON → OFF → ON and execute the
same already-recorded command buffers, with no pipeline, descriptor or command
rebuild between them. Pixel contents alone determine recognition. The complete
91-case matrix checks:

- All twelve individual cells at both inclusive ±2-code endpoints and both
  exclusive ±3-code neighbors: 48 cases.
- Wrong, shifted, mirrored, scaled, erased, old-style-header and plain strips.
- Degenerate/reversed/out-of-unit/nonfinite R1, and out-of-R1/nonfinite input u.
- Two distinct VkImages with identical chart metadata and changing validity.
- Four chart rectangles, both metadata selectors and both decoder branches.
- Partial original aperture, independent zero/partial/full coverage and opaque
  black. Coverage is numeric component G, never a color key or decoded green.

Every invalid/OFF result must match **all four original994 words bit for bit**.
The original command is executed again after the guarded command and must also
restore bit for bit. Nonfinite controls compare actual local original results;
this is not a general legality or determinism claim for malformed native inputs.
Valid contribution RGB/alpha is checked against independent CPU semantics.
An actual Vulkan blend against a known opaque background checks premultiplied
source-over. The single mip and explicitly owned nearest/clamp sampler match the
candidate's bounded LOD0 test contract; native conversion/filter equivalence is
not inferred.

A copied valid strip with a modified body is deliberately **accepted** and its
new contribution is verified. The strip recognizes a synthetic format; it does
not authenticate the image body or prove freshness. Missing or invalid strip
contents restore opaque original output within this test contract.

## Host cost experiment

The same program has a separate timing mode. It uses owned varying raster UVs,
one RGBA32F output, first a 512×512 safety step and then 1152×4608
(5,308,416 pixels). Each original/valid-guard/first-cell-OFF case has one warm draw
and three measured reuses of a recorded command buffer. Vulkan timestamps cover
the render pass and its attachment transition; CPU timing covers queue submit
and fence completion, including the fixture's one-pixel copy/host barrier.
Source generation/uploads, shader/pipeline creation and query retrieval are
outside both intervals; the GPU timestamp interval excludes that copy. Validation
is explicitly absent during timings and mandatory for the correctness pass.

These are indicative **host Intel/Lavapipe** costs, not Steam Frame timings.
RGBA32F output and ordinary RGBA32F sampling differ from the real receiver's
attachments and converted video view. The vertex UV field is owned test code;
it is not native distortion, reprojection or foveation. Uniform guard centers
are sampled by every fragment, as in this candidate. Order, clocks, compilation
and caching can affect short samples; the report preserves all values and does
not infer an OFF speedup merely because one median is lower.

## Run

```sh
python3 -B tests/native-fragment-guarded-presenter-gpu/run.py \
  --original /private/distort_ps_achromatic_se2.spv \
  --probe-tool tools/native_fragment_guarded_presenter.py \
  --record /private/new-guard-proof.json --driver both
```

`--part correctness` and `--part timing` permit focused follow-up without
repeating a passed part. Provide an isolated `VK_LAYER_PATH`/`LD_LIBRARY_PATH`
when Khronos validation is not installed; the timing child hides that layer
package and refuses to report if validation remains enabled. Generated modules
are checked with `spirv-val`. Reports preserve input/source/protocol/module pins,
shader validation, case facts, actual driver output, timing samples and failures.
The shared completion-cleanup API is called after children are reaped; exact
private fallback cleanup removes only this invocation's regenerable modules,
fixture, case files and temporary validation logs. External private originals
and the small final evidence remain intact.
