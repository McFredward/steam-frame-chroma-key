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

At checkpoint `7db6569`, `tools/compositor_raw_session.py` refused all activation
until the launch boundary was repaired. The known-unsafe version-1 deployment
remains rejected. Version-2 bundles now require the reviewed cold-freeze selector,
exact launch configuration and actual startup-isolation evidence described below.
An acknowledgment flag or old artifact cannot bypass this package contract.
Offline bundles and inert staging are not runtime authorization. Old artifacts
remain marked not for deployment in their private evidence directories.

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

At that checkpoint the Frame had been restored and both owned input/session
directories removed. The later 2026-10-14 attempt is recorded below.

Validation reused already passing evidence for unchanged areas. The integration
ran three focused CTest groups: 70 session controls, 44 selector core checks and
12 real-loader/process controls. A further targeted session test covers receipt
storage failure during recovery. The collector repair passed 37 health tests;
the subsequent PID-reuse repair and existing old-invocation control passed
separately. The new CTest health registration was checked without repeating that
whole suite. Previous raw runtime/writer and final ARM ABI results remain
inherited evidence, not a new complete-gate or hardware pass.

## Initial prototype and its isolation gap

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

## Cold-freeze replacement (2026-10-14)

The selector now freezes the five exact diagnostic getter values before the
observer reads configuration or creates either writer. It proves the exact
executable, main PID/TID, ordinary credentials, `AT_SECURE=0`, a single-task
phase before and after provider resolution, and restores the original signal
mask. It then publishes a clean actual `environ` once, preserving unrelated
entries and only the tiny selector preload. Native libc `system`/`popen` launches
therefore inherit no full payload, audit or trace activation.

`getenv` and `secure_getenv` retain the frozen diagnostic view only in that exact
owner PID, so delayed Capture and implicit Vulkan-layer initialization still
work. Fork children receive no virtual diagnostic values. Foreign getters and
launches preserve native results and `errno`; secure-execution restrictions are
not bypassed. The selector has no module TLS or C++ dependency. Kernel metadata
reads/provider discovery occur only during the explicit cold handshake, not in
getters, launch wrappers or rendering callbacks. The Vulkan loader uses secure
environment queries; see the
[Khronos loader architecture](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md).

`/proc/PID/environ` describes the initial exec environment, not the changed libc
pointer. Original stack strings are deliberately preserved. The startup probe
therefore permits exactly the declared seven initial inputs in the one target;
it separately requires a fresh current-PID raw header proving successful freeze
(ABI 1, provider mask 63). Header time is only a startup-freshness check, never
video-frame/eye association. Appending later records cannot invalidate that
immutable header, but changing its owned node or exceeding its bounds does.

The authoritative service-cgroup census checks every managed executable's
UID/PID/start/parent/executable/cgroup brackets, initial environment and mapped
diagnostic libraries. Non-target processes must have no full payload/audit/trace;
unreadability, changing census or ambiguous fork copies cannot establish
isolation. A current contamination is a fault. The probe retains cumulative
64-MiB metadata, 64-sample and 4096-process-check bounds across checkpoints.
Restoration disables this diagnostic-expectation probe and requires original UI
readiness instead. A failed top-level startup receipt and its failed gate cannot
be resumed as ready; active monitoring revalidates the frozen header.

Cold denial stays fail-closed. When single-thread admission itself is unavailable,
the selector cannot safely change a live target environment. No observer thread
or success header starts; readiness is refused and coordinated recovery is
required. This is a controlled diagnostic launch contract, not a sandbox against
an arbitrary raw exec syscall with a separately saved dirty `envp`.

Local evidence includes 19 real libc/loader/process controls (actual internal
`system`/`popen`, fork and constructor order), 48 core checks with sanitizers,
59 startup-health checks, the actual ARM64 bundle/profile validation, and inert
final-ARM forwarding/forced-unwind controls with the pinned native providers.
A real late Vulkan/Mesa loader run activates the actual passive layer after
freeze, including image/bind/view and query-pool lifetimes. These results do not
prove an ARM-kernel cold phase, clean Frame processes or a headset picture.
New hardware package/scope approval is still required before deployment.

The separate raw `InvalidEntry` retirement and missing source frame/eye/UV/GPU
associations are still described in
[the first-candidate analysis](COMPOSITOR-RAW-FIRST-CANDIDATE-20261013.md).
Startup health does not resolve them or activate passthrough.

## Cold-freeze hardware attempt and journal repair (2026-10-14)

The maintainer explicitly authorized the reviewed v2 package and immediate
original-state recovery on a detected startup fault. The operator uploaded and
staged only its five artifacts, deployment plan and three helpers, then consumed
restart readiness immediately. No native file or shader was modified.

The gate stopped on `Unsupported current journal evidence`. Its saved current
invocation includes normal colour-coded XRService/ONNX messages whose `MESSAGE`
is a byte array. This is a documented
[systemd Journal JSON encoding](https://systemd.io/JOURNAL_EXPORT_FORMATS/), not
by itself a SteamVR or loader failure. The same parser defect rejected the first
original-service recovery receipt. The operator removed the exact activation and
performed an additional original-service restart rather than leaving uncertain
diagnostic state. The maintainer subsequently confirmed a normal interface.

The new parser accepts only strings or bounded arrays of genuine integer bytes
0–255, preserving every byte. Unknown, missing, duplicate-value and oversized
representations remain non-proof. ANSI CSI control sequences are removed only
from the fatal-search view: a colour prefix must not hide the word boundary of
`VK_ERROR_DEVICE_LOST`. Original evidence and invocation/PID/time filtering are
retained. Local tests cover genuine source-shaped JSON, fatal arrays, malformed
values, byte limits and old/current invocation filtering.

The raw header records a successful exact-owner cold freeze with ABI 1/provider
mask 63. The initial cgroup sample maps the full observer/audit only in its one
declared compositor; its sampled helpers have neither those libraries nor trace
inputs. However, two short-lived processes disappeared during that census, and
the journal fault occurred before a stable UI/isolation verdict. This is useful
positive launch evidence, **not a passing startup gate or complete live-isolation
proof**. A separately declined native image route is described in the
[first-candidate analysis](COMPOSITOR-RAW-FIRST-CANDIDATE-20261013.md).

Following confirmed restoration, a stable census of 15 current managed processes
showed no owned diagnostic mappings; the service environment/drop-ins and both
activation paths were empty. The operator saved all bounded captures, verified
the exact owned file hashes and removed only the 28 recorded input/session files.
That cleanup did not restart SteamVR. Private operation, recovery and cleanup
receipts remain in `build/private-compositor-freeze-integration-20261014/`.
The failed automatic health receipts are preserved as failures; user confirmation
and later original-state census do not retroactively turn them into ready receipts.
