# Register-only resource-boundary census

## Implemented next step

The source adapter now observes one bounded native import/pending/reference
candidate through the existing authenticated client-to-libc mutex bindings.
It reuses the successfully born resource RPC pipe; the independent native
source review verifies that Import and Reference use exactly that pipe.
No new public interposer or native provider was added.

Enable `FRAME_MASK_CLIENT_RESOURCE_CENSUS` together with
`FRAME_MASK_CLIENT_CONNECT_OBSERVATION` when building the uninstalled client
observer. The separately prepared artifact uses `FCCENS01`, schema 3, rather
than silently extending a startup/connect-only envelope. Its retained hidden
16-byte scope object lives in a read-only, non-executable segment and is checked
by both offline preparation and session staging.

The three actual native boundaries are:

| Boundary | Client return RVA | Recorded result |
| --- | --- | --- |
| Import begin | `0x3623c0` | True lock entry registers and original lock result. |
| Import pending | `0x3624c4` | True unlock entry registers and original unlock result. |
| Following reference | `0x362d58` | True lock entry registers, token argument and original lock result. |

The source-specific register timeline must match the same pipe, output pointer,
frame arithmetic, full entry snapshot and original integer result. Pending
allows the native deferred-logging change to x21. Reference checks its actual
manager address. One atomically claimed thread exclusively owns sampler state;
the completed connect sampler does not terminate the resource sampler.
Missing completion, wrong entry/result, loss or retirement leaves the candidate
incomplete. Original native execution is always preserved.

A prepared startup-thread cookie distinguishes genuine ownership from a
recycled TLS address. The sampler reports that fact without reading another
thread's stack. Its large state is process-lifetime storage; the actual final
static TLS footprint remains **18 bytes**.

Capture records identify whether an original return value is available, and the
footer separately reports historical connect/resource sampler completion.
This records only six mutex-boundary observations. It does not copy stack,
heap, sockaddr, wire, image or shader data; the five existing startup-word reads
remain unchanged. A token argument or completed mutex timeline is not an
accepted resource import. Transaction, resource namespace, source-frame/eye,
UV and GPU proof flags remain false; strict carrier and mask activation remain
disabled.

## Concrete reviewed candidate

The own main ARM library is **351,232 bytes**, SHA-256
`9da41b376b2e686ae41234a21159ae1c38c6c807b9d2389bdadab70b0c9bfdb5`.
Its dependency-free audit is **69,080 bytes**, SHA-256
`df61de75e2e82f0c04b2daa148157147a0fe22469c780317a369195c03f3ef36`.
The prepared offline plan SHA-256 is
`7613d1eb4305c6f82b1a7f577f04e2f052efe110123e6c5c0bddac91eb4ca905`.
These files remain private. The coordinated native capture is now complete and
the diagnostic was fully removed; original Frame state was verified afterward.
Existing native references and proprietary bytes are not distributed.

Focused software evidence covers:

- Four root CTest cases, including the new census and unchanged readiness,
  connection and inert-loading controls.
- **670** census controls under ASan/UBSan, including eight actual recycled
  startup-marker addresses rejected by fresh-thread cookies. Native stack tests
  disable ASan's artificial return stack, as the existing startup suite does;
  production ownership guards were not weakened.
- The exact final ARM library executed with the pinned native libc/loader in
  three unrelated-process cases, **29 controls each**, leaving all five public
  libc bindings native.
- Offline preparation's 14 ordinary controls plus two actual private-artifact
  cases, and 59 session/recovery controls.
- **103** private native-source checks, 19 independent source controls and
  **171** independent final artifact/envelope/compiled-audit/receipt checks.
- **25** kernel-helper controls, including a real separate client/server
  process, unknown peer FD, aliases, rebinds, lifecycle changes and query limits.

After a bounded fixture/configuration repair, only its affected controls were
repeated; unchanged passing evidence was retained. No new complete local gate
is claimed. [CI at `b360bb2`](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38301181405)
also passed. These are software and retained-source checks. The subsequent
[actual Frame capture](CLIENT-RESOURCE-CAPTURE-20261012.md) separately confirms
all six register boundaries and a resource caller outside the startup thread's
prepared stack. Neither result is a working passthrough test.

## Exact kernel peer snapshot

`tools/inspect_client_connection.py` extends the existing read-only kernel helper
to find the peer FD in one specified process instead of guessing it. Supply
the live left PID/FD/process-start ticks and right PID/process-start ticks.
It examines at most 256 FD entries, uses four exact kernel queries within two
seconds, repeats their actual cookies, and rechecks PID/start, UID, namespace,
FD and reciprocal peer inodes. Aliases or changes reject the snapshot.

This is contemporaneous kernel metadata only. It does not authenticate the
historical FD from a previous capture, continuous descriptor ownership, a
Registry lease, resource namespace or video/GPU association. It installs
nothing, changes no native process and reads no socket payload.

## Coordinated runtime scope and recovery

Offline preparation requires both `--connection-observation` and
`--resource-census`. The reviewed session helper requires a distinct
`--ack-resource-register-census-scope`, as well as the existing startup/connect
acknowledgments and coordinated restart readiness. A plan or passing test does
not supply permission.

The concrete proposed capture remains **32 records, 64 KiB and 120 seconds**.
The deadline ends recording only. Installation, if authorized, stays in the
inventory-owned user directory and exact runtime Steam service override; no
native binary, game, system image, shader or PC Steam sender changes.

After coordinated rollback readiness, use the staged own helper's `rollback`
mode with `--ack-steam-client-restart-readiness`, archive the bounded capture and
receipts, verify the original service/native/environment-file hashes and affected
readable mappings, and remove only the matching owned inventory. No automatic
restart or redeployment is scheduled.

The completed capture establishes the full register timeline and reports that
the actual resource thread does not own the startup thread's prepared stack.
The next step must safely register that real caller outside hot callbacks.
A later separately authorized fixed-window/wire adapter can then use
[the concrete source plan](CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md).
Native lifetime and exact connection/resource/video/eye/UV/GPU association
remain separate obligations; the explicit-mask output stays opaque meanwhile.
