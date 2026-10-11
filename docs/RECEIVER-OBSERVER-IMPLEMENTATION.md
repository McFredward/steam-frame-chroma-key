# Opt-in receiver selected-attempt observer

`receiver/receiver_observer.cpp` implements the narrow observation proposed in
[the receiver boundary audit](FRAME-RECEIVER-OBSERVER.md). It is an **offline
implementation**, not installed on the Frame, and does not enable passthrough.
The earlier runtime approval covered public Vulkan metadata only. Loading this
observer or reading the native selected records requires the additional reviewed
permission and reversible deployment plan described in
[the next adapter proposal](FRAME-NEXT-ADAPTER.md).

## Build and activation boundary

The Linux shared-library sources are `receiver/receiver_observer.cpp` and
`receiver/receiver_observer_core.cpp`. Compile as C++17, PIC, with hidden symbol
visibility, initial-exec TLS, `-fno-exceptions` and `-fno-rtti`; link pthreads and
`dl`. This avoids an unnecessary C++ runtime preload dependency. The integration build
adds an opt-in `FRAME_MASK_BUILD_RECEIVER_OBSERVER` target, disabled by default.
Only `pthread_mutex_lock` and `pthread_mutex_unlock` are exported.

Production accepts only the exact AArch64 `vrlink` executable SHA-256
`0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b`.
The basename is an inexpensive preliminary filter, not its identity. The hash,
ELF architecture and executable PT_LOAD bounds are checked once before claiming
any output file. Unrelated inherited preload processes and unsupported executables
pass through and produce no file, so they cannot consume the real receiver's
exclusive capture path. A deployment preflight must diagnose unsupported builds.

The two required environment values are:

- `FRAME_RECEIVER_TRACE_OUTPUT`: absolute path to a **new**, private JSONL file;
- `FRAME_RECEIVER_TRACE_ARM`: absolute path to an empty regular arm marker in
  the same private directory, created only when the VR stream is ready.

The recorder walks directory components with `openat`/`O_NOFOLLOW`, rejects dot
components, requires a user-owned private leaf directory, and creates output
exclusively with mode 0600. Existing output, symlinks and relative paths fail
closed. An arm marker must be empty, user-owned and private; a symlink never arms
the capture. These paths do not authorize a different native executable.

This library does not restart SteamVR, install itself, delete capture files, or
schedule rollback. Exact deployment paths, preload removal and the subsequent
native restart are the session tooling's responsibility. It must verify that the
native libc provider exports the required aliases before activation. An absent
versioned alias is a loader failure, not something a constructor can repair.

## Forwarding and capture contract

Public lock/unlock interposition forwards directly to distinct, versioned libc
aliases `__pthread_mutex_lock@GLIBC_2.17` and
`__pthread_mutex_unlock@GLIBC_2.17` on AArch64. The host fixture uses their
`GLIBC_2.2.5` aliases. There is no lazy resolver or `dlsym` recursion in a native
callback. Constructor-time `dladdr` verifies both alias addresses belong to the
same provider as the libc version function. The exact result and errno from each
forwarded operation are preserved, including EDEADLK/EPERM failure paths.

After successful acquisition at the pinned selected lock caller, a fixed-size
thread-local scope copies the protected token, full key and 64-bit content
generation. The exact metadata lock/unlock pair is observed only inside that
scope. Before its real unlock, the valid flag and stored full key must match;
only the first 32-bit native metadata word is copied. At selected pre-unlock,
all selected fields are compared again. Nothing retains an escaped native record
pointer after unlock.

Zero/multiple metadata pairs, nesting, inconsistent mutexes, lock failures,
invalid pointer arithmetic, invalid fields, changed tuples, unmatched selected
unlocks or an incomplete pair invalidate the observation. Ordinary out-of-scope
metadata lookups are passthrough. Samples after the 32-attempt limit are explicitly
excluded and their unlocks do not create artificial missing-pair errors. The
native mutex protects legitimate pinned record reads; arithmetic checks do not
claim arbitrary unknown memory is safe to dereference.

Initial-exec TLS avoids dynamic TLS allocation. The callback copies bounded
scalars into 32 preallocated publication slots with bounded atomic attempts.
Callbacks do not allocate, resolve symbols, take additional locks, format logs,
read files, access sockets, sample clocks or wait. The background writer alone
polls the arm marker, serializes records and enforces a 1 MiB output limit and a
120-second deadline after successful startup. There is no receiver-thread flush.

Capture closure disables admissions and uses sequentially consistent admission
checks and callback counters. Already admitted scopes can finish. The recorder
waits at most 100 ms for admitted callbacks/scopes, then latches quiescence before
the final drain. A late completion cannot make a timed-out closure complete.
Final evidence cannot be complete unless armed, at least one
sample was serialized, all completed records were serialized, no callback or
scope remains active, and no loss/invalid/failure/unmatched state exists. No
samples or an unarmed run is explicitly **not evidence**.

Each record is a **protected selection attempt**, including the normal
missing-metadata branch. It is never an accepted presentation, GPU fence, source
session/frame identity or eye/UV association. Output keeps `accepted_submit`,
`source_frame_associated` and `gpu_consumed_coherent` false. The observation number
is local recorder order only; it must not be used as a native frame ID.

## Offline validation

Focused validation on 2026-10-11:

- strict host and AArch64 shared-library compilation with
  `-Wall -Wextra -Wpedantic -Werror`;
- 173 core assertions, including 64-bit generation preservation, pointer bounds,
  exact scope transitions, native lock failure, all conservative invalidation
  branches, ordinary metadata passthrough, sample 33 exclusion, queue saturation,
  concurrent producer publication, closure of an admitted scope after disarm,
  and a sticky incomplete quiescence latch despite late completion;
- the same 173 assertions under AddressSanitizer and UndefinedBehaviorSanitizer;
- 16 isolated real host PLT/preload cases covering native result/errno forwarding,
  wrong executable/hash, private path refusal, valid and invalid record branches,
  unarmed/no-evidence state, the sample limit, incomplete scope at process exit,
  and a forced writer-start failure with real constructor-time mutex reentry;
- AArch64 ELF inspection confirmed only the two public exports, versioned libc
  alias imports, 80-byte static TLS, and no `__tls_get_addr` or `dlsym` import.

The real host PLT test uses an explicitly compile-separated host-only testing
library with exact fixture executable hash and assembly return labels. That
library is temporary and removed after testing; its bypass is absent from the
production binary. It never establishes Valve runtime compatibility. The pinned
native call-site/field audit is inherited evidence; no new hardware observation,
image read, mapped-uniform read, receiver preload or Frame modification occurred.

Regenerable test binaries are removed after validation; small validation records
and currently reviewed production libraries are retained privately until the
integrator has copied or rebuilt them. A new complete repository gate is not
claimed for this focused scope.
