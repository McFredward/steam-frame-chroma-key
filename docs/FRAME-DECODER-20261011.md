# Frame decoder investigation, 2026-10-11

## Outcome

The inspected Steam Frame executable has an explicit decoded-buffer/metadata
association and a private compositor presentation path. This is a useful place
to establish a real mask/video association, but it does **not** establish a
working bridge. Neither the PC encoder frame identity nor the private compositor
resource ABI has been verified. Do not enable mask composition from these
findings alone.

The initial static inspection preceded the
[read-only live stream inspection](FRAME-LIVE-20261011.md). No process was
attached, and no mask runtime or passthrough headset test was performed.
Follow-up static analysis corrected the timestamp origin below. Addresses are
ELF virtual addresses for the one identified build, not portable hook locations.
The repository contains no proprietary
binary, instruction listing, extracted shader source, or private runtime data.

## Inspected build

| Property | Value |
| --- | --- |
| Executable | `vrlink` |
| Size | 2,412,336 bytes |
| Architecture | ELF64 little-endian AArch64, position independent |
| ELF build ID | `0a51addb6a9ef05dc52c055b1076947908f20acc` |
| SHA-256 | `0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b` |
| Public interface version found | `IVRCompositor_029` |
| Private interface version found | `IVRCompositorSystemInternal_XXX` |

The binary was supplied privately from the device and kept in an ignored build
directory. Capstone and ELF unwind function ranges were used to inspect only
the relevant call paths. Function labels in this report are descriptive; local
symbols are stripped, so these are not recovered Valve API names.

## What is source-proven for this build

### Decoder input and capture retain an association token

**Correction:** the first report confused the input-buffer index with a separate
frame token. The decoder input function at `0x31118` preserves its fourth
argument, copies it to the timestamp source at `0x31200`, and writes that value
to `v4l2_buffer.timestamp.tv_sec` at `0x3124c` before its queue ioctl at `0x31260`.
Its pointer-table lookup supplies the separate `QBUF.index` field. The timestamp
does **not** originate in that buffer-table index.

Further dataflow traces this token to a received 16-bit packet sequence, through
completed-frame bookkeeping and decoder submission. The metadata lookup uses
a small indexed table but also checks the complete native sequence key. This
is an explicit receiver-side association, not a guessed equality of arbitrary
counters. See [the corrected dataflow report](FRAME-ASSOCIATION-DATAFLOW.md).
It does not prove that the sequence equals the PC's public `PresentInfo_t.nFrameId`.

The capture/media function at `0x31cd0` dequeues into a `v4l2_buffer`. It reads
the returned timestamp field at `0x31ec4`, retains its low 32 bits, and calls
`0x34308` at `0x31f5c` with both the CAPTURE buffer index and that token. The
callee uses the latter argument in a diagnostic labelled `unFrameId` and also
reads a separate presentation-number field. A diagnostic variable name does
not establish the protocol meaning of its value.

The standard V4L2 stateful decoder interface preserves the input timestamp on
the matching output, including reordered decode results. That makes this an
exact native receiver-side association mechanism, rather than approximate
timestamp matching. It does not establish how the PC encoder assigns the
received key or associates it with source coverage.
[Linux V4L2 decoder documentation](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-decoder.html)

Sequence wrap, stream/codec restart epochs, surface generation and resource
lifetime must still be established before using this token as an external mask
key. A native 16-bit sequence or reused buffer index alone is insufficient.

### Presentation uses a private compositor path

The renderer function at `0x34820` has the missing-metadata diagnostic and calls
presentation helper `0x33e60` at `0x34d50`. The helper invokes a private interface
at virtual-table byte offset `0x140`; the surrounding path also invokes that
interface at offsets `0x1e0` and `0x238`. Their full ABI and argument semantics
have not been established.

The helper subsequently invokes the public `IVRCompositor_029`
`PostPresentHandoff` method at offset `0x48`. The relevant presentation helper
does not contain a call at that version's public `Submit` offset `0x30`.
Consequently, intercepting only public application `Submit` is **not a proven
way** to observe the Steam Link decoded frame or its imported resource. This
does not claim that no other executable path calls `Submit`.

One argument of the private presentation call originates in the first 32-bit
field of a queued record. Its relationship to the metadata token above and the
resource/surface lifetime is incomplete. It must not be advertised as the PC
encoder frame ID or interpreted by guessing a private method signature.

### Public resource import does not solve frame identity

The current public OpenVR header exposes
`IVRIPCResourceManagerClient_003::ImportDmabuf` and
`TextureType_SharedTextureHandle`. The import describes image dimensions,
format, modifier and backing planes; it supplies no video-frame ID. The
inspected executable contains the private compositor interface string instead
of the public resource-manager/import strings. The public API is therefore a
possible supported resource-import building block, **not evidence that this
build uses it**. [Valve OpenVR header](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h)

## Unresolved links

1. **PC source:** the source application frame, Steam Link's encoder frame and
   emitted stream metadata must have one exact, session-scoped association.
   An application submit counter cannot be substituted for it.
2. **Decoded surface:** connect that verified stream identity to the capture
   buffer, generation and imported resource actually used for presentation.
   File descriptors and slot numbers are reusable resources, not frame IDs.
3. **Compositor binding:** identify the supported resource/descriptor route and
   fences without inventing a private C++ interface ABI.
4. **UV mapping:** establish the exact eye packing, visible crop, foveation and
   reprojection coordinates at the mask sampling point. A correct frame ID
   alone does not prove correct mask alignment.
5. **Lifetime:** retain each mask/resource until its last compositor use and
   invalidate associations on stream restart, codec change or resource reuse.

The explicit-mask transport and shader validation in this fork address local
format/validation concerns. They do not provide these missing runtime links.
Use opaque rendering on any absent, invalid or ambiguous association; neither
the latest mask nor a near timestamp is a safe fallback.

## Minimum next evidence

First obtain a short active Steam Link stream and use existing, bounded runtime
diagnostics where available. Record the executable build IDs and stream session.
Capture only numeric association metadata, not images, private configuration,
full logs or memory dumps. Do not install an executable hook on the basis of
these offsets.

The smallest useful trace would correlate:

- the real PC encoder frame identity and source frame association;
- its received metadata identity before decoder submission;
- the input allocation token, capture buffer index and reuse generation;
- the imported image identity and presentation record using that same frame;
- the eye/crop/foveation mapping and synchronization fences used at presentation.

If existing diagnostics cannot expose the missing columns, stop before runtime
modification. Agree on a narrow, opt-in instrumentation approach for the exact
verified build, with an opaque fallback and a bounded capture. Prefer a
supported API or source integration over patching private executable methods.
No such instrumentation was implemented or approved by this investigation.

A subsequent software test must deliberately cover decoded-frame reorder,
dropped video frames, delayed or duplicate masks, repeated presentation,
buffer-slot reuse, session restart and resolution changes. Only after the full
association and UV mapping have been demonstrated should a headset test enable
composition. This checkpoint is documentation evidence, not a functioning Steam
Link passthrough receiver.
