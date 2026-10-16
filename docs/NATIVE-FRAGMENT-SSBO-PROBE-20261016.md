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

## Still required before a Frame package

Observe actual native enabled features and full layout compatibility; prepare a
compatible clone cold with original stage/state/lifetime rules; bind each record
to one admitted original draw, image/view/command generation and the matching
owned input acquisition/fence. A lifetime handle is not a decoded video epoch.
Retain normal menu recovery and manual rollback. No package, Frame agreement,
automatic restart, live mask or passthrough presenter follows from this fixture.
