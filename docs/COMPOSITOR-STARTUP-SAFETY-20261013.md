# Compositor startup safety after the missing-UI run

## Hardware evidence

At 2026-10-13 21:58:45 CEST the private-export compositor diagnostic started,
but the same service invocation's `vrwebhelper` process 368028 failed while
loading `/opt/steamvr/bin/vrwebhelper/linuxarm64/libcef.so`:

```text
cannot allocate memory in static TLS block
```

The compositor stayed alive until the requested manual rollback at 22:00:53.
The maintainer saw a blue background without a usable interface and confirmed
normal startup after restoration. The old service/process/preparation check was
insufficient and had incorrectly reported that the diagnostic startup was ready.
Private evidence remains under
`build/private-compositor-preload-repair-20261013/blue-screen-capture/`.

The service-level `LD_PRELOAD` and `LD_AUDIT` environment loaded diagnostic
dependencies into unrelated descendants before the observer's constructor could
reject capture. This is a proven exposure boundary. The particular contribution
of each library to libcef's TLS failure was not isolated on hardware. A host
negative control reproduces the same loader error class with an initial-exec
TLS preload and auditing; it does not substitute for the exact Frame dependency
graph. Increasing global loader/TLS reserves is not the chosen workaround.

## Implemented protection

`tools/compositor_raw_session.py` refuses `activate` and `start` before publishing
activation files or running service commands. The known-unsafe deployment
cannot be re-enabled by an acknowledgment flag or environment setting. Offline
bundles and inert staging are not runtime authorization. The old artifacts are
marked not for deployment in their private evidence directories.

`tools/compositor_startup_health.py` collects bounded read-only evidence outside
graphics hooks. A restart is bracketed by its previous service invocation,
kernel PID/start identities, journal cursor and native log offsets. Readiness
requires fresh compositor graphics/render-thread markers and live UI browser,
system UI and libcef-backed renderer evidence. New loader failures, missing UI,
process replacement/loss, unreadable required evidence and stalled initialization
prevent readiness. A short resumable monitor detects subsequent loss without
restarting anything. Normal Gamescope warnings seen in a healthy session are not
fatal on their own. No result claims inspected pixels, a working headset picture
or an actual PC-VR stream.

Activation failure removes only exact owned configuration and reports that a
manual original-service restart is still required. After explicit restart
readiness, rollback removes exact owned activation first and restarts the
original service even if baseline evidence is unavailable or native manifests
were regenerated. Content, permissions, identity or environment mismatches still
prevent a restoration success claim. Native files are never rewritten. Fresh
restoration/UI receipts join the existing bounded capture archive.

The current Frame has already been restored and both owned input/session
directories removed. These new changes were tested locally; they have not been
deployed for a further headset run.

Validation reused already passing evidence for unchanged areas. The integration
ran three focused CTest groups: 70 session controls, 44 selector core checks and
12 real-loader/process controls. A further targeted session test covers receipt
storage failure during recovery. The collector repair passed 37 health tests;
the subsequent PID-reuse repair and existing old-invocation control passed
separately. The new CTest health registration was checked without repeating that
whole suite. Previous raw runtime/writer and final ARM ABI results remain
inherited evidence, not a new complete-gate or hardware pass.

## Launch selector prototype and remaining requirement

The C-only `receiver/compositor_launch_selector.c` prototype has no module TLS
or C++ dependencies and uses reviewed public libc exec/spawn providers. Its
configuration defaults to inert. Tests exercise six public launch routes through
a launcher, the target compositor substitute and clean UI descendants, preserving
arguments, unrelated environment, native results and `errno`. Tests also cover
real dynamic-loader failure, ELF surface, ambiguous paths and bounded environment
inspection. No installed target or deployment plan selects this prototype.

This is **not complete native launch isolation**. The retained current compositor
imports `system` and `popen`; their libc-internal launches can bypass public
exec/spawn interposition. A shell can therefore receive the full observer/audit
environment before any later public wrapper sanitizes a child. The optional
environment-cleanup API remains uncalled: lazy Vulkan/capture environment reads
and an already running observer writer prevent a safe claim that startup
configuration has been frozen in a single-threaded phase.

Before another hardware deployment, the implementation must establish a safe
boundary that also covers those native internal routes, preserve the implicit
Vulkan layer's delayed activation and validate actual helper environments and
mappings. A passing public-exec fixture alone cannot authorize it. Any changed
hardware package and restart scope must be reviewed before asking the maintainer
to reconnect; local package work must be finished before that coordination.

The separate raw `InvalidEntry` retirement and missing source frame/eye/UV/GPU
associations are still described in
[the first-candidate analysis](COMPOSITOR-RAW-FIRST-CANDIDATE-20261013.md).
Startup health does not resolve them or activate passthrough.
