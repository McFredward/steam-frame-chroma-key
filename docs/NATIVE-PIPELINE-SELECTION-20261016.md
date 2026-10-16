# Exact native pipeline selection after arming (2026-10-16)

The retained native API archive records six `PS_DISTORT` pipeline creations,
six different layout handles and twelve different VS/PS module handles. Its
294 actual `draw_recorded` events use only the final two pipelines:147 draws
each, in two different command buffers. The archive does not establish those
objects' simultaneous lifetimes. Treat all six variants as potentially live;
creation order cannot identify the native target used for a later capture.

The previous bridge combined two unrelated choices: the first cold ready recipe
and the first later source candidate command buffer. It could prepare a never
used pipeline, or learn the command buffer of the opposite active variant. The
old four-pipeline inventory and four-receipt allowance also lost later variants.
These were deterministic source integration defects, independent of visual
verification on a headset.

## Current selection

The definition journal now has16 slots each for Set0, layouts, render passes,
original shaders and pipeline recipes. Its measured host size is558,576 bytes.
The existing four-receipt output budget remains unchanged and does not consume
selection availability. `lookupPipeline` copies the exact live
`{native device, pipeline value, journal generation}` requested by the selected
original draw, even when that recipe was already reported. It never substitutes
another ready recipe for a stale, unknown or reused identity.

Before arming, the original shader callback may prepare the pinned canonical
Sparse32 **CPU artifact** and promote earlier owned count1 Set0/layout facts.
It does not choose a pipeline or construct a driver clone. Device eligibility,
actual queue identity and canonical profile readiness are distinct from clone
readiness: requiring a clone before allowing the arm action would form a cycle.
The operator requests the existing manual arm only after the maintainer confirms
the visible streamed test pattern.

After that arm, the actual source candidate supplies the journal identity of
its selected original draw. A fixed scalar mailbox transfers that identity and
its command/pool generations to the cold worker. The rendering callback performs
no journal lookup, allocation, shader compilation or driver-object construction.
The cold worker selects and prepares only that exact retained recipe, with the
existing currentness check and cancellation epoch. It may wait for a legitimate
later original queue retrieval without discarding the selected metadata.

Only the selected command buffer's next successful original Begin can then adopt
the ready clone. The recording ticket must name the exact selected pool lifetime
and a later command generation. A different valid command buffer does not adopt
it, even when its own active pipeline has identical shader bytes. Selected
command free or pool destruction cancels pending selection. A successful pool
reset preserves the pool object identity and the next opaque recording ticket
still observes the actual current command generation.

All previous original-output, source/execution pairing, retirement, bounded
record validation and explicit idle/device-destruction requirements remain.
Selection metadata is not a native lifetime lease or frame/eye association.

## Focused evidence and remaining unknowns

The private callback fixture supplies the exact pinned original VS/PS to both
production observers. It retains six complete variants with distinct layouts,
Set0 handles, render passes and twelve shader-module handles, using the recorded
29-binding Set0 shape. Two actual command-buffer candidates exercise the fifth
and sixth variants independently of creation order and the first four reporting
receipts. A destroyed/recreated selected pipeline refuses instead of falling back
to the first recipe. These are finite fake-dispatch callback controls, not native
GPU execution or visual proof. Existing zero-count draw, replay, source pairing,
retirement and teardown controls remain in the affected suite.

The recorded Set0 shape fits the existing32-binding and16-element bounds: UBOs
0..7, storage buffer8, sampled images9..24 and samplers25..28, each count1 with
stage ALL. Complete current native graphics/render-pass creation inputs remain
unknown until observed; the passive archive cannot prove their admission or
current lifetimes. No Frame access, deployment or sender modification is part of
this change.
