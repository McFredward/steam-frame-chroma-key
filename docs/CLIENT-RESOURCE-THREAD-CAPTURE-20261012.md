# Actual decoder-thread preparation and peer snapshot

## Hardware result

The maintainer authorized the separately reviewed cold decoder-thread scope
with "Alles klar, leg los" and confirmed normal Frame operation and a visible
GloomhavenVR PC-VR stream after the coordinated Steam restart. Source commit
`8aa8535` and the exact eight inputs, **555,372 bytes**, match
[the reviewed preparation](CLIENT-RESOURCE-THREAD-PREPARATION.md).

The final stable private capture is **40,076 bytes**, SHA-256
`99ca939a803f647e228a19810ddaaade2bb8d6ceff7ca5266aec111665cac592`.
It contains a header, **21 ordered records**, and a deadline footer. The
source-specific first native event wait was intercepted and its actual calling
thread's usable stack was prepared successfully before forwarding the wait.
All six subsequent resource-boundary records report
`resource_thread.stack_owned_current=true`, reason zero, and entries within
that independently obtained interval.

| Observed boundary | Original native result | Current cold stack owned |
| --- | ---: | --- |
| Import begin, Before/After | Lock returned 0 | Both records |
| Import pending, Before/After | Unlock returned 0 | Both records |
| Following reference, Before/After | Lock returned 0 | Both records |

The older `startup_thread_stack_owned` field remains **false** throughout.
Preparation of the decoder thread does not retroactively make it the startup
thread. Original Before/After entries, the native pipe, output/frame arithmetic,
manager, and reference-token argument match the preceding source review.

The diagnostic read **zero private resource transaction, wire, pixel or shader
bytes**. Recording completed within the existing 32-record, 64-KiB and
120-second bounds; 21 queued records were drained without a queue-loss record.
Native rendering and the PC Steam sender were unchanged.

## Later ownership refusal

After the six successful resource observations, a later call at the same native
wait site produced `ResourceThreadRefused`, reason **9 (`WrongThread`)**. Its
original SP, decoder register and return address equal the prepared entry's
values, but those addresses do not grant thread-lifetime ownership. The gate
refused further use and the footer retains root refusal **24 (`ResourceThread`)**
alongside the historical successful-preparation and sampler-completion fields.

This was a safe diagnostic cutoff, not a reported native rendering failure.
It must not be described as an uninterrupted, process-wide decoder capability.
The existing reason groups current marker address, fresh marker bit, current
stack-owner anchor, and the gate remaining prepared during the check. The
capture does not say which condition failed and records no operating-system
thread ID or exit history. Thread replacement with reused addresses is a
possible explanation, not a proven hardware event. No guard was relaxed, slot
rearmed, guessed stack used, or live pointer read after refusal.

The useful positive result is narrower: the first captured import/reference
timeline had actual current-thread stack ownership at every observed boundary.
Any later transaction capture must independently retain that ownership at each
copy and stop on loss. Repeated decoder lifetimes need their own source-backed
admission, rather than recycling this one-shot registration.

## Actual kernel peer relation

The corrected external helper performed exactly **four kernel metadata queries**
and scanned **405 descriptors** in the one authenticated compositor process,
below the explicitly authorized 4,096-entry bound. Current UID, network
namespace, PID/start identities, FD inodes and both socket-generation cookies
were validated before/after the lookup. The symmetric kernel peer relation
between the current receiver endpoint and compositor endpoint was observed.

This is a contemporaneous snapshot taken after the resource observations.
It does not authenticate the historically captured FD continuously, supply a
native Registry lease, or associate a resource token with either endpoint's
video frame. Socket continuity, cross-process resource namespace, source frame,
eye, UV, accepted submit and coherent GPU consumption remain **unproven**.
The completed run's PID/FD numbers are not reusable leases.

## Recovery and evidence review

After the separate "Ja, jetzt zurücknehmen" confirmation, the helper removed
the diagnostic override and restarted original Steam. All **57 readable
user-owned process maps** were free of the diagnostic; no unreadable process
belonged to the affected Steam service. The original native hashes,
environment file, service environment and manager trace keys matched the fresh
pre-install baseline. Only the exact archived session inventory and eight own
uploaded inputs were removed. Final checks found Steam active and no remaining
diagnostic paths. SSH was closed, and standby readiness was confirmed.

A root review passed **171 metadata/source-relation/recovery checks** against
the real capture and peer receipt. This is evidence review, not a new complete
software test gate. Passing source, final ARM artifact and CI evidence from
the preparation remains inherited. Compact hardware, source, inventory and
rollback receipts remain private under ignored
`build/private-resource-thread-frame-run-20261012/`; the regenerable upload
wrapper was removed. No Valve binaries or private capture records are published.

## Next boundary

The calling-thread prerequisite for the first sparse transaction observation
is now hardware-tested. The next separately scoped adapter can copy the exact
required scalar slots and successful request/reply metadata bytes described in
[the resource integration plan](CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md).
It must use the existing decoder, original native read/write providers and
current-thread capability, without admitting the strict carrier prematurely.

This step still does not enable genuine-mask streamed passthrough. Mask shader
activation stays disabled until native resource, source-video/eye and UV/GPU
association are established. No additional native PC Steam sender modification
is part of this route.
