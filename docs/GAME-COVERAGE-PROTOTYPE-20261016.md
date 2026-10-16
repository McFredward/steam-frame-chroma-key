# Inert game-coverage shader prototype

The existing transported registration pattern is synthetic. Successfully reading
its numeric panel does not turn the game's ordinary framebuffer alpha into
scene coverage. This developer-only fixture begins testing that separate producer
problem without installing anything on the Frame or modifying production shaders.

`tests/unity-game-coverage/run.py` requires an explicitly supplied GloomhavenVR
checkout and the real Unity2021.3.5 editor. It copies four owned production shader
families and their local includes into a disposable editor project. It pins the
original bytes, fixture and runner. No game assets, private Valve shader bodies,
native graphics hooks or PC Steam sender modifications are involved.

The copied variants change only their owned shader names and these alpha rules:

| Known family | Original RGB operation | Candidate alpha operation |
| --- | --- | --- |
| EnvParticleAlpha | Original straight-alpha over | `One OneMinusSrcAlpha` |
| ActorBarSprite | Original straight-alpha over | `One OneMinusSrcAlpha` |
| EnvParticleAdd | Original additive RGB, including original premultiplication | `Zero One` |
| HeadUnlit | Original opaque texture/tint RGB | Fragment alpha1 |

The original `SrcAlpha OneMinusSrcAlpha` factors applied to all RGBA channels
produce alpha `a*a + A*(1-a)`. Scene coverage instead needs
`a + A*(1-a)`. Separate alpha factors fix this difference for these known over
passes. The additive pass must preserve its RGB emission while leaving existing
coverage unchanged; emission at alpha0 is valid and must not be discarded.
An opaque pass needs coverage1 even when its albedo carries a different alpha.

These are explicit per-family semantics, not a general blend-state inference.
Later passes that use destination alpha or sample changed RT alpha can change RGB
indirectly. Unclassified passes, alpha feedback, original game materials and UI
intermediate RTs remain outside this prototype. It enables no source tap, live
coverage export or passthrough.

## Actual evidence

An Intel RPL-P Vulkan run executes both Gamma and Linear Unity projects, each
with 36 cases / 319 checks over admitted ARGBFloat RTs at 1/2/4/8 samples. It
checks transparent clear, one/two half-opacity layers, additive emission alone
and over glass, opaque coverage, UI rect clipping and ColorMask0 stencil setup.
The actual original over passes produce .25/.375; the candidate produces
.5/.75. Additive emission remains nonzero at coverage0. Tested RGB values agree
within the fixture's fixed .001 tolerance. This is not a bit-identity claim.

All observations come from one fully covered interior pixel. **These runs do not
test MSAA boundary sample coverage/resolve, alpha-to-coverage, a subsequent stencil
test, soft-mask/alpha-clip edges, HDR color processing, every game shader or a
Windows D3D11 runtime.** The stencil case tests zero color writes only. Listing
the admitted MSAA counts does not remove those limits.

Private receipts retain the copied inputs, exact changed shader texts, original
pixel values, Unity/driver identity, source pins and logs at
`build/private-game-coverage-20261016/run-ip8a9jdk/`. Processes are reaped before
calling the shared completion-cleanup API and removing owned generated projects.
A later runner-only cleanup-accounting correction is syntax-checked and its
receipt confirms both projects absent; the unchanged pixel experiment is not
repeated for that bookkeeping edit.

Next producer work needs additional edge/UI composition controls and audited
material families before a diagnostic same-eye source tap. That tap must preserve
the displayed ordinary source and freeze RGB contribution plus coverage from the
same real eye source/resolve, with observed format/color space and completed pair
lifetime. A latest mask or a scheduling counter cannot identify a native decoded
video frame. The eventual scalar composition convention is
`scene_contribution_rgb + (1-coverage)*passthrough_rgb`, retaining emission at
coverage0. Colored transmission and refraction require additional semantics.

The Frame remains restored; no new device agreement or restart follows from this
offline prototype. There is no timer or automatic rollback.
