# Inert original-invocation buffer observer

The next native binding needs values consumed by the original fragment, not
uniform bytes copied after its draw. The original
[offscreen MRT observer](NATIVE-FRAGMENT-SSA-PROBE-20261016.md) established how to
export those SSA values. This follow-up writes them into one owned128-byte SSBO
while retaining a single ordinary color attachment. It is host-only and not
installed in the native compositor.

The transformer accepts only the exact private original fragment profile. Its
original instruction stream and output store remain in place. A bounded fragment
coordinate guard and uint atomic compare/exchange allow one invocation to claim
one32-word record. The record contains version/magic/size, original packedu,
R1/R0/patch, raw flags, tint bits, both original sampled vectors and the same
invocation's original RGBA. No additional native UBO or sampler descriptor loads
are added. One original FragCoord input load is added, explicitly recorded.
Original module bytes and generated derivatives are not redistributed.

## Admission and ownership

The input admission schema describes caller-owned offscreen facts only. It
requires actually enabled `fragmentStoresAndAtomics`, an explicitly compatible
owned layout, bounded device limits and descriptor range exactly128 bytes.
Physical-device support by itself is insufficient. Vulkan forbids writable
fragment storage variables when that feature is not enabled; see
[VkPhysicalDeviceFeatures](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html).
The original pinned fragment declares Set0; the fixture owns Set1/Binding0.
That does not prove a native pipeline layout is compatible. Disabled admission
refuses the module entirely: a future normal OFF path uses the original shader.

Claim1 means CLAIMED, not READY. The host observes the record only after the
shader-to-host memory dependency, GPU fence and noncoherent-memory invalidation
where required. Fence completion alone is not a host memory visibility rule;
see [Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
Pipeline preparation and file access are confined to this offline fixture.
No compilation, waits or I/O are added to a native rendering thread.

## Actual focused evidence

Intel and llvmpipe each pass36 normal cases/8124 checks and36 unselected-fragment
cases/4848 checks with Khronos validation active and zero validation/sync errors.
Original RGBA is actually bit-identical in36/36 cases on both drivers; the
inherited inert comparison limit remains2e-6 rather than claiming universal
compiler bit identity. There is one active color attachment. Metadata/raw bits
match, including flags whose bit patterns would be NaN/subnormal if incorrectly
converted through float. A later claim cannot overwrite the completed record.

A real device created with fragment storage disabled refuses before creating
the instrumented pipeline. Valid R1 and raw-flag mutants fail their respective
record oracles. A logical-boundary mutant writes the following canary and is
detected: only that adversarial test uses a256-byte descriptor, keeping the
access Vulkan-legal while violating the independent128-byte record contract.
Normal admission still rejects range256. A missing-third-module control also
exercises exception teardown. The owned descriptor pool is destroyed only after
device work has stopped, before base resource teardown.

Four Python methods cover strict admission, record/coordinate bounds, preserved
instructions, added input versus native descriptor loads and validated exclusive
CLI output. Host GPU checks reuse the approved texture/resource helpers. They do
not stand in for a native capture. An independent source/evidence review finds
no blocker for this inert scope.

Private evidence at `build/private-native-ssbo-20261016/` retains commands, source
pins, complete validation messages, positive/negative results and cleanup receipts.
Temporary modules, executables and privately extracted validation-package caches
are removed after process reaping; original reference inputs are preserved.

## Reused command-buffer executions

The original first-claim buffer cannot be joined to the most recently copied
source image just because both share command/image generations or a fence. A
new owned GPU fixture records its target command buffer exactly once with flags0,
then submits that unchanged recording three times on one queue. A separate update
recording, also recorded once, changes both actual source-image pixels and UBO
contents between fence-completed executions.

An ordered128-byte GPU fill before the render pass resets the claim and body on
each execution. Intel and llvmpipe each pass three cohorts/1005 checks with
Khronos synchronization validation active and zero errors. Each cohort compares
the full4096-byte GPU source copy, the record and its instrumented RGBA output.
Without the fill, a legal control fails on the second execution: the unchanged
FIRST record accompanies a changed LAST source image and RGBA. The positive
starts with a nonzero claim, so even its first result depends on the GPU reset.

This proves serial replay of one owned recording, not native source stability,
multi-queue or simultaneous submission. It adds no new separate stock-output
comparison; that evidence remains the preceding fixture's independent result.
The receipt's `readback_memory_coherent` field describes the source-copy buffer
only. Noncoherent invalidation code is present but this run does not establish
its execution for all readbacks. Private source pins, original failures, results
and review remain in `build/private-native-cohort-reset-20261016/`.

## Distinct layouts and restoration

An additional owned GPU fixture uses two genuinely different pipeline layouts:
original Set0 only, versus an owned Set0 copy followed by the record's Set1.
The two Set0 handles are distinct but their complete supported definitions match;
all push ranges also match. Original Set0, push data, viewport and scissor are
bound once. Between original and restored draws, the probe binds only its Set1
and immediately restores the original pipeline after its draw. It never rebinds
Set0 or rewrites push/dynamic values. Set1 remains bound but unused afterward;
there is no general unbind operation being claimed.

Intel and llvmpipe each pass two asymmetric cases/222 checks. Original and
restored output are bit-identical. Probe-versus-stock error is zero in this run
(the independent inherited comparison bound remains2e-6); its record matches the
same invocation's probe output exactly. Both drivers have active Khronos/sync
validation and zero errors. Prior higher-set use and different push ranges are
refused before issuing probe commands. The first run's renderpass transition
hazard is retained; adding the owned renderpass's incoming transfer-read to
color-write dependency repairs that exact failed fixture. Private evidence is in
`build/private-native-prefix-restore-20261016/`. None of these are native draws.

## Cold definitions and owned construction

`pixel_ssbo_clone_contract.h` captures a bounded22,664-byte recipe while the
successful creation inputs are still readable. It owns exact pinned module
bytes, supported Set0 arrays, every original push range, one classical subpass
description, and nested supported graphics state. It retains native identities
only as scalar diagnostic provenance. Unknown extensions, immutable samplers and
unsupported profiles refuse. Ignored dynamic viewport/scissor and unused depth
state are not dereferenced. New views use only explicit caller-owned replacement
objects and must be rebuilt after copying or moving the recipe/view.

Focused definition controls pass142 public/171 private checks normally and under
ASan/UBSan, plus AArch64 syntax. The final all-unused-color early refusal has
three additional targeted controls; the integrated public CTest passes145.
Earlier checks are inherited honestly rather than reporting another full run.

The separate `pixel_ssbo_owned_clone` component constructs its Set0/Set1, layout,
renderpass, modules, pipeline, record buffer/memory/map and descriptor pool cold
through downstream PFNs. It borrows neither native object handles, pipeline cache
nor allocator callbacks. The record is128 bytes; its actual allocation is bounded
at8 MiB. Only ordinary host-visible memory properties are admitted. This first
component is tested through fake dispatch, not an actual native driver creation.

Each coherent/noncoherent failure stage is injected, including undefined failed
single-create/map outputs and the graphics-pipeline API's valid partial-handle
exception.2326 focused controls pass normally and under ASan/UBSan; a later small
test-only mapping-lifetime strengthening passes2338 via the integrated CTest.
There is no command recording, queue wait or implicit Vulkan destructor.
After a caller marks resources referenced, explicit release requires both all
recordings retired and device quiescence/loss; a completed reusable submit is
insufficient. This distinguishes the conditions in
[Vulkan object lifetime and runtime errors](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html)
from a CPU handle-generation check. Private construction-control evidence is in
`build/private-cold-clone-owner-20261016/`.

The definition and construction components are compiled/tested independently;
neither is linked into the native receiver target. Their complete/ready flags
grant no native compatibility, original lifetime, frame/eye or activation proof.

## Still required before a Frame package

Observe actual native enabled features and full layout compatibility; prepare a
compatible clone cold with original stage/state/lifetime rules; bind each record
to one admitted original draw, image/view/command generation and the matching
owned input acquisition/fence. A lifetime handle is not a decoded video epoch.
Retain normal menu recovery and manual rollback. No package, Frame agreement,
automatic restart, live mask or passthrough presenter follows from this fixture.
