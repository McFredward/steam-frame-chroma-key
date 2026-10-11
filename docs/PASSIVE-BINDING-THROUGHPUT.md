# Passive binding throughput repair, 2026-10-11

The [native journal run](FRAME-BINDING-JOURNAL-RUN-20261011.md) lost 201,483
binding publications despite zero binding-lock contention and no exhausted object
tables. This repair addresses measured replay and burst limits. It is not a new
Frame result, a video-frame association, or working streaming passthrough.

## What the measurements distinguish

The throughput fixture performs synthetic supported Vulkan API observations;
it creates no Vulkan device and accesses no headset or native image. It retains
the same calls, object population, ordering and snapshot assertions across
baseline and repaired implementations. Timing measurements are observations,
not CI pass thresholds. Semantic checks require genuine losslessness in a finite
concurrent workload and verify rejection after deliberate saturation.

The original journal reserves approximately 2.5 KiB for every observation, even
a single scalar. Its 128-record lanes overflow during dense bursts despite a low
average rate. Faster replay helps but cannot make a lane hold the same burst.
Shortening idle polling alone does not solve this. The compact journal separates
2,048 32-byte headers from a 256-KiB byte ring per lane, retaining only each
validated payload prefix. Independent header/byte admission happens before
ticket reservation, and failure remains sticky. Fixed journal storage falls from
13,956,232 to 13,827,056 bytes in the measured x86-64 builds.

The background object tables now have bounded, allocation-free indexes. The
writer preserves exact global tickets while caching acquired lane boundaries.
It continues productive snapshot replay instead of sleeping after each batch.
None of these changes introduces waits, clock sampling or logging in binding
hooks. Native arguments and return values are forwarded unchanged.

## Results and limits

Final measurements use root checkpoint `97da821`, journal SHA-256
`bcd6ddd640ddada31205dd2b8cad9c2662a2f099960302cab04e62298ad72580`,
and fixture SHA-256
`5b258bb7667284937b346b09958b60b7196b2b55b7181a87c04ece757c36d3dc`.
Exact raw outputs, compiler flags and source hashes are retained privately in
`build/replay-repair-validation-20261011/throughput/`. The baseline is the
unindexed full-record checkpoint; the later indexed/preferred-lane checkpoint
is recorded separately rather than presented as the baseline.

| Synthetic measurement | Original checkpoint | Final compact implementation |
| --- | --- | --- |
| Direct Core, 384 objects | 0.413 µs/observation | 0.061 µs/observation |
| Direct Core, 32 objects | 0.071 µs/observation | 0.056 µs/observation |
| About 128 calls/burst, 90 Hz | 0.25% queue loss; no complete snapshots | Zero loss; 33/33 complete snapshots |
| About 256 calls/burst, 90 Hz | 45–49% queue loss | Zero loss; 33/33 complete snapshots |
| About 512 calls/burst, 90 Hz | 66–74% queue loss | Zero loss; 33/33 complete snapshots |
| Unpaced 5,762-call asynchronous startup | About 93% queue loss | 28–29% queue loss; incomplete |

The scheduled comparisons use the same independent 90-Hz schedule at 1,000 and
100 µs idle polling. Each burst remains unpaced internally; queue health never
changes the producer schedule. Ordinary hot-state comparisons build and drain
their warm population outside the measured interval; the separate asynchronous
startup case deliberately does not. Do not mistake warm-state results for a
proof that startup is lossless.

The final largest scheduled case reached 536 headers and 47,232 payload bytes in
one lane. The unpaced startup fills both independent limits and reports those
losses honestly. Compact decoding also adds work: an empty-update microbenchmark
has approximately 72 ns background drain cost, versus 44 ns at the preceding
preferred-lane checkpoint. This is a burst-capacity repair and an indexed lookup
improvement, not a claim that every individual operation is faster.

The expanded fixture also exposed a closure race: a head scan could precede the
last publication, then observe quiescent closure and incorrectly reject an
already-published ticket. A second authoritative head scan after acquiring
quiescence prevents that false gap without waiting on rendering threads.

## Validation scope

The complete local 17-suite gate passed after initial indexing and writer
changes. After the bounded compact/closure/footer repair, only its seven affected
suites were rerun: binding trace, lookup, journal, extensions, throughput,
pipeline trace, and real-loader GPU trace. All seven pass. The ten unchanged
suites retain the earlier gate's evidence; this is not a second complete local
gate. The real-loader fixture retains its 17 modes and unchanged GPU pixels.

The compact journal has 2,631 passing assertions in strict, ASan/UBSan and TSan
builds. TSan initially rejected its runtime memory mapping; one bounded retry
executed all assertions without a reported race. Tests cover typed union-member
restoration, wrapped prefixes and mixed write/copy calls, borrowed input lifetime,
separate header/byte saturation, malformed headers/counts, cursor/ticket wrap,
concurrent publication, ordering, generation reuse and closure. Independent
read-only reviews checked the final source without repeating the same suites.

The source trace builds strictly for AArch64. Its library is 230,272 bytes,
SHA-256
`9100902dfa240427523099062ccf4f77fd647d48979b37eb966d391943dc175d`;
ELF TLS remains 24 bytes, with no `__tls_get_addr` import. The library remains
below the existing 256-KiB deployment guard. Private native evidence and small
validation records are retained; completed worker binaries and checkouts are
removed after integration.

## Next native check

No diagnostic is currently installed on the Frame. Activation still uses the
approved [temporary session](FRAME-TRACE-SESSION.md), one announced restart,
explicit PC-VR-stream readiness, and manual rollback without a timed restart.
The capture remains capped at 1 MiB, 32 selected snapshots, 2,048 output events
and the existing deadline. It reads no image or mapped uniform content and
activates no replacement shader or mask.

The new footer records per-operation publication/replay loss, independent header
and byte exhaustion, high-water occupancy, replay/serialization/output duration,
and background wall/thread CPU duration. Thread CPU sampling occurs only twice
in the writer measurement window; clock failure is explicitly marked and yields
no CPU estimate. These counters can distinguish sustained replay cost from
short bursts, output stalls and CPU scheduling when real workload evidence
arrives. They do not identify video frames or certify GPU-consumed uniform data.

A native result must retain all required lifecycle observations and the selected
snapshots without historical loss before proceeding to descriptor/frame/eye
association. An incomplete result remains opaque and does not authorize masking.
