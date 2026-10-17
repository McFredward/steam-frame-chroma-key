# Current-R1 scene-contribution fragment prototype, 2026-10-17

This is a new offline generator, separate from the retired synthetic source and
its historical generators. It accepts only the exact canonical
`scene-contribution-atlas-v1.json` profile, SHA256
`2c1ca215715ec7d2d7591eb2a2e8071a15e11fb5f96530927d10ceab7405e30c`.
It requires the private original PS pinned at
`f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2`.
No original/derived native executable is redistributed in tracked source.

`native_fragment_scene_presenter.transform(original, profile, enabled=True,
descriptor_count=1)` returns bytes and a bounded provenance record. Count3 is an
explicit offline alternative. Disabled returns the exact original bytes. The
new candidate is 19224 bytes, SHA256
`57ce6fdaa983765fe12800002a1f3c53be50291586e16a13d5fbcc0bced49cb3`.

Original descriptor accesses, both implicit samples, the entire function prefix
through original SSA994, and the original current-R1/decoder/reconstruction/tint
semantics are unchanged. Three invalid paths store SSA994 directly. New reads
use explicit LOD0: twelve guard samples (first-cell fast OFF), four panel samples.
No descriptors, subgroup capabilities, device features or CPU eye mappings are
added. The exact existing count1 specialization remains separate and attested.

Global q is clamped to scene texel centers for BOTH scene and coverage. Original
aperture is evaluated at the original unclamped q. Coverage selects one of four
256-row data tiles using `floor(max(q.y*1024-.5,0)/256)` clamped to3, with a global
16-pixel halo and half-pixel center convention. It is not a single affine panel.
Native original color and mask reconstruction weights remain independent.

RGB is the already prepared scene contribution, multiplied by the original
native aperture and tint once, **never by mask coverage**. Alpha is clamped raw-G
coverage times original aperture. Positive emission at A0 therefore survives.
The initial producer contract is Gamma working space and SDR UNorm; this offline
Floatatlas proof is not a proof of the native color/codec chain or refraction.

The guard recognizes twelve inclusive raw-G code windows in the same current
sampled image and R1. It has no historical cache. It is format recognition, not
body authentication, a session/frame/physical-eye identity or native activation
permission. A modified body with a copied valid strip deliberately passes.

Focused evidence: Intel and Lavapipe each pass 146 actual-output cases, with
110 independent actual-original color and raw-G checks, 36 bitwise-original
fallbacks, 45 seams/edges and16 unequal reconstruction-weight cases; real
source-over is checked and A0 positive contribution remains. The separate OFF
route passes146 bitwise-original cases per driver. The coordinate diagnostic
passes81 cases (45 valid seams/edges,36 original fallbacks). All four valid
mutants fail their intended numerical oracle on both drivers. Every module
passes `spirv-val --target-env vulkan1.1`; active Khronos/sync validation reports
zero errors. No old suite or headset run was repeated. Small source/results,
including the first strict-compile failure, are retained privately.
