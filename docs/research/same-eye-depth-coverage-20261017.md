# Same-eye depth attachment coverage experiment

This is an offline Unity2021.3.5 experiment, not an installed game effect or a
claim of complete Gloomhaven coverage. It preserves the existing forward scene
render. At `AfterForwardOpaque`, two fullscreen `ColorMask A` draws reset alpha
to0 and write1 only at samples whose existing scene depth is nearer than far.
No `_CameraDepthTexture`, shadow-caster silhouette, second scene render, pixel
classification or color key supplies coverage. The test inherits the camera's
actual color/depth/MSAA target, then observes the actual `OnRenderImage` source.

The untouched owned HeadUnlit shader deliberately returns albedo alpha.17.
Its original red channel is1, so its independently resolved visible red values
give an oracle for opaque geometric/sample coverage. A separately copied cutout
variant retains discarded samples; over/additive variants use the previously
reviewed independent alpha blend rules. Whole64x64 original/candidate RGB values
are compared exactly, rather than testing a bookkeeping flag.

## Evidence

| Actual backend / color space | Cases / assertions | Scope |
| --- | --- | --- |
| Intel RPL-P Vulkan / Gamma |64 /491,820 | ARGB32 and ARGBFloat,1/2/4/8 samples |
| Intel RPL-P Vulkan / Linear |64 /491,820 | Same admitted formats/sample counts |
| llvmpipe OpenGLCore / Gamma |32 /245,920 | Non-reversed-Z backend; admitted1/4 samples |
| Intel Vulkan / Gamma, focused additions |24 /196,716 | AlphaToMask, stencil-clipped color and stencil-only masks |
| Intel Vulkan / Linear, focused additions |24 /196,716 | Same new cases, unchanged earlier paths inherited |

All compared RGB channels are exactly unchanged. Maximum measured coverage
error is.001961 in the quantized over cases; float comparisons use their
separately declared.0015 bound and pass. Opaque/cutout/depth-stamp edge values
match the independent red oracle. At2/4/8 Vulkan samples the opaque triangle has
102/135/157 partially covered pixels. AlphaToMask has1,551 and1,552 partial
pixels at4/8 samples. These are actual resolved sample observations, not a claim
of all driver sample patterns or all native shaders.

Both older shared-alpha accumulation and deliberately reversed ShaderLab depth
comparison are causally rejected. Depthless visible color is explicitly detected
as **unsupported**: it has RGB but no scene depth and the stamp cannot infer its
coverage. Additive emission is observed nonzero at coverage0. Cutout controls
require both surviving and discarded pixels. Stencil-only masks introduce no
color/coverage; stencil-clipped geometry follows the actual surviving depth.

The first launch lacked `ExecuteInEditMode`, so no image-effect callback occurred.
The second reached real pixels and rejected an incorrect double reversal of the
depth comparison. ShaderLab comparisons are logical camera-depth comparisons:
Unity already reverses the native comparison on Vulkan/D3D. Use logical
`Greater` for the far triangle on both conventions; only its raw clip-space far
vertex changes0/1. Those failures and their exact source copies remain retained.
The final additional cases were run as a focused subset, not by repeating the
already passing base cases.

Private source pins, copied fixture/shaders, individual pixel/error summaries,
Unity logs and cleanup receipts are under
`build/private-depth-coverage-20261017/`. Processes are reaped before the shared
`cleanup_completed_run` call; generated private Unity projects are removed on
success and failure. This work changes no game asset or production camera.

## Remaining integration requirements

- Windows D3D11 and actual XR eye-target/event routing have not executed here.
  Logical ShaderLab behavior on two host conventions is useful evidence, not
  proof of those missing runtime paths.
- The opaque prefix can retain its original alpha until this boundary, avoiding
  new earlier destination-alpha dependencies. Its RGB background still needs a
  separately owned transparent/black MR clear and suppressed sky geometry.
- Far-plane color, depthless or late opaque draws, later depth clears and unknown
  command buffers need explicit admission. Do not move the stamp after a depth
  clear and assume the old scene silhouette remains available.
- Transparent suffixes still need audited alpha equations. UI intermediates,
  destination-alpha RGB feedback, distortion/refraction and arbitrary native
  VFX are not certified by opaque depth. Data RT alpha must remain untouched.
- The resulting contribution RGB and coverage must be captured from the same
  admitted eye/resolve. A new contribution profile must preserve emission at
  coverage0; the old straight-RGB synthetic decoder is incompatible.

The maintainer has requested removal of the synthetic test-pattern setting from
the shipped mod. This standalone fixture is an internal, non-shipped experiment
supporting the real-image MR path, not a replacement player-facing test mode.
