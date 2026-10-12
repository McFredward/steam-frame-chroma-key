# Next PC adapter: observe composition through the real TX key

Plan: 2026-10-11, based on `dev` checkpoint `92847f6`. No Windows adapter was
installed or executed for this plan. No PC driver, Frame file, game source or
rendering was changed. The current Frame capture can establish public Vulkan
resource metadata without journal loss; it still cannot identify a source video
frame or prove the GPU-consumed eye mapping.

## Decision

**Maintainer steering, 2026-10-12:** prefer an implementation with no additional
modification, injection or private hook in the native PC Steam sender. The
GloomhavenVR application still needs genuine eye coverage. First investigate
an app-owned route through the unchanged stream; a separate mask channel needs
an independently proven selected-video identity and composed UV relation. Do
not treat that requirement as automatically authorizing a sender hook.

The private-driver observation plan below is retained as a documented alternative,
not the selected next implementation. It requires a separate decision if the
app-owned feasibility gates cannot be met. See the
[no-sender-modification review](PC-WITHOUT-SENDER-MODIFICATION.md) for those gates.

The previously planned runtime investigation is on the **Windows sender**, observing
one actual composition through encoder input, encoded output and native TX-key
allocation. Another public mirror poll or Frame descriptor-only capture cannot
close this missing relation. Build an opt-in, version-guarded metadata observer
first; add genuine coverage rendering only after that observer establishes the
active path and resource ownership.

The supplied [PC driver investigation](PC-DRIVER-20261011.md) already proves the
following static chain for one build:

```text
DirectMode layer acquisition and composition
  -> native NV12/P010 composition output
  -> encoder input slot + copied presentation metadata
  -> encoded output + that slot's metadata
  -> queued TX record
  -> native 16-bit key assignment + wire header metadata
```

The output is already luma/chroma, without a coverage plane. Coverage therefore
has to be retained **before** RGB-to-YUV conversion. Reading this encoder
texture's alpha, or recognizing a green background, cannot recover it.
[Microsoft's format definitions](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format)

Use the existing private validation results instead of repeating broad
disassembly. They establish 43 static assertions against SHA-256
`1e846be041039798580a91452371f6288c614fb4c152126b137e2c693fb77eef`.
They do not establish which branch or encoder backend ran in a live stream.
Private reference binaries and instruction listings remain untracked.

## Smallest passive observation set

The RVAs below identify reviewed locations in that exact file. They are candidate
observation boundaries, not a supported hook ABI or an installation instruction.
Before attachment, validate the loaded module, relocations, concrete vtable
dispatch and argument layout. An unsupported binary or a different active path
leaves the observer inactive; do not search for a vaguely similar signature and
continue. A transparent public OpenVR observer does not provide the private
encoder/TX callbacks below.

| Boundary | Existing static evidence | Minimum immutable event and unresolved check |
| --- | --- | --- |
| Connection and active renderer | HMD dispatcher `0x9e55d0` exposes DirectMode 009 and DisplayComponent 003. | Connection creation/reset/disposal generation, active component, device identity, codec/backend and extent generation. The actual native connection-lifecycle entry points still need targeted identification. |
| Acquired source layers and composition | `SubmitLayer` `0x9ed780`, `Present` `0x9ea110`, composition `0x9f3530`. | Layer/eye order; acquired texture and content generation; slice/subresource; bounds; actual transform/foveation revision; native synchronization ownership; composition result and output generation. Establish the exact acquisition/release path and coherent GPU-consumed transforms before exporting pixels. |
| Composition output passed with metadata | Surface accessor and encoder submission at `0x9e9bda` / `0x9e9beb`. | One relation between the completed composition output generation, bounded metadata copy and encoder-submission result. This branch is conditional; a callback observed elsewhere is insufficient. |
| Actual encoder input | NVENC candidate method `0xa210d0`; source copy `0xa217f5`, metadata copy from `0xa21815`. | Device, source subresource, acquired input slot plus its new content generation, metadata revision and submission acceptance/failure. A reusable slot or `ID3D11Texture2D*` alone is insufficient. If another backend runs, this candidate remains unsupported until its equivalent boundaries are verified. |
| Encoded output and TX queue handoff | NVENC worker `0xa1dae0`; output forwarding `0xa1dd4a`–`0xa1dd61`, `0xa2e590` and `0xa44ff0`. | The exact completed input generation, output byte count/status, copied metadata and TX-queue record generation. Do not copy the encoded video body for this metadata investigation. Identify skipped/cancelled inputs and asynchronous slot reuse. |
| Serialized TX-key allocation | TX worker `0xa44a60`; counter read/increment `0xa44c70` / `0xa44c7f`; initializer call `0xa44ccb`, concrete initializer `0xa45ea0`. | Actual queue record, full 16-bit key, explicit key-reuse generation, connection generation, metadata and initializer/dispatch result. Determine where final drops/cancellations and retransmission occur; allocation alone does not prove delivery. |

Each event must copy values while the native caller's lifetime and synchronization
make them valid. Do not enqueue a pointer to a stack metadata block, mutable
encoder slot, unlocked TX record or later-reused texture. Observing a function
entry and a return is not enough unless the intermediate native ownership and
acceptance semantics are known.

Keep these identities distinct:

- Native connection generation and negotiated 128-bit mask session.
- D3D device/adapter identity, with a generation across device recreation.
- Resource creation generation and **content generation** within that resource.
- Composition result, input-slot generation, encoded-output record and TX record.
- Driver-owned 32-bit presentation metadata field and native 16-bit TX key.
- Explicit key-reuse generation and the eventual 64-bit source-frame identity.

A local observer event ticket orders evidence; it is not a video-frame identity.
Assign a mask source identity to one verified composition result, then carry it
through the observed native relations. Different encoded outputs may repeat one
composition; a composition may be dropped without producing an output. Model
those relations explicitly instead of requiring numerically equal counters.

## Connection reset, wrap and repeated presentation

Advance the native key generation at the **serialized allocator**, before reuse,
not when packets arrive. At 90 outputs per second a 16-bit key can wrap in about
12 minutes; a successful short trace says nothing about this case. Any missed
allocation or reset event invalidates the affected session's proof.

Sender observation alone does not give the Frame that generation. The inspected
receiver carries the full key through decode and selects a specific record with
metadata. An authenticated control mapping must be joined to that **selected
native record**, with an independently proven connection/reset association.
Receiving a new session handshake or a mapping for the same key is not evidence
that an older decoded record belongs to the new generation.

One candidate discriminator is the selected record's copied native metadata
paired with the key and connection generation. Its first 32-bit presentation
field is already traced on both sides. Live evidence must establish its producer,
copy lifetime, repetition rules and uniqueness over the retained window. If the
same tuple could identify two generations, retain opaque output; use a verified
additional native discriminator or a drained reset barrier. Do not infer a TX
key from that field, hash collision-prone metadata into an identity, or accept
the latest side-channel mapping. A presentation-counter wrap needs an explicit
fresh-session/barrier contract, independently of the 16-bit TX wrap.

On the receiver, the later native adapter must supply selected key/generation,
decoded-surface content generation, shared-token generation and compositor import
content generation. Repeated presentation resolves the same source mask and
retains its GPU image until all uses complete. An allocation/import generation
does not track changing decoded contents. The existing
[exact-association ledger](EXACT-FRAME-ASSOCIATION.md) provides bounded validation
of these scalar relations; it cannot manufacture native evidence.

## What public interfaces can contribute

| Interface | Useful contribution | Why it cannot complete this adapter |
| --- | --- | --- |
| Public application submit | Genuine source-eye textures, declared bounds/pose/depth and an application-owned coverage pass. | Later layer composition, reprojection, foveation, drops and repetition can change the encoded result. No actual encoder or TX identity is returned. |
| DirectMode 009 | Actual per-layer eye handles and composition inputs, when observing the active driver route. | `Present` supplies synchronization information, not VirtualDisplay's public frame ID or a TX callback. |
| Public mirror SRV | An independently acquired undistorted eye image. | No atomic encoder-input association, frame identity or guaranteed scene coverage. Polling faster does not add that contract. |
| VirtualDisplay 002 | Its own `PresentInfo_t.nFrameId` for drivers exposing that component. | The inspected vrlink HMD dispatcher does not expose it. Installing another display-redirect driver would change the streaming route. |
| D3D11 resource and asynchronous-query APIs | Resource descriptions, local ownership and verified GPU completion for a deliberately owned coverage/readback path. | No native connection, input-slot content generation, TX key or scene semantics. Texture descriptions alone do not prove coverage. |

These public method contracts are defined in the pinned
[OpenVR driver header](https://github.com/ValveSoftware/openvr/blob/0924064316de3effbcd1acf1e309182a2deb1c05/headers/openvr_driver.h)
and [application header](https://github.com/ValveSoftware/openvr/blob/0924064316de3effbcd1acf1e309182a2deb1c05/headers/openvr.h).
ETW/frame timing, timestamps, a compositor submit count and a generic encoder
API hook do not supply the missing relations either. They can measure work, not
identify which mask belongs to one native selected frame.

## Coverage architecture after observation succeeds

1. **Real application coverage.** Add an explicit PC alpha-export mode to
   GloomhavenVR, separately authorized from this fork's investigation. Its current
   PC chroma clear is fully opaque. Preserve opaque geometry, partial transparent
   coverage and antialiased edges, with an explicit straight/premultiplied RGB
   convention. Do not request an unsupported PC runtime alpha-blend mode or derive
   coverage from RGB. Test opaque green and red, transparent black, partial
   coverage, overlapping UI and opaque SteamVR dashboard layers.
2. **Native-matched coverage composition.** Before the inspected NV12/P010
   conversion, compose an owned R8 coverage target using the **same acquired layer
   generations, eye selection, order, bounds, transforms and foveation** as the
   native color result. If an existing intermediate retains verified composited
   alpha, extract it there. Otherwise a parallel pass is required. Four-channel
   shader samples and blend-state presence do not establish meaningful alpha.
   Unsupported layers/transforms retain opaque coverage.
3. **Owned GPU lifetime.** Preallocate a bounded ring of targets, staging resources
   and completion state; compile original project shaders during setup. Enqueue
   coverage work under the native context owner's established acquisition,
   before source release/reuse. Prove ordering against the matching encoder copy.
   Restore every changed state and do not mutate the native video target.
4. **Worker export.** Once completion is known, an owned CPU worker handles
   readback bytes, compression and authenticated transport. Retain the associated
   source identity through encoder output and actual TX allocation. Export no
   mask for rejected or ambiguous work. Do not block color delivery waiting for
   coverage; a missing matching mask means opaque presentation on the Frame.
5. **Receiver use.** Select the mask only through the complete native association,
   then sample it through the verified eye/crop/foveation/reprojection route.
   Camera composition stays on the Frame; no camera video is uploaded to the PC.

Thread ownership matters: a D3D11 device context is not safe for concurrent use.
Moving work to a worker does not permit that worker to operate on the native
immediate context without a proven ownership mechanism. Do not add blocking
locks or enable generic multithread serialization in the render path as a fix.
Use the native owner for bounded command enqueueing; keep CPU processing separate.
[Microsoft's threading contract](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)

Query completion also has a context restriction: `GetData` uses the immediate
context and may report that results are not yet available. The future export
must use a verified nonblocking completion mechanism compatible with context
ownership, rather than a worker spinning on that context or a blocking staging
map. Completion method/capability selection is still an implementation gate.
[Microsoft's asynchronous-query contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)

The existing lossless raw/RLE [mask protocol](MASK-TRANSPORT.md) is a foundation,
not a measured 90-Hz production transport. Measure actual coverage bandwidth and
latency before selecting bounded compression/deltas. Same-video-frame packing
would still require native pre-YUV composition and receiver mapping, and its
codec compression is a separate coverage-quality tradeoff. It is not the next
passive observation step.

## Bounded observer and acceptance sequence

The first Windows executable should contain only version/ABI checks, native
metadata observation, fixed object-generation tables, a bounded ordered event
journal, a background validator and a private report writer. No GPU readback,
image contents, coverage pass, mask transport, shader replacement or Frame
activation belongs in that first measurement.

Hooks copy bounded scalar metadata and publish immediately. They perform no
allocation, file/socket I/O, waits, shader compilation or deferred dereference.
Unknown calls, capacity loss, missing lifecycle events or unsupported counts
make the relevant evidence incomplete. Detailed traces are limited to the
explicit diagnostic session; background summaries expose loss and unsupported
branches. Do not hide pre-arm resource/connection loss.

Proceed in these gates:

1. Identify connection lifecycle and the exact synchronization/argument layouts
   still missing from the static route. Implement portable scalar-ledger tests
   and refusal controls before a Windows observer can attach.
2. Validate a supported active Windows stream, with stable per-event ownership
   and complete composition-to-TX relations. Confirm actual backend and code
   branch. The observer must not change the rendered picture or native ordering.
3. Test skipped input, encoder failure, slot/resource reuse, reconnect, device
   recreation, extent/codec changes, TX-key wrap and repeated presentation.
   Synthetic tests cover state handling; a native run must establish actual
   callback semantics. Missing cases remain explicitly unverified.
4. Add and validate real source coverage and native-matched GPU composition,
   including independent pixel controls and unchanged native color output.
5. Close the Frame selected-record/resource/eye/coherence gates, validate trusted
   transport and GPU lifetime, then consider opt-in mask activation.

No implementation should start at gate 5 merely because the descriptor journal
is complete or the explicit-mask SPIR-V validates.

## Required PC help and validation scope

The supplied driver and manifest already satisfy the initial static-input
request. Before the first PC observer measurement, obtain the current SteamVR
build and a bounded successful-stream initialization excerpt identifying backend,
codec, extent and connection version. Re-run the existing collector if the driver
changed. No full configuration, credentials or memory dump is needed.

An actual Windows observer requires user access to the streaming PC and explicit
approval of its exact loading/attachment mechanism, diagnostic duration, bounded
output paths, SteamVR interruption and rollback procedure. Existing Frame
diagnostic approval does not authorize replacing or patching the PC driver. A
breakpoint experiment that stalls rendering is not the proposed passive observer.
Plan rollback before installation; do not overwrite the supplied/installed driver
or silently restart SteamVR.

This document-only checkpoint reuses the 43 existing version-pinned static
assertions and prior portable association/protocol evidence. Local validation
checks document links, file scope and whitespace. No runtime, GPU coverage,
network-throughput or headset verification is claimed, and no complete software
gate was rerun for this plan. Completed local generated artifacts are cleaned;
private source/capture evidence is preserved.
