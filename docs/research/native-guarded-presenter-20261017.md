# Inert fragment-only synthetic format guard

`tools/native_fragment_guarded_presenter.py` wraps the current-R1 candidate with
an entirely fragment-local check of the frozen
[`synthetic-presenter-v2` contract](../protocols/synthetic-presenter-v2.json).
It installs no override and changes no runtime hook, native vertex shader,
descriptor, push constant, sender or device setting. Generated modules remain
private. All native frame/eye/mapping and passthrough authorities remain false.

The input contract is exact, including JSON value types. It has a 688×548 canvas,
color at `(16,244,320,192)`, coverage at `(352,244,320,192)`, and twelve neutral
32×32 cells beginning at `(16,464)` with horizontal stride52. Their centers are
`(32+52*i,480)`. The first twelve bytes of the documented SHA-256 signature are
the codes `111,153,155,51,111,82,92,219,102,227,121,189`. Each sampled native-G value
must lie in the inclusive ordered interval `(code±2)/255`.

This recognizes a format. It does **not** authenticate the body, establish
liveness, associate a separately received frame, prove game coverage, or grant
permission to compose camera pixels. Copying the strip onto another body still
passes this format check. The source/session/coverage obligations remain separate.

## Execution order

The original native function through SSA994 is retained instruction for
instruction, including its two original implicit samples. Then:

1. Ordered comparisons require finite unit-bounded R1, positive spans, and the
   original packed coordinate u inside R1. Failure stores the exact original
   SSA994, with no additional texture read.
2. Sample cell0 through the current original R1 using the original B9 image and
   B25 sampler values, explicit LOD0, component-G. A mismatch immediately stores
   SSA994. The remaining eleven cells and both body panels are skipped.
3. Only after cell0 passes, read the remaining eleven cells and check every
   ordered inclusive interval. NaN and infinities cannot satisfy the intervals.
   Failure stores SSA994 without evaluating retargeted panel code.
4. Only after all twelve checks pass, evaluate the inherited current-q/R1 color
   and coverage panels and write the straight-source premultiplied contribution.

There is one output attachment/location. Three distinct invalid branches store
the original value directly; the valid branch stores the candidate contribution.
There is no eager `OpSelect` that calculates both alternatives before deciding.
Ordinary first-cell mismatch costs one additional sample, later mismatch costs
twelve, and a valid body costs sixteen additional samples including the four
panel reads. The independently owned GPU timing lane measures that cost; this
source count is not a performance claim.

The branch can differ between fragments because u differs. Implicit derivatives
inside that conditional path would not be generally defined. Therefore **only
the four new panel samples** are changed to explicit LOD0, as are the twelve
guard samples. The two original samples and original SSA994 remain untouched.
The new candidate's declared scope is an owned single-mip synthetic image with
equal min/mag filtering. The successful r7 native acquisition separately required
`image.mipLevels==1` and `view.levelCount==1` in the tracker admission path; this
does not itself prove arbitrary native sampler-filter equivalence. No such live
equivalence is claimed by the tool.

The guard reuses the already loaded original image/sampler values. A separate
`OpSampledImage` combination is emitted in each guard block, since its result
cannot be consumed in another basic block. The first fast-OFF draft violated
that rule; `spirv-val` refused it before publication. The bounded repair adds no
descriptor load, and the final candidate passes validation. The failed result
and repair receipt are retained.

## API and offline checks

```python
profile = native_fragment_guarded_presenter.load_profile()
binary, record = native_fragment_guarded_presenter.transform(
    exact_private_original, profile, enabled=True, descriptor_count=1)
```

The profile may also be supplied as a dictionary, but it must equal the frozen
semantic contract; no caller B/W, new codes, tolerance, geometry or CPU enable
field is accepted. The default count1 candidate retains the inherited fresh-ID
interface specialization and introduces no descriptors or SSBO. Disabled API
output is the exact original module bytes. This compile-time OFF API is not a
runtime switch for an already recorded native command buffer.

Enabled modules retain an unmistakable `MRMASK/` module marker and SPIR-V1.1
header; the actual unchanged C++ legacy guard rejects them. Marker removal or
installation through that legacy path is not supported.

```sh
FRAME_PRIVATE_ORIGINAL_PS=/private/exact-original.spv \
  python3 -B tests/test_native_fragment_guarded_presenter.py -v
python3 -B tools/native_fragment_guarded_presenter.py \
  --original /private/exact-original.spv \
  --output /private/new-guarded.spv --record /private/new-guarded.json
```

Three public methods check the frozen schema, geometry/signature fields and
explicit API types. Six private methods check exact original-prefix preservation,
unchanged original reads, both count interfaces, ordered/nonfinite/boundary
predicate behavior, control-flow dominance and no panel work on invalid routes,
real legacy refusal, OFF and exclusive validated CLI output. Three deliberately
wrong but valid SPIR-V modules (bypass first cell, wrong interval, wrong fallback)
fail their corresponding independent structural checks.

The initial focused run passed eight methods. The ninth exposed a test-mutator
assumption that the original contained a global vec4 constant. It does not. The
repair creates a fresh ordinary vec4 constant for that intentionally wrong
fallback and reruns only the affected method; all three mutants are valid and
rejected by the control proof. This is a composed focused pass, not a replay of
previously passing suites. Independent host-GPU correctness and timing are
recorded in the separate new guarded-presenter GPU fixture.

Private logs, source hashes and cleanup receipts live under
`build/private-guarded-presenter-worker-20261017/`. Generated modules and compiler
outputs are removed after child processes finish, using the shared completion
cleanup API with an exact private-scope fallback. No original hardware capture
or another worker's active output is removed.
