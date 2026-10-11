# Selected video surface and compositor resource association

## Result

The inspected receiver stores the decoded frame key and reuse generation beside
a shared-resource token. Its presentation helper copies the **selected record's
token into both eye presentations**. The resource originates in a native Vulkan
YUV image exported as a DMA-BUF. The compositor has a matching message path that
receives plane handles and creates a separate Vulkan image with DMA-BUF memory
import.

This narrows the missing resource bridge considerably. It does **not** identify
the sampled image view for a live streamed draw, establish GPU-consumed eye
uniforms, or associate that draw with the PC's source coverage. A descriptor
snapshot cannot supply the received frame key by itself. Mask activation remains
blocked until those associations and GPU lifetimes are established.

## Identified builds and validation

Read-only analysis used the existing private reference copies:

| Executable | Size | SHA-256 | ELF build ID |
| --- | --- | --- | --- |
| `vrlink` | 2,412,336 bytes | `0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b` | `0a51addb6a9ef05dc52c055b1076947908f20acc` |
| `vrcompositor` | 5,964,624 bytes | `9b1bd5fe4463592cbc71b2517b9ceb0a0fcd66cd6885a19dca4be0784bf6c265` | `ba0d798c2694e05985421e848b67ced8984fe2d3` |

Addresses below are ELF virtual addresses for these exact builds, not portable
entry points. Private instruction listings and binary copies are not tracked.
An ignored validation record retains **74 passing instruction, relocation and
string checks**. Function-pointer identities were checked through API-name
loading, pointer-table offsets and GOT relocations, rather than inferred solely
from a function's apparent argument shape. The compositor renderer's virtual
method was additionally resolved through its RTTI and vtable relocations.

No Frame connection, attachment, file change, GPU readback or runtime hook was
performed for this analysis. Static call paths establish possible native
operations; they do not prove which imported resource the successful passive
session sampled. The existing 39 complete draw/submit pairs establish the shader
tuple only; [their limits remain unchanged](FRAME-TRACE-SESSION.md).

## Receiver: decoded record to shared resource

The existing [received-key analysis](FRAME-ASSOCIATION-DATAFLOW.md) establishes
packet key → decoder timestamp → decoded capture record → selected presentation
record. The following extends that chain into resource metadata:

| Boundary | Verified operation | What it establishes |
| --- | --- | --- |
| Record allocation, `0x33728` | `vkCreateImage` at `0x3387c` writes record offset `0x08`; `vkAllocateMemory` at `0x3396c` writes `0x10`. | Each reusable capture record owns an image and memory allocation. |
| External image layout | The image's `pNext` chain contains `VkExternalMemoryImageCreateInfo` with handle mask `0x200`, then `VkImageDrmFormatModifierExplicitCreateInfoEXT` with two memory planes and modifier `0x0500000000000001`. | The producer image has an explicit, shareable DMA-BUF layout. The modifier must be preserved, not treated as a linear raster. |
| Allocation and export | A dedicated-image allocation chains `VkExportMemoryAllocateInfo` with the DMA-BUF handle type. `vkBindImageMemory` executes at `0x33988`; `vkGetMemoryFdKHR` at `0x339c8` writes record offset `0x18`. | The exported descriptor belongs to this image's bound memory. It is not a video-frame identity. |
| Shared-token registration | A resource-manager accessor at `0x33a40` is followed by a virtual call at table byte offset `0x38` (`0x33a5c`), passing DMA-BUF-shaped attributes and an output at record `0x20`. A reference call uses table offset `0x18`. | Registration supplies the token stored with the capture record. |
| Decoded content | `0x34440` writes the received key at record `0x28`; `0x34444` writes a separate generation at `0x30`. | The image slot, received key and reuse generation are distinct identities. |
| Selected presentation | `0x34a28` chooses a record; `0x34a90` reads its key; `0x34d40` passes that record to `0x33e60`. `0x3407c` reads its token, and `0x34080`/`0x3408c` copy it into the two eye structures. | Both native eye presentations reference the selected surface, including repeated presentation. This is stronger than using the latest decoded record. |
| Private presentation | The helper calls the private compositor interface at table offset `0x140` (`0x340fc`), then public `PostPresentHandoff` at `0x48` (`0x3414c`). | Intercepting only public `IVRCompositor::Submit` does not observe this verified route. |
| Disposal/reconfiguration | `0x33ae0` drops the shared token, closes its exported FD, destroys the image and frees memory. Reconfiguration `0x33b98` disposes and rebuilds the bounded record set. | Resource reuse and codec/extent changes require invalidating old associations. |

The registration attributes and three virtual slots match the current public
`IVRIPCResourceManagerClient` layout: `ImportDmabuf`, `RefResource`, and
`UnrefResource`. This is a **strong ABI inference**, not proof of the private
accessor's interface version. The receiver does not request that public interface
by name. The private presentation structures must likewise not be interpreted
as public `Texture_t` merely because they carry the same resource token.
[Valve's public OpenVR definitions](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h)

The receiver creates sampled, 2D, single-mip/single-layer images. The inspected
format branch selects DRM NV12 when the Vulkan format is
`VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`, and DRM P010 otherwise. Actual format, extent,
strides and plane offsets must come from the live creation/import metadata;
the existence of that branch does not prove that every other format is P010.

## Compositor: DMA-BUF import to native image

The compositor's import-message path at `0x137fc8` receives each plane's IPC
handle through `0x13809c`, constructs the DMA-BUF attributes, and calls import
helper `0x137dd0` at `0x1380fc`. That helper validates the attributes and calls
renderer wrapper `0x6e6c8`. The wrapper invokes its renderer's texture-creation
slot at byte offset `208` (`0x6e7bc`). The verified `CVulkanVRRenderer` vtable
resolves that slot to `0x2bf3d8`.

In the DMA-BUF branch of that renderer:

- `vkCreateImage` at `0x2bf790` receives the external-memory/DMA-BUF and explicit
  DRM-modifier chain assembled at `0x2bf730`–`0x2bf770`.
- Allocation helper `0x2b6618` duplicates the first plane FD at `0x2b671c`,
  constructs `VkImportMemoryFdInfoKHR` with handle type `0x200`, and links it
  with a dedicated-image allocation. `vkAllocateMemory` executes at `0x2b6778`.
- `vkBindImageMemory` at `0x2bf868` binds that imported allocation to the new
  compositor image. Subsequent view creation is a separate resource boundary.

This proves a concrete public Vulkan import path exists in the native receiver
of the resource message. It does not prove that this path's image/view is the
one sampled at set 0/binding 9 in the captured distortion draws. Shared-token
lookup, renderer image selection and descriptor revision must still be joined
for the same presentation generation.

A successful Vulkan FD import consumes its descriptor and creates a distinct
`VkDeviceMemory` object. The OpenVR import API, in contrast, duplicates the
caller's FDs. Consequently, producer and compositor `VkImage`/`VkDeviceMemory`
handles and FD integers cannot be compared as cross-process identities.
[Vulkan FD import contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryFdInfoKHR.html)

Explicit modifier metadata describes memory-plane layout, not source-eye
packing or foveation. Keep it separate from the shader's selected-eye rectangle
and projection data.
[Vulkan modifier contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageDrmFormatModifierExplicitCreateInfoEXT.html)

## What the public metadata diagnostic can establish

A bounded diagnostic can observe existing public Vulkan operations without
changing rendering or reading image/uniform contents:

| Metadata | Useful association | Limit |
| --- | --- | --- |
| Device identity plus image create/destroy generation | Format, extent, usage, external handle type, DRM modifier and memory-plane layout | Matching dimensions/modifiers do not prove matching allocation or frame. |
| Memory allocation/import/export flags and image-memory binding | The compositor image's local memory provenance and effective bind offset | Raw FD numbers are process-local, reusable and not an external payload key. |
| Image-view create/destroy generation | Sampled view's exact image, format and subresource range | A view may display many decoded frames during its lifetime. |
| Pipeline layout, descriptor layouts/updates/copies and command binding | Set 0/binding 9's image view, binding 25's sampler, and whether candidate binding 31 is unused | Mutable descriptors require revision validation at submission. Layout completeness alone does not prove shader semantics. |
| Buffer descriptors and dynamic offsets | Effective offsets for bindings 0, 2, 3, 4 and 6 | No eye selector, selected metadata slot or rectangle value is established without observing coherent GPU-consumed contents. |

Unsupported descriptor operations, unknown `pNext` chains, capacity exhaustion,
resource reuse, missing generations or changed descriptors must mark evidence
incomplete. The diagnostic must not perform `fstat`, deferred FD inspection,
mapped-memory reads, blocking IPC or GPU copies on rendering hooks. Import FD
ownership may already have transferred when a deferred observer runs.

## Missing bridge and next acceptance gate

The minimum complete chain remains:

```text
PC composition + eye coverage
  -> exact encoder input/output association
  -> source session + extended wire-key generation
  -> receiver-selected decoded key + capture reuse generation
  -> shared-resource registration + compositor import generation
  -> submitted descriptor image-view generation
  -> coherent eye selectors + source rectangle + GPU lifetime
```

The public Vulkan layer can fill the last resource columns but cannot recover
the received packet key or source session. A separate, version-verified native
registration/presentation adapter is therefore needed to expose an immutable
copy of the selected key/generation/token association. Its lifetime and mapping
to the imported compositor resource must be proven before implementation; the
static offsets above are not permission to patch private methods.

Capture slots and shared resources persist across multiple decoded frames. Mask
selection must follow the native **selected content generation**, not image
creation order, arrival order, a submit counter, the presentation number, or a
"latest mask". The [PC sender report](PC-DRIVER-20261011.md) separately shows why
its presentation counter cannot replace the wire key.

After establishing this bridge, require one complete record for each physical
eye with the exact sampled view, selected content/session/frame identity,
effective buffer offsets and coherent GPU-consumed selectors/rectangles. Then
validate the [full-eye UV route](FRAME-UV-BINDING-20261011.md), resource fences,
reorder, repetition, key wrap, reset and extent changes. Until all of those
conditions hold, retain opaque output and leave the genuine-mask shader inactive.
