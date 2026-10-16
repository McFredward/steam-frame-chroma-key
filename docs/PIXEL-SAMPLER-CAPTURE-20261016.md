# Native sampler capture and static source recovery, 2026-10-16

## Result

The approved query-history follow-up to the [cold refusal](PIXEL-SAMPLER-ATTEMPT-20261016.md)
completed a real paired acquisition. The unchanged strict offline parser recovers
all three original CRC-protected source headers for each eye from the sampled
image. Expected session, content ID, encoded eye identity and color/coverage
digests match the trusted original sources. The actual native sampler preserves
the measured interior grayscale coverage codes, including fractional values.

This establishes static source carriage through the real decoded resource. It
does not establish native spatial/temporal association or working passthrough.
The Frame has been fully restored; no further device access is required for
this offline analysis. GloomhavenVR source build 676 and the native Steam PC
sender remain unchanged. No native shader replacement or alpha composition was
activated.

## Actual acquisition and integrity

The final ARM diagnostic is 291,776 bytes, SHA-256
`01d371a143bbf77b60a1d89cec1693e657c12cbcad728916cd3c3abb539445ed`.
The source is fork commit `a2893cb85552d6a67b0b97f1d55fd687d35778fc`;
its complete hosted validation passed. Actual native import/load, UI health and
cold preparation passed before requesting the stream. Native admission observed
enabled YCbCr and pipeline-statistics features, complete query-route coverage,
the exact native conversion/format profile and a compute-capable queue. Merely
enabling pipeline statistics was not treated as an active query. This corroborates
the repaired input class; the old missing first-refusal record remains unknown.

After the maintainer confirmed the source pattern in both eyes, the bounded
capture progressed through prepared, armed, commands_recorded and complete.
Raw and sampled output share image generation 2963861, view generation 2963863,
conversion 22 and reusable command generation 42987. The owned fence completed
and that exact reusable generation retired before publication.

All three artifacts were copied separately and independently verified against
the remote lengths/hashes and saved metadata:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Tight P010 copy | 15,925,248 | `f69b97385b0b89fe4f9673a7e4a2a2c55d06cfd5d42dd0e82bf23b128ac5b813` |
| Native sampled RGBA8 components | 21,233,664 | `b7d5a76350238a6697d64f2bd2a7ed935c7ee2cd6e830e80f649f02ced633433` |
| Snapshot metadata | 2,412 | `9187cb6d9a4dc9bb16582ca6d983832a2b7cd2034e006f73c58eb74eacfc2bb4` |

The image extent is 1152x4608. Current metadata identifies the Qualcomm compressed
DRM modifier `0x0500000000000001`, with physical plane row pitches 1536. These are
physical import facts, not replacement strides for the logical tight copy.
The sampler uses the exact native RGB_IDENTITY/FULL/identity/MIDPOINT/LINEAR
conversion. Its output components are not reconstructed scene RGB.

## Offline recovery and controls

Sampled G visibly contains complete source canvases in the second and fourth
1152-row bands. A bounded refinement finds a common rational pixel-center map:
horizontal scale `115243/68800`, horizontal offset `-1/8`, vertical scale `288/137`,
with vertical offsets 1152 and 3456. Several nearby fits also pass. These are
explicit offline sample hypotheses, not a unique calibration, native eye
placement, runtime UV or foveation/reprojection contract.

The unchanged production parser checks every original 4x4 cell in both halves
of every header bit, all three copies, contrast, identical headers, CRC and
exact trusted source bytes. No threshold, source or metadata guard was relaxed.
Both eyes pass all three original headers. Swapping the trusted eye fails exact
source identity after the redundant-header/CRC checks. Applying the same maps
to raw Y from this acquisition fails strict contrast.

Six constant strips in the actual source carry codes 0, 64, 128, 191, 255 and 192;
the independent five calibration fields are also measured. All measured interior
source-center samples arrive exactly in sampled G. Across the entire coverage
plane, including edges,
mean absolute error is 0.1399/0.1449 eight-bit codes for the left/right source;
maximum error is 24. Interior preservation must not be substituted for edge
fidelity or arbitrary translucent-scene correctness.

The raw-copy representation and native sampling disagree: mean absolute
raw-Y/sampler-G difference is 97.978 eight-bit codes, with 4,470,317 of 5,308,416
pixels differing by more than one code. Sampling removes the prominent 16/32px
block discontinuities. This identifies the usable interpretation for the next
experiment, not a specific driver bug. Further carriage work should use logical
native GPU sampling rather than speculative CPU untile transformations.

Private evidence is retained in `build/private-query-admission-run-20261016-r1/`
and `build/private-paired-source-analysis-20261016/`. An independent review checks
all 23 source-analysis artifact hashes and the parser/measurement semantics.
The initial paired-reader report precedes the fitted source recovery; its
`header_recovered:false` is not the final source-analysis result. Its
`pair_valid:true` establishes the integrity/generation envelope, not equality
between raw-copy values and sampled values.

## Restoration and next boundary

The maintainer separately approved manual rollback. Configuration, implicit
manifest and arm were removed before a manual SteamVR restart. Fresh native UI
health is ready, the protected process-map scan has no owned or foreign
diagnostic mapping and is not uncertain, and the owned run library is absent.
Rollback completed with no errors. The maintainer was told standby is safe.
Inactive package/evidence files remain intentionally; no automatic expiry,
rollback or restart was introduced.

The next work must establish the native atlas/UV/foveation/reprojection route
and bind a changing app-owned source-frame identity to that exact decoded
frame. Static content ID 7 is not a video-frame counter. The encoded left/right
header label does not prove the native eye selector or packed-image mapping.
Use the [conditional native UV analysis](FRAME-UV-BINDING-20261011.md) as a
hypothesis to verify, not as activation permission.

Only after those associations are proven can real scene color/coverage be
composed with the Frame camera, preserving native mapping and correct fractional
alpha. Missing or stale association must remain opaque. Moving content, edges,
partial transparency, performance and the final mod option still require
separate validation. No visible passthrough result is claimed by this capture.
