# Changing application registration source, 2026-10-16

The [completed final-draw uniform capture](NATIVE-UNIFORM-CAPTURE-20261016.md)
used GloomhavenVR676's fixed artwork. Its static identity cannot establish that
a changing color frame and coverage plane still belong together. GloomhavenVR677
adds the next synthetic source in the existing mod's opt-in test, without
modifying the native PC Steam sender or enabling passthrough.

The copied native uniforms also retain an unresolved host-write lifetime:
post-draw contents have not independently been proved identical to shader-read
values. GPU publication fences alone do not supply that native CPU-writer
contract. The new source/GPU tests do not close or silently waive this gap.

## Source contract

Each explicit run has a fresh nonzero128-bit app session. Monotonic64-bit pair
IDs reserve one frozen pair before the actual left/right eye callbacks; the
Unity frame count is only a scheduling cohort, not a native video-frame ID.
Both eyes use the same pair and distinct source-eye identity. Incomplete begun
pairs, duplicate eyes and stale/unknown admissions cannot become completed pairs.
Menu/switch/camera/session termination restores the ordinary output; no expiry
timer or automatic restart is introduced.

The fixed688x548 RGB8 canvas contains320x192 color and grayscale coverage
planes. Three complete132-byte headers retain the existing exact byte format,
session, app pair ID, eye, layout and plane SHA256/CRC. The new wrapper is
`frame-app-registration-source-v1`, profile`dense-asymmetric-registration-v1`.
Dense asymmetric backgrounds, differing eyes, small moving landmarks and
coverage codes0/64/128/191/255 plus192 overlap distinguish orientation, source
coordinates and changing content. Expected bytes reproduce from the explicit
session/pair/eye tuple; there is no latest-receipt join.

Generation, hashing and upload occur outside eye callbacks. Three owned stereo
texture slots cannot be rewritten until a CPU graphics fence queued after their
prior blits passes. Pool pressure skips a whole cohort, not one eye. Providers
without CPU-fence support explicitly retain the old static profile; the PC's
actual D3D11 support still needs runtime evidence. Bounded Debug receipts retain
only the first four completed CPU pairs and actual callback matrices/targets;
they do not claim GPU, native eye/video or application-ray authority.

## Local software and actual graphics evidence

Existing1,225 lifecycle controls, new64,789 production source/pair controls and
167 independent unchanged-header/CRC/SHA controls pass. Actual game-Unity typing
and complete Debug/Release mod builds have zero warnings/errors. This is focused
and composed evidence, not a repeated complete mod test gate.

Real Unity2021.3.5/Intel Vulkan runs separately validate Linear and Gamma:

| Observation | Linear | Gamma |
|---|---:|---:|
| GPU/production assertions | 113 | 110 |
| Exact original headers recovered | 24 | 24 |
| Actually pending, unchanged slots | 3 | 3 |
| Actual texture reuses after GPU fence | 2 | 2 |
| Maximum RGB8 code error | 0 | 0 |

Eight eye outputs per profile are checked after real rendering, with real
vertical-flip negative controls. Pending observations precede readback, so a
synchronous read cannot hide incorrect pool reuse. An initially empty Editor
scene needed explicit fixture-only command submission; production sources were
unchanged. Windows D3D11, real XR callbacks and native Steam Link decoding are
not simulated by these tests.

## Strict offline reader

`tools/registration_source_recovery.py` validates the explicit new wrapper and
owned expected tuple, all exact extents/placements, raw planes, SHA256, CRC and
every authored0/255 header pixel. It never reimplements the C# generator or
loosens the original static full-canvas validator. Ten new focused methods
exercise unknown/stale/mixed identities, CRC/artwork failures, field/type/size
limits and the CLI.

Optional comparisons accept only a caller-supplied, already mapped688x548 RGB8
or sampled-G grayscale image and an explicit mapping-provenance record. They
check all three headers, report whole-canvas/plane/grid code errors and at most32
control points. The label is always `FITTED_DIAGNOSTIC_ONLY`: no warp or source-ray
transform is invented. Even exact code agreement grants no decoded integrity,
native frame/eye/map or alpha authority. A matching header with damaged artwork
is reported as an error, not silently admitted.

The actual C#-generated and Unity-rendered Linear-left pair1 and Gamma-right
pair4 artifacts also pass the new CLI with zero code error. The actual flipped
GPU output refuses. These are source-canvas round trips, not newly acquired
native decoded images. The separate CMake registration passes; unchanged full
fork/mod suites are inherited rather than replayed locally.

Private evidence is retained under
`build/private-registration-gpu-review-20261016/`,
`build/private-registration-reader-20261016/` and
`build/private-registration-integration-20261016/`. Regenerable caches are
removed after processes stop; original source/output bytes and failed controls
remain preserved.

The Frame remains restored and user-confirmed usable. A fresh native capture
needs a newly reviewed package and readiness agreement. Changing decoded IDs,
matching application rays under streaming transforms/composition and real
fractional game coverage remain separate blockers before live alpha output.
