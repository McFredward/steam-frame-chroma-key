# Passive binding loss audit

## Evidence and conclusion

This audit covers source `8571cc6` and the private 2026-10-11 armed capture in
`build/private-frame-armed-resource-run-20261011/`. It changes no rendering and
performs no Frame operation. The capture-control repair worked: one writer-arm
event precedes all 32 resource snapshots. Resource proof still failed for two
independent reasons: dropped bookkeeping and unsupported resource metadata.

| Observation | Proven conclusion |
| --- | --- |
| 32 resource snapshots; zero complete | None can authorize a descriptor or mask change. |
| Event drops 0; pipeline-state losses 0 | These are separate counters; neither establishes lossless binding tracking. |
| Binding losses 6: contention 5, unknown 1; all capacity counters 0 | Increasing table capacities does not address the observed failure. |
| All 32 snapshots have populated command/set generations and final reason 2 | Their snapshot reads acquired the mutex. Historical loss subsequently overrides their reason with `Contention`. |
| Bindings 0/2/3/4/6 have complete scalar metadata in all 32 | Useful observations survive, but global completeness remains rejected. |
| Binding 9 incomplete in all 32; 30 include a 1152×4608×1 image, two lack image metadata | The sampled-image chain is not certified. The known images use format `1000156013` and DRM-modifier tiling `1000158000`. |
| Binding 25 incomplete in all 32; custom-float border enum `1000287003` | Sampler metadata needs extension support. Its exact chain was not captured. |
| Target 31 reports absent, but `layout_has_binding31` is unknown | A populated target entry is not a certified free descriptor slot after observation loss. |
| File cap reached at 1,046,498 bytes; I/O successful | Recording was bounded. No complete resource/UV/frame association follows from successful output. |

The image format is `VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16`
(P010-shaped). Extent alone does not identify an eye, source packing or frame.

## Why the current design fails

`frame_bindings::Core` uses one `std::mutex` for all devices and object tables.
Every observation makes one try-lock attempt. A concurrent observation is
discarded immediately; `lose()` permanently increments the global loss counter.
The lock encloses linear handle lookup and potentially several table scans,
descriptor updates or pool/device invalidations. Independent Vulkan work thus
competes for the same observer lock even when native object synchronization is
valid. Reducing scan time could reduce collision probability; this capture does
not measure critical-section duration or attribute a headset hitch to it.

The integrated path invokes `snapshot()` only for the recorded 32 requests and
does not call `validate()`. All requests have populated identities, so the five
contention losses occurred during mutation bookkeeping, rather than these
snapshot reads. The footer does not identify the lost API/object. The unknown
loss might be a later consequence of a missed lifecycle, or a separate unsupported
ordering/path; neither explanation is proven. Add bounded API/object-kind loss
counters before assigning that cause.

`snapshotLocked()` rejects every future snapshot when *any* global loss exists,
including unrelated objects/devices, and overwrites an earlier local resource
failure with `lastLoss_`. This conservatism prevents false certification. Removing
it, resetting the counters at arming, or ignoring the five collisions would erase
potentially missed destroy/reset/reuse operations. Such changes are unsafe.

There is also a separate completeness gap. Resource creation currently rejects
non-null `pNext` without incrementing `Reason::Unsupported`: it retains identities
and basic fields but marks that object incomplete. Therefore footer
`unsupported=0` does **not** establish extension support. Removing contention
alone cannot make bindings 9 and 25 complete. The raw capture does not establish
which chain nodes were present; the static
[resource-path analysis](FRAME-RESOURCE-ASSOCIATION-20261011.md) supplies candidate
paths, not runtime chain proof.

## Recommended bounded redesign

Move binding-table mutation and snapshot construction to one background owner.
Rendering hooks should only copy bounded API scalars into an owned record and
publish it. Native calls and their results remain unchanged. A separate fixed
metadata queue must not reuse the output ring's producer mutex or lifetime event
budget: lifecycle tracking must continue before the 32 target snapshots are armed.

A tractable implementation uses fixed per-producer SPSC lanes, a fixed producer
registry and a lock-free global ordering ticket. Registration and publication
must have bounded attempts, with no allocation, waiting, spinning or file I/O in
a hook. Statically verify the relevant atomics are lock-free on AArch64. Copy all
supported arrays and chain fields while caller storage is valid; never retain
API pointers for later dereference. Publish multi-record API batches atomically
or mark the entire batch incomplete, preserving writes-before-copies semantics.

The consumer merges tickets in order and exclusively owns fixed object tables.
An unpublished earlier ticket is not permission to process a snapshot past it:
defer that background work without delaying the driver. At closure, unresolved
tickets make affected proof incomplete. Queue overflow, registration exhaustion,
overlarge batches and missed lifecycle remain explicit sticky losses. Do not
silently evict records, skip a ticket or recover by selecting the latest state.
Use fixed indexed tables to reduce replay cost; bound lookup probes and report
exhaustion rather than performing unbounded searches.

Snapshot requests enter the same metadata stream at the draw observation point.
The background owner constructs each snapshot after its preceding mutations,
even if later updates arrived before serialization. Submission observations
must reference the recorded identities/revisions; recheck their dependency chain
at the appropriate submission boundary. Successful native submission remains
distinct from descriptor validation and GPU completion. Current accepted-submit
events do not validate binding snapshots.

Initially retain device-wide loss invalidation. Narrowing it later requires a
dependency-aware journal that records which object generation and operation were
missed. Unknown lifecycle or handle-reuse gaps must invalidate every possibly
affected dependent record. Read-only snapshot failure may be classified separately
from lost mutation, but that small change alone does not solve this live run.
Per-device locks/shards or shorter critical sections are useful interim work,
not a guarantee that another hardware attempt will produce complete proof.

## Native extension scalars to capture

Support the actual observed chain types explicitly; unknown/duplicate/overlong
chains remain incomplete. Bound node and plane counts. The candidate support set
is:

- Image creation: external handle-type flags, DRM modifier and explicit memory
  plane offset/row/array/depth pitches. Memory planes are not source-eye regions.
  [DRM layout contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageDrmFormatModifierExplicitCreateInfoEXT.html)
- Allocation: DMA-BUF import/export handle type and dedicated image/buffer
  identity. Copy the input FD integer as a scalar only; successful import
  transfers ownership, and a reused integer is not payload or frame identity.
  No `fstat`, duplication or deferred FD inspection on this path.
  [FD ownership contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryFdInfoKHR.html)
- Sampler: custom border components as raw bits and the associated format.
  [Custom border contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCustomBorderColorCreateInfoEXT.html)
- Views/samplers: YCbCr conversion identity plus generation, with matching
  conversion create/destroy metadata when encountered. Preserve conversion
  model/range, component mapping, chroma offsets/filter and reconstruction flag.
  [Conversion attachment contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerYcbcrConversionInfo.html)

These fields still do not reveal uniform contents, coherent GPU-consumed eye
selectors/rectangles, the receiver's selected decoded content generation, source
session/frame, or its association with PC coverage. The exact native resource
bridge and UV/lifetime acceptance gates remain mandatory; opaque rendering and
the inactive replacement shader remain the correct behavior.

## Acceptance tests before another diagnostic deployment

1. Concurrent producer fixture with independent devices and resources; native
   API arguments/results unchanged and complete ordered replay without contention
   loss under the chosen tested capacity. Stress throughput and bursts separately.
2. Delayed-publication ticket, reversed producer arrival and interleaved
   create/update/bind/draw/destroy/reuse: never certify across a missing predecessor.
3. Queue/producer/table saturation, interrupted batches, closure with an in-flight
   producer and serial wrap: bounded memory/time and explicit incomplete evidence.
4. Reused handles, pool/reset cascades, mutable descriptors and secondary command
   execution: retained dependency/revision invalidation. No old snapshot revival.
5. Fixtures matching imported DRM images, dedicated allocations, custom borders
   and YCbCr attachments, including unknown/cyclic/oversized chains and alias
   entry points. Runtime scalars must match input; failures stay incomplete.
6. Preserve existing pre-arm traffic/budget tests, byte/time limits and source-only
   flags. GPU pixel forwarding stays identical; no new render-hook I/O or allocation.

This document was checked against the private capture summary/validation record
and current source. It introduces no implementation or new automated-test pass.
