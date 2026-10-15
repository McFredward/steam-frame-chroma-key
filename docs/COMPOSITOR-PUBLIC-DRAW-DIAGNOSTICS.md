# Independent public draw diagnosis

This is inactive diagnostic infrastructure, not Steam Link passthrough. The
2026-10-15 periodic capture showed why the first selected import was an
insufficient filter: its image and view had left current tracking before nearly
all distortion probes. Continuing to require that resource for every public
descriptor observation hid the actual drawing inputs.

## Two explicit kinds of evidence

`ExactTarget` preserves every existing tag, cookie, device, generation, binding
and currentness gate. A different view of the same image is not a match.

`UnassociatedPublicDraw` is an opt-in diagnostic of the current public command
and layout. It is permitted only after the original import publication and
matching retained tag/cookie/device, with restored, ordered, loss-free replay.
The collector selects the observed fragment entry `PS_DISTORT`; that name is a
sampling filter, not proof of video, physical eye, source frame or GPU use.
Retirement of the original target may leave this separate observation useful.
Its target result remains `Unknown` or `Filtered`, not `ExactMatch`.

Every serialized full snapshot carries `snapshot_provenance`, `target_matched`
and `source_frame_associated=false`. Public diagnostic completeness refers to
the copied API metadata only. Neither kind establishes GPU execution, frame
contents, cross-process namespace or native reprojection/foveation mappings.
Missing or invalid mask associations must continue to render opaquely.

The public lane has a 16-snapshot sublimit. Both lanes share the existing total
32 full snapshots. A public request cannot bypass an exact-target quota refusal
or count an omitted snapshot as emitted evidence. The 4096 probe ceiling,
60-ms generic/distortion permits and 32 classification credits per period stay
unchanged. Graphics callbacks gain no private reads, clocks, waits, allocation,
file I/O or new launch getter.

## Lifecycle and incomplete descriptor observations

Three fixed records identify replayed public image destruction, view destruction
or memory release for the exact original generation. Device destruction can
retire still-observed original generations too. These are ordered API-call
observations, not GPU completion or continuous resource leases. Recycled numeric
handles do not retire an earlier generation a second time.

Incomplete descriptors now retain the current candidate view/image identities,
write/current completeness flags, conversion identity and bounded creation-chain
summaries before returning the original refusal. These diagnostics explain
which metadata is missing without accepting incomplete data. Descriptor output
remains bounded to the existing set-0 binding subset and fixed chain capacities.

## Authenticated offline contract

The new plan schema is `frame-compositor-raw-offline-bundle-v8`. Its hidden,
read-only ELF marker is `FCMSMP04`, 64 bytes: version 4, size 64, draw 32, execute
16, submit 16, full snapshots 32, probes 4096, flags 127, period 60000000 ns,
two probe permits, 16 denial samples, diagnostic version 2, 32 classification
credits and 16 independent public snapshots.

The observer report and live protected journal header must agree exactly on
typed scope. The `FCMRAW05` acquisition object, envelope version 5, one-candidate
757-byte private-read ceiling, 120-second absolute window, 64-KiB raw output and
1-MiB journal remain unchanged. Older v4–v7 plans retain their original checks
and recovery; their ELF objects cannot be relabelled as this collector.

Owned deployment still requires fresh operator authorization and a coordinated
SteamVR restart. Recording expiry does not restart the service. Original-state
rollback precedes diagnostic evidence checks; native files are preserved.

## Next experiment

This lane removes an observation blind spot. It does not solve transport.
Before another hardware run, specify which new fact it can establish beyond the
already captured complete YUV bindings. The preferred next feasibility test
uses app-owned color, coverage and identity regions in one image, as described
in [the sender-free route](PC-WITHOUT-SENDER-MODIFICATION.md). Both carriage and
the native spatial/composition mapping remain required, even if a visible
identity header survives the codec. No captured handle or timestamp substitutes
for that proof.
