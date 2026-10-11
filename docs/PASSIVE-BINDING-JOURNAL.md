# Ordered passive binding metadata

The 2026-10-11 [loss audit](PASSIVE-BINDING-LOSS-AUDIT.md) found five lost
binding observations and unsupported imported-resource metadata in the armed
Frame recording. This implementation addresses those two source-level causes.
The subsequent [native Frame run](FRAME-BINDING-JOURNAL-RUN-20261011.md) loaded
this implementation and retained the supported import/sampler fields. It also
exposed substantial journal queue overflow. Those two hardware recordings remain
incomplete. The subsequent [compact native run](FRAME-COMPACT-JOURNAL-RUN-20261011.md)
retains 703,494 observations without loss and all 32 selected CPU resource
snapshots. No replacement shader or mask is activated.

## Hook and writer responsibilities

Binding hooks copy supported API inputs into owned, fixed-size records. They
never enter the binding object's table mutex, retain borrowed API arrays, read
image/buffer contents, inspect an imported FD, or perform logging. Successful
native creation/binding results and destruction calls are forwarded unchanged.

There are 32 producer lanes, each with 2,048 small headers and a 256-KiB owned
byte ring. A scalar operation copies only its scalar payload; bounded arrays
copy their validated used prefix. Maximum-size calls still have a separate byte
bound. Each lane occupies less fixed memory than the former 128 full-size slots.
Typed payloads are restored on the background owner with their active C++17
union members initialized before copying. Thread registration has a
fixed scan; cold identity reservation has at most 64 changed-value CAS attempts
and never waits for an owner. Cached publication uses lock-free scalar atomics.
The AArch64 build statically verifies the relevant atomic types. Linux uses
24 bytes of initial-exec POD TLS, avoiding a first-hook `__tls_get_addr` call
and TLS initialization guard. Instance/thread identities saturate without reuse.
Loader TLS reservation failure rejects the optional library.

A single background writer merges global tickets and owns all binding-table
mutation and snapshot construction. An unpublished earlier ticket defers this
background work; native rendering continues. Multi-write/copy calls occupy one
record and retain writes-before-copies semantics. Supported call arrays are
limited to 32 entries; an oversized call is rejected as a whole. Unknown
extensions and unsupported descriptor arrays stay incomplete.

Snapshot requests use the same ordered stream. A small cookie joins a completed
writer-owned result to its original draw-time output serial; it is neither a
resource identity nor a video-frame key. Full snapshots are not copied through
render events. Events are 848 bytes, with 256 queue slots and the unchanged
2,048-event lifetime limit. The writer retains at most 32 snapshot results.

Registry/queue exhaustion, oversized batches, ticket wrap, interrupted closure
and missing predecessors remain sticky losses. No counter is cleared at arming.
Any lost binding observation prevents a complete binding claim, including a
claim that binding 31 is free. A closed gap drains remaining requests as
incomplete. The footer distinguishes journal transport losses from table
capacity, unsupported paths and pipeline/event loss.

Writer closure first quiesces existing event admissions, then closes and drains
the journal before emitting its stable footer. Rendering threads never wait for
the writer. The existing native-context dispatch mutex and pipeline/event
try-lock path remain; this is not a claim that the entire layer is lock-free.
Binding-table lookups now use fixed-capacity indexes and stable object slots.
Destruction removes the index entry; device/handle generations and dependency
invalidation remain intact. The writer reuses a lane only for the exact expected
ticket, and caches its acquired publication boundary. A closed, quiescent journal
rescans published heads before declaring a missing predecessor. Productive
snapshot replay does not add an idle delay after each 256-record batch.

The final compact journal uses 13,827,056 bytes in the x86-64 validation build,
including its indexed object tables; the original unindexed checkpoint used
13,956,232 bytes. Compile-time checks enforce the per-lane memory bound on the
AArch64 build too. This is diagnostic memory, not per-frame allocation. The
file still stops at 1 MiB, 32 selected binding snapshots, the event limit or the
existing 120-second filtered/30-second unfiltered deadline. Recording completion
does not restart SteamVR in the maintainer's manual-rollback mode.

## Imported resource metadata

Bounded pointer-free parsers retain up to eight chain nodes, four DRM memory
planes and eight modifier candidates. Supported fields are:

- Image external handle types, explicit DRM modifier and memory-plane layout.
- Allocation DMA-BUF input FD integer/handle type, export flags and dedicated
  image/buffer inputs and resolved generations. The FD is never used after import.
- Custom sampler border components as raw bits and their format.
- View/sampler YCbCr conversion attachments and conversion creation/destruction,
  model/range, swizzle, chroma offsets/filter and reconstruction flag.

Unknown, duplicate, cyclic or oversized chains cannot certify completeness.
A DRM modifier candidate list is retained but does not certify the driver's
selected modifier. Conversion/resource destruction or handle reuse invalidates
dependent records. Retained scalar observations are not decoded pixels or
cross-process resource identities.

## Initial local validation and review

The initial merged gate passed 14 of 15 suites. The real-loader GPU test exposed
a 64-slot journal overflow during selected draw bursts; a rerun also exposed
output-ring pressure. Neither loss was hidden. The final bounded repair doubles
the journal lane to 128, keeps only small references in render events and uses
256 small event slots. Pending replay work is processed before an idle delay.
Native fixture traffic was not throttled to make the test pass.

After that limited repair, only the three affected suites were rerun:
`passive_binding_journal`, `passive_pipeline_trace` and
`passive_pipeline_trace_gpu`; all passed. The twelve unchanged suites inherit
their initial merged-gate result. This is complete combined evidence, not a
claim that a second full gate was executed.

The final journal has 840 passing checks, including an unthrottled burst of all
32 selected snapshots with surrounding updates, 16 concurrent producers,
borrowed-input lifetime, delayed predecessor publication, handle reuse,
secondary/reset dependencies, queue/registry saturation, closure and wrap.
Its final ASan/UBSan run also checks stack-use-after-return and leaks. The earlier
741-check algorithm checkpoint passed TSan with `-no-pie`; the default PIE
runtime rejected its memory mapping before fixture execution. That TSan evidence
is inherited for the unchanged publication/replay algorithm, not a fresh run of
the final capacity/burst fixture.

The extension parser passes 46 strict and ASan/UBSan checks. Existing binding
state tests retain ten passing groups. The real Vulkan loader exercises 17
modes, including eight concurrent native resource producers, exact deployment
manifest discovery, pre-arm traffic, delayed arming, snapshot limits and
unchanged fixture GPU pixels. Five additional repetitions of that affected GPU
suite passed after the scheduling-sensitive overflow, without ignoring losses.
Strict AArch64 compilation passes, its ELF TLS reservation is 24 bytes and its
dynamic symbols have no `__tls_get_addr` import. API-metadata fixtures do not
establish correct headset output.

Independent review additionally repaired inactive union-member lifetime,
ignored descriptor-member pointers, identifier reuse at wrap and lost original
dedicated-resource input handles. No reviewer accessed or changed the Frame.
Private validation/artifact hashes are retained under
`build/journal-validation-20261011/` and `build/journal-trace-staged/`.
Completed test builds and worker checkouts are removed after integration.

The integrated checkpoint `a822f2c` subsequently passed the
[complete 15-suite CI run](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38150625034),
the Windows diagnostic-sender cross-build and protocol sanitizer. This is a
separate complete CI result; the local limited-repair scope above is unchanged.

## Remaining acceptance gates

The earlier authorized native run recorded all 32 selected snapshots with locally
complete supported import/sampler metadata and zero binding-lock contention.
It also recorded 201,483 journal queue losses and 9,222 unknown-state losses;
none of its snapshots is globally complete. Queue capacity is therefore
insufficient for that observed producer/replay balance. The subsequent
[throughput repair](PASSIVE-BINDING-THROUGHPUT.md) measures background lookup,
replay and burst limits, then indexes tables and compacts owned storage within
the previous memory budget. Its six scheduled burst cases retain every snapshot,
but extreme asynchronous startup still overflows. These software cases alone
are not native Frame evidence; pre-arm losses remain sticky and cannot be
cleared to accept a recording.
The footer now separates header and byte pressure, attributes publication/replay
losses to API operations, and reports background replay, serialization, output,
wall and thread CPU time. Clocks and serialization run only on the background
writer; render hooks do not sample clocks or write logs.
The [fresh compact native run](FRAME-COMPACT-JOURNAL-RUN-20261011.md) subsequently
retains all 703,494 binding observations, zero per-operation losses and all 32
source-complete snapshots. That closes the observed CPU resource-metadata loss
gate; its explicit byte cap still omits two trailing output events.
The frame/eye/source association, GPU-consumed uniform coherence, selected
decoded resource generation and submit-time binding validation remain open.
`gpu_consumed_coherent` and `video_frame_associated` stay false.

The temporary session and exact rollback procedure remain those in
[FRAME-TRACE-SESSION.md](FRAME-TRACE-SESSION.md). Restart only after the maintainer
confirms readiness; recording completion causes no automatic restart. These
changes alone are not working Steam Link passthrough.
