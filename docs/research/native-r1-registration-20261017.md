# Offline registration directly in the observed native R1 chart

The first complete native original-invocation capture now supports a bounded
source-registration check without fitted coefficients or another device capture.
This tool is diagnostic only. It changes no shader, presenter, device or PC Steam
sender, and does not produce real game coverage.

`tools/native_r1_registration.py` accepts the original128-byte SSBO, the paired
raw/component files and their metadata. It checks the existing record schema,
byte digests, exact image/view generations, source cohort and final accepted
execution; execution is not assumed to be1. The source layout remains the
transaction contract's `SHADER_READ_ONLY_OPTIMAL`5; unknown layouts refuse. It reuses the existing paired-image
validator. Raw-to-component differences are reported, never used as an opacity
oracle or silently repaired. The selected original lookup must intersect its
positive, bounded R1 rectangle. The fixed688×548 source profile requires at
least one receiver texel per source pixel.

B is **only** the actual R1 rectangle:
`[R1.z-R1.x,0,R1.x;0,R1.w-R1.y,R1.y]`. Source pixel centers map through that
chart to current component-G texel centers. There is no search, fit, historical
map, outer-band selection or other-quarter fallback. The explicit numeric
coverage panel remains distinct from scene RGB; no key color supplies opacity.

The new reader declares `fixed_source_cell_central_2x2_v1`: the central2×2 pixels
inside each authored4×4 complementary header cell, with unchanged≤64/≥191
thresholds. All three complete132-byte headers must independently pass CRC and
agree byte-for-byte, including session, app-owned pair ID, encoded eye, convention,
layout and both plane hashes. The existing strict4×4 reader is unchanged and
is never automatically replaced or retried through this reader.

Discovery returns an untrusted tuple bound to the complete input digests and
recorded cohort. Verification requires the same inputs, exact canonical C#
source artifacts and the existing source/DLL/invocation provenance receipt. It
reuses canonical source, held-out marker and homogeneous coverage checks. A
modified candidate, including a fitted B or altered authority/type, refuses.
All physical-eye, native-frame, continuous mapping, sampler-equivalence, GPU/source
association and passthrough authorities remain false. Recorded producer metadata
is validated for agreement, not elevated to independent native-frame proof.

The CLI discovers with `--snapshot`, `--sampler`, `--metadata`, `--ssbo` and an
exclusive `--record` output. Verification additionally requires all of
`--candidate`, `--source-record`, `--color`, `--coverage`, `--canvas`,
`--source-provenance` and `--source-root`. It never executes a generator/compiler;
the supplied provenance is checked using the existing bounded helper.

## Evidence and limits

Ten initial owned synthetic methods pass; targeted deltas cover the changed
candidate/type checks and the new eleventh layout-refusal method. They cover exact execution/cohort/schema, invalid R1,
byte changes, both encoded labels, mixed/ambiguous/corrupt headers, canonical and
provenance mutants, explicit interior-versus-strict-footprint behavior, CLI
argument completeness and exclusive output. No unchanged full suite is rerun.
The synthetic receipt fixture is explicitly not an authentic C# generator.

On the existing retired capture `4c5680d108a3fcf143722a05d94e02d8`, actual record
R1=(0,.25,1,.5) directly recovers session
`835dc49d01f049158280f6d714ff57df`, pair443/phase58, encoded label left. Both CLI
stages pass. The real retained C# emitter reproduces the declared RGB/coverage
hashes; its DLL and all five source pins match the earlier successful build.
Held-out background221/color218 cells have no wrong/ambiguous bits; minimum
contrasts are81/88.5. Source-selected7×7 uniform regions have at least846 samples
per code and maximum error1. These are code/interior observations, not boundary
quality or linear game-alpha proof.

For the same non-fitted chart, the unchanged strict4×4 reader still refuses14
ambiguous cells in every copy. No strict-reader pass is claimed. The independent
pixel-fitted locator's finite candidate differs by at most.175 receiver pixels
horizontally/.025 vertically; it is comparison evidence, never this tool's input.
Current independent PC-session logs are unavailable. One native sample falls in
a fixed magic bit, not the session/pair fields. Complete headers are read from
the same-execution paired component image, not exported during the original
fragment. The tool therefore does not claim those remaining associations.

Small source pins, commands, failures, CLI reports, provenance and cleanup
receipts are retained privately. No Valve binary, image, shader or disassembly
is redistributed. Future work can reuse this bounded chart/header contract for
same-invocation scene/coverage retargeting and real application-owned coverage;
it still needs explicit live lifetime/composition checks and visible validation.
