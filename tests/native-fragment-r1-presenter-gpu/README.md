# Inert current-R1 presenter GPU controls

This fixture tests the **offline candidate**, not Steam Link passthrough. It uses
an explicitly supplied, SHA256-pinned private original fragment module and a
synthetic owned RGBA32F source. No Valve shader body or generated derivative is
included in the repository or release. Nothing is installed on a receiver.

The candidate takes the original invocation's current normalized
`q = (u - R1.min) / R1.span`, applies independent color and coverage panel
affines, and preserves the native reconstruction for each read. The synthetic
source convention is **straight RGB** plus independently encoded numeric
component-G coverage. Expected output is premultiplied RGB and explicit alpha:
color × coverage × the original aperture, with original tint applied once;
alpha is coverage × the original aperture. This does not prove that a streamed
video channel has linear game coverage or that a real source uses this encoding.

The fixture reuses the validated `native-fragment-ssa-gpu` allocation, metadata,
private-module and submission infrastructure. Its single-color-attachment
adapter follows the existing owned packed-output fixture. A count1 candidate is
actually created against a count1 B9 layout; unchanged-original comparison draws
use a separate count3 context. The original module is never used with an
incompatible count1 layout. Disabled mode returns the original module exactly
and compares its GPU output bit for bit.

The 192 cases use the actual observed R1 quarter, two other quarters and an
asymmetric span, both metadata selectors, all four original decoder and inner
reconstruction flag branches, distinct first/second color and mask footprints,
zero/partial/full coverage, opaque black, independent unequal reconstruction
weights, partial inner weights and partial original aperture. Four original
source texels are deliberately distinct; partial first/second mask coverage
also differs, making reused color/mask weights observable.

There are three independent checks for each enabled case:

- CPU binary32 native algebra and the explicit synthetic source predict the
  final output.
- An unchanged-original GPU instance is retargeted to the color coordinate;
  only its own panel aperture is removed before applying the original
  invocation's aperture and independently reconstructed coverage.
- Another unchanged-original GPU instance uses unit tint at the mask coordinate.
  Inverse linear decoder coefficients reconstruct raw component G. This checks
  numeric coverage separately from decoded green and from the candidate.

The four causal mutants remain valid SPIR-V. Missing RGB premultiplication,
applying aperture twice, sharing the color weight with the mask, and treating
original packed `u` as normalized current-R1 `q` must fail the **actual GPU output
oracle**, rather than merely being rejected by a structural parser. Both
positive and mutant modules are validated with `spirv-val`; Intel and Lavapipe
runs require active Khronos and synchronization validation with zero errors.
The inherited original parser remains unchanged; the fixture's bounded mutation
framer preserves the candidate's `OpModuleProcessed` marker.

Run only with an explicitly owned private original and the presenter helper:

```sh
python3 -B tests/native-fragment-r1-presenter-gpu/run.py \
  --original /private/distort_ps_achromatic_se2.spv \
  --probe-tool tools/native_fragment_r1_presenter.py \
  --record /private/new-r1-presenter-proof.json --driver both
```

If validation layers are not installed, provide an isolated `VK_LAYER_PATH` and
`LD_LIBRARY_PATH`; no system install is necessary. Reports retain input/source
hashes, case facts, commands, shader validation, exact mutant failures and driver
results. After every reaped run, the shared completion-cleanup API is called and
this invocation's temporary generated modules, fixture, cases and logs are
removed. Private external originals and final reports are preserved.

Passing these controls does **not** establish real native mesh/reprojection
mapping, video transfer equivalence, current source-session/frame identity,
physical-eye placement, provider/lifetime authority, additive emission,
refraction, or native activation. A1/partial/A0 here describe the explicitly
owned synthetic input only. The candidate remains inert until those separate
runtime and source validity requirements are established.
