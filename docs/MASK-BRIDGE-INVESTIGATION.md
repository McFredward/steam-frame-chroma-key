# Steam Link mask bridge investigation

Status: source and read-only installed-runtime investigation, 2026-10-11.
The actual Frame shader has now been inspected on SteamOS 0.3.0, build
20260922.6101926. Its interface and source-coordinate dependencies provide
concrete integration constraints. A decoded-frame identity and the complete
live sampling transform remain unproven. This document separates the working
upstream chroma hook from the genuine-mask work.

## What the source establishes

- The upstream compositor layer identifies the local installed distortion
  shader by a hash and can substitute SPIR-V at shader-module creation. It
  recognizes YCbCr immutable samplers and their pipeline layouts. The underlying
  NV12 decoder surface has no alpha channel; alpha currently comes from color
  keying inside that shader.
- `QueueSubmit` counts GPU queue submissions. One video frame can cause many
  submissions or be reused for several display frames. This counter is not a PC
  source-frame identity and cannot pair an externally received alpha image.
- The layer currently does not intercept descriptor updates/binds or associate
  a sampled decoded image with decoder metadata. Vulkan image handles identify
  allocated resources; reused surfaces need a new frame identity each decode.
- Valve's public [`Submit` API](https://github.com/ValveSoftware/openvr/wiki/IVRCompositor%3A%3ASubmit)
  supplies an eye texture and optional texture bounds.
  [`VRVulkanTextureData_t`](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h)
  describes its Vulkan image/device/queue and dimensions, with no arbitrary
  encoder/decoder packet ID field. Additional pose/depth structs describe render
  inputs, not a public custom streaming metadata channel.
- [`Compositor_FrameTiming`](https://github.com/ValveSoftware/openvr/wiki/Compositor_FrameTiming)
  exposes a runtime frame index for timing history. The documented API does not
  establish equivalence between a PC index and a Frame decoder surface's index.
- The [OpenVR driver header](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr_driver.h)
  has an important candidate: `IVRVirtualDisplay::Present` receives a backbuffer
  handle and `PresentInfo_t::nFrameId`. This may identify the PC encoder input
  **if the deployed vrlink driver uses that component**. Its existence does not
  prove that the ID survives streaming or is visible at the decoder. The
  direct-mode `SubmitLayer`/`Present` interface has different inputs, so this
  candidate must be verified before choosing an interception point.
- Valve's [Steam Link vendor integration](https://github.com/ValveSoftware/openvr/wiki/Steam-Link-Integration-for-External-Vendors)
  exposes resource-only driver settings. Stream width and eye resolution both
  affect foveation. Dimensions alone therefore cannot establish the correct
  inverse packing transform. These settings do not document a custom alpha
  transport channel.

These are API/source constraints, not proof that an internal Steam Link bridge
is impossible. A runtime trace may reveal a usable private surface/packet link.

## Correctness requirements for the bridge

The PC must export alpha from the same rendered stereo input as the streamed
color. Its session, source-frame ID, eye, extent, UV convention and alpha
convention must travel explicitly. The Frame must associate that identity with
the exact decoded surface sampled by the selected draw. A decoded surface reused
for reprojection must retain its original alpha, not adopt the newest mask.

If the video encoder receives a compositor-produced frame rather than the
original application input, its pose and alpha need the same composition and
reprojection. An application `Submit` ID alone is insufficient when that relation
is unproven. Queue timing, receive timing, nearest timestamps and "latest mask"
matching are not an acceptable substitute for this link.

The mask then uses the same eye selection, valid rectangle, packing/foveation,
crop and reprojection coordinates as color. Sampling a full-resolution mask at
an output-space fragment coordinate will cause movement-dependent edge errors.
Premultiplied scene output must agree with the camera blend; multiplying an
already premultiplied source by alpha again darkens edges.

GPU upload, descriptor allocation, command-buffer reuse, host writes, semaphore
ordering and mask-image lifetime must also be safe with multiple frames in
flight. A missing, mismatched or stale mask leaves the stock opaque scene. It
must never accidentally make dashboards or unrelated applications transparent.

## Concrete next integration approach

1. Collect the actual shader and metadata below on the Frame. Privately inspect
   its sample coordinate dependency graph, image bindings, push constants and
   per-eye layout. Do not rely on upstream's numeric `%883` ID across builds.
2. Add an opt-in bounded runtime trace of decoded-image creation/import, image
   views, descriptor writes/binds and target draw selection. Trace stable internal
   packet/surface identities only after verifying the deployed build. The static
   collector below deliberately does not claim to replace this trace.
3. Inspect the deployed PC vrlink driver's component interface. If it uses
   `IVRVirtualDisplay`, trace its `PresentInfo_t::nFrameId` and backbuffer through
   the encoder and receiver. Otherwise identify the actual equivalent. Validate
   a PC encoder-frame to decoded-surface link. If no reliable private link
   exists, prefer a genuine mask encoded alongside color **in the same video
   frame**, with explicit layout/reconstruction. That avoids side-channel temporal
   pairing but still requires verified access to the encoder input and correct
   foveation/crop treatment. It is a proposed alternative, not an implemented path.
4. Bind the mask only when its frame and mapping match. Test asymmetric eyes,
   reconnects, surface reuse, dropped/out-of-order masks, repeated video frames,
   resolution changes and rapid head motion before enabling it for a game.

The camera image remains local in either approach. It need not be sent to the PC.
The current fork's mask transport/CPU tests can verify packet and matching logic;
they cannot establish the Steam Link bridge or headset visual correctness.

## Read-only Frame investigation, 2026-10-11

The installed native shader and decoder executable were read over SSH and
inspected privately on the development host. No layer, shader, runtime setting,
or diagnostic process was installed or activated on the headset. The only
authorized headset write was confirming native SSH service boot enablement.
The device was already running SSH from its most recent boot; the initially
refused connection used an obsolete IP address.

Observed installed-runtime metadata:

| Component | Observed identity |
|---|---|
| SteamOS | 0.3.0, build 20260922.6101926, variant `vr` |
| Distortion shader | 6,620 bytes; SHA-256 `f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2` |
| `vrcompositor` | ELF build ID `ba0d798c2694e05985421e848b67ced8984fe2d3` |
| `vrserver` | ELF build ID `5aa95363d05998027c870b98e01281f46d2b3ab5` |
| `vrlink` | 2,412,336 bytes; ELF build ID `0a51addb6a9ef05dc52c055b1076947908f20acc` |

The shader's existing opaque-store anchors match the upstream patcher, but
that does not establish mask-coordinate compatibility. It uses an image array
at set 0/binding 9 and a separate sampler at binding 25. The candidate `%883`
coordinate belongs to edge normalization, rather than either native color
sample. A logical full-eye mask at this coordinate requires a separately
verified eye/rectangle transform; a packed decoded-video mask cannot simply
use it.

The installed decoder contains frame metadata handling and explicit missing-
metadata cases. This is a useful interception lead, not proof that an external
application frame ID survives encoding, decoding and compositor surface reuse.
Static strings alone do not establish an ABI or a reliable pairing contract.

Detailed metadata and remaining coordinate constraints are recorded in
[the installed-shader inspection](FRAME-SHADER-20261011.md) and
[the decoder association investigation](FRAME-DECODER-20261011.md). The bounded shader
type-reuse repair was verified with the 44 focused shader tests and the headless
Vulkan rendering/guard test. Both generated versions of the private native shader
were also checked against the legacy layer's guard and correctly rejected while
unbound. Transport, endpoint and collector evidence remains
the earlier complete foundation gate; those unchanged areas were not rerun for
this repair.

## Local diagnostics collector

Run on the Frame, from a checkout of this fork:

```bash
python3 tools/collect_mask_bridge.py --output-dir "$HOME/frame-mask-diagnostics-1"
```

Use a new or empty output directory each time. If SteamVR is elsewhere, add
`--steamvr-root /path/to/SteamVR`. If the deployed `vrlink` executable location is
known, add `--vrlink /path/to/vrlink`; it hashes that file without executing it.
The collector reads the expected installed distortion shader, known compositor
and server executable locations, selected OS build fields, and Vulkan ICD
descriptor names/API versions/library basenames. Known version-file locations
are recorded by hash only. It uses `spirv-dis` and
optionally `readelf`, with size and execution-time limits. It does not initialize
Vulkan, modify SteamVR, restart services, attach to processes, read user Steam
configuration or logs, inspect environment variables, or send data over a network.

- `report.json`: selected shareable metadata: hashes, ELF build IDs, SPIR-V
  interface/descriptors and image-sample operand IDs. Absolute home paths and
  arbitrary shader strings are excluded. Verify it before sharing.
- `report.md`: a short reminder of the collection scope.
- `private/distort_ps_achromatic_se2.spvasm`: local disassembly needed for exact
  shader analysis. This is Valve runtime code; **keep it on the headset and do not
  publish or commit it**. No shader binary is copied. The output directory is
  private to the user and includes a deny-all `.gitignore`.

Missing files/tools produce metadata warnings. The report always says that
decoded-frame identity and coordinate mapping remain unproven. Finding expected
patch markers is not a compatibility certification. No installation or runtime
passthrough setting changes are performed by this collector.

Focused verification:

```bash
python3 -m unittest discover -s tests -p 'test_collect_mask_bridge.py' -v
```

The tests use synthetic reflection fixtures, verify private/public separation,
reject unsafe output reuse, detect shader marker changes, and exercise subprocess
timeout/output bounds. When SPIR-V tools are available, an end-to-end test
assembles and collects our own tiny shader. They require no headset and contain
no Valve binary or disassembly payload.
