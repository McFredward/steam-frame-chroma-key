# Native recording callback identified, 2026-10-17

The approved `native-ssbo-20261017-r1` run loaded against all 82 actual native
imports with exact-provider isolated-load verification. Original startup and
sampler gates passed; the maintainer confirmed the moving PC-VR test pattern.
CaptureID: `724593dd48cb378cea4215552344fc67`.

The original-output clone became ready for pipeline 281472362934688,
generation 143. A later Begin adopted command generation 22775, pool generation
1 (learned generation 22769). No observer draw was substituted. This time the
reserved first-refusal record identifies the exact callback:

- Operation: `CmdBeginRenderPass2KHR`.
- Phase: RecordingOutsidePass, sequence 3, before reason 0, after UnknownRoute 17.
- Decoder reason: UnknownExtension (4), chain SubpassBegin (1), header index 0.
- Root: `VkSubpassBeginInfo`, type 1000109005, inline contents.
- Its one observed pNext header type: **1000109099**.
- Ordinary RenderPassBegin chain: empty. No metadata truncation or contention.

The same header is observed on a later Begin. Twenty distinct recording
contexts cover 41 attempts with zero overflow. Subsequent original selected
pipeline/indexed-draw encounters remain visible as evidence after refusal, but
cannot authorize a replacement. First payloads, repeat counts and last sequence
are not a complete per-call timeline; do not invent unrecorded draw arguments,
all original layouts, unknown header payloads or the full ordering of repeats.

This establishes an actual missing recording-header route for this run, rather
than an imagined imageless Begin. It does not by itself classify the header's
payload or semantics. Do not simply whitelist its numeric tag, read an invented
structure, strip it from the original call, or weaken source/retirement/state
checks. Audit its public registry status and existing private native references
offline before deciding whether a faithful adapter is possible.

The independent source copy succeeds: P010 1152x4608, 15,925,248 bytes, SHA256
`c364cbb5e7ede79e2187663f091db356d8670eb22daa0eb27f40c497dc3c13f7`;
sampled components RGBA8, 21,233,664 bytes, SHA256
`cc6b2f2c4805a3724da4e339a6267dee5b937fa1de422ba72c7b6ae04a4568ec`;
snapshot metadata 4,129 bytes, SHA256
`49415b6900d92b55d300352d5443ce99b8322ce28f6838c9f20905c70f73e36b`.
All three are downloaded and checksum verified. The optional 128-byte
original-invocation SSBO file is absent. Raw stored Y and sampled component codes
are not reconstructed native RGB or source-frame/eye/UV proof.

The maintainer separately approved rollback. Activation and run library are
removed, no diagnostic mappings remain, original native startup is healthy and
the maintainer confirms the interface is visible/usable. Standby is safe; no
automatic expiry/restart occurred. The run plan is consumed and must not be
reactivated. Exact protected evidence, available-file transfer and completion
receipts remain in `build/private-native-ssbo-run-20261017-r1/`.

Next work is offline. It should also review whether multiple original selected
draws can be preserved safely across the complete command: an earlier UnknownRoute
may mask a later state-protection refusal. An extra descriptor set used by an
observer must never disturb valid later native descriptor state. A refused
publication after executing the earlier observer is not, on its own, a repair
for already disturbed state. No new Frame access is authorized by this note.
