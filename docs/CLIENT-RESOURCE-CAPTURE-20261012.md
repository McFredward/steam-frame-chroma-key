# Native resource-boundary capture and recovery

## Hardware result

The maintainer authorized the reviewed register-only resource census and four
bounded external kernel metadata queries with "Freigeben; jetzt bereit".
Fresh native, environment and service pins matched before installation. The
eight owned input files totalled **545,639 bytes**. After the coordinated Steam
restart, the maintainer confirmed a usable Frame and a visible GloomhavenVR
PC-VR stream.

The receiver produced a stable **33,298-byte** private capture, SHA-256
`9a20abdeaa17c18573db9b5b0a49654552fcc1bb81ecb5aed38fb579b243a3fe`.
Its header, **18 ordered records** and deadline footer include all six expected
resource observations. No refusal or queue-loss record occurred. The footer
reports 18 queued/drained records, successful startup, completed connect and
resource samplers, and expected finalization reason 17. The 120-second deadline
ended recording only; it scheduled no restart or removal.

| Boundary | Actual client return RVA | Before/After result |
| --- | --- | --- |
| Import begin | `0x3623c0` | Original lock result **0**. |
| Import pending | `0x3624c4` | Original unlock result **0**. |
| Following reference | `0x362d58` | Original lock result **0**. |

All pairs retain identical original entry snapshots. Source-specific pipe,
output pointer, caller-frame arithmetic, manager and token-argument checks
match. The full timeline uses the resource pipe whose actual successful native
birth was captured during startup. Both sampler completions are now explicitly
recorded, unlike the preceding connect-only capture's implicit unlock evidence.

The decisive new observation is **`startup_thread_stack_owned: false` at every
resource boundary**. The actual resource caller is not covered by the cold
startup thread's owned-stack registration. Consequently this observer copied
no resource stack, heap or wire data. A later decoder cannot safely reuse the
startup thread's bounds for these calls. It must register the real calling
thread with a supported lifetime before any private acquisition.

The main library, audit and plan are the exact reviewed artifacts recorded in
[the census implementation](CLIENT-RESOURCE-CENSUS.md). No native/packet/image
bytes were redistributed, no shaders or rendered pixels changed, and no PC
Steam sender modification occurred.

## Kernel connection lookup

The external helper used a fresh receiver PID/start identity and current FD,
and authenticated the exact compositor executable before its lookup. It made
**one** exact kernel socket query, then refused because the known compositor
process exceeded the helper's **256 FD-entry scan limit**. It issued no right
endpoint query or retry. Thus this run establishes **no validated kernel peer
pair**, historical captured-FD ownership, continuous socket lifetime or native
resource namespace.

The failure concerns an internal bounded FD scan, not a native connection
failure. The corrected helper now offers an explicit bounded limit up to 4096
and passed nine focused controls, including a real separate peer with over 300
FDs and late alias rejection. It retains the four-query/two-second budget and
exact process checks. This is a local repair, not a retrospective hardware
pairing result. Do not reuse the completed run's descriptor number as a future
lease.

## Validation and complete recovery

A root metadata review passed **126 checks** against the actual capture,
source-specific entry relations, false proof flags and recovery receipts. This
is evidence review, not another complete test gate. The already passing source,
ARM and CI evidence in the census document is inherited unchanged.

After "Ja, jetzt zurücknehmen", the staged helper removed the owned runtime
override and restarted original Steam. All **57 readable user-owned process
maps** were free of the diagnostic; no unreadable process belonged to the
affected Steam service. Native binary and environment-file hashes, the original
service environment and manager trace keys matched the pre-install baseline.

Only the exact archived session inventory and eight uploaded inputs were
removed. The final check found no diagnostic override, staging folder or
activation. Steam was active, SSH was closed and standby readiness was
confirmed. The regenerable upload wrapper was deleted; small source, capture,
inventory and recovery evidence remains private.

The old operator verification script initially refused the known own main
library against its previous 256 KiB receipt cap. The correction accepted only
the current library's exact size and SHA-256; unknown receipt bounds stayed
unchanged. This local verifier correction changed neither the observer nor the
Frame's activation and was recorded privately.

## Next proof

Prepare lifetime-safe bounds for the actual resource thread outside render and
mutex callbacks, then review the minimum separately authorized sparse native
transaction capture in [the integration plan](CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md).
Successful mutex operations or a nonzero reference argument do not prove an
accepted resource import, selected source video frame, eye, UV mapping or GPU
consumption. Those associations remain incomplete; strict carrier admission,
mask shader activation and genuine-mask passthrough remain disabled.
