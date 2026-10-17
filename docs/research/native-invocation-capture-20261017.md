# Successful original-invocation capture

The separately approved `native-ssbo-20261017-r2` run completed on the Frame.
The maintainer confirmed the PC-VR test stream. All four artifacts were copied
to the workstation and verified against their remote SHA256 values. The later
separately approved rollback removed activation and the loaded run library;
original SteamVR startup passed and the maintainer confirmed the visible UI was
usable. Standby is safe. No timer or automatic rollback/restart was used.

## New evidence

The 526,280-byte r7 library resolved all 94 required imports against the actual native
providers and passed isolated loading against those exact providers. The cold
driver lease admitted the actual chip 7 KHR callbacks at the established offsets
in the exact previously acquired Valve driver. It stayed Ready at epoch 1, with
zero acquisition misses and no outstanding permits in final metadata.

Capture `4c5680d108a3fcf143722a05d94e02d8` contains an 128-byte original-invocation
record alongside the 1152 × 4608 native sampled-component image and tight raw copy.
The source/image/view/memory cohort and command generation agree. Command 22123,
pool 1, execution 1 is the one observed original submission containing the copy.
The source recording was genuinely retired and the owned GPU fence completed
before readback. 32 deduplicated recording contexts account for 46 encounters,
without overflow, contention or a first recording refusal. These counts are
not native video-frame identifiers or a complete per-call timeline.

The record header is claimed 1, magic `0x5353424f`, version 1, 32 words. All 27 float
fields are finite. The recorded original RGBA expression can be reproduced
bit-for-bit from its actual samples and consumed metadata. This is not a
framebuffer/display comparison. The actual first lookup lies in the current
registration canvas. Its component values agree with the four-neighbor native
sampler copy within the explicitly conditional half-UNORM8-code envelope.

The raw-copy logical Y interpretation disagrees strongly with the actual native
sampled lane. No cause is assigned. It is retained as evidence, not used as a
sampling oracle or a reason to discard the independently coherent sampled lane.

## Offline registration result and next implementation

Current-pixel recovery, without a historical fitted map as input, identifies
encoded source session `835dc49d01f049158280f6d714ff57df`, app pair 443 / phase 58,
for both encoded labels. The actual original fragment R1 is `(0, .25, 1, .5)`.
Its nominal complete-canvas chart is consistent with held-out markers and
coverage interiors. The old complete 4 × 4 header-cell reader still refuses edge
ambiguity at that exact nominal chart. A separately identified interior 2 × 2
footprint passes all three full CRC headers with unchanged 64/191 thresholds.
This is a distinct proposed diagnostic profile, not a relaxed old-reader pass.

Fresh PC logs for this session were not supplied; the source tuple is recovered
from the current image. Canonical regeneration and hash agreement must retain
that distinction from independently trusted PC-session provenance. A source app
pair is not a new native video-frame number. One actual invocation also does
not establish a general continuous mapping or physical output-eye assignment.

The next offline components are a bounded direct-R1 registrar and a single-output
same-invocation panel reconstruction. Both color and numeric coverage must use
the same current source, original u/R1, sampler and native reconstruction. This
can preserve the existing native eye route without introducing a separate CPU
left/right table. A fixed host B for only the observed quarter is insufficient.

A future visible presenter must handle every eligible native draw, preserve
original state and resource lifetime, prove the subsequent alpha consumer, and
have a real OFF/invalid-source path even for reusable command recordings. The
first-draw-only SSBO diagnostic is not that presenter. Synthetic numeric coverage
is not real game coverage, and no streaming passthrough is enabled by this run.

Private artifacts, manifests, source pins, rollback proof and reviews remain in
`build/private-native-ssbo-run-20261017-r2/` and the three
`build/private-native-r7-*-review-20261017/` directories. No Valve module,
instruction body or image capture is redistributed here. No unchanged complete
test suite was rerun for this read-only result review.
