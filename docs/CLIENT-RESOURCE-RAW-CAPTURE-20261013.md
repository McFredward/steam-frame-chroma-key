# Actual bounded client import capture, 2026-10-13

## Result

The separately authorized raw diagnostic captured **one complete native client
import transaction** on the Frame. Its actual outgoing request and incoming
reply have the same sequence. The reply token equals both the native pending
result and the receiver's following reference argument. This closes the local
request/reply/publication question left by the
[cold-thread capture](CLIENT-RESOURCE-THREAD-CAPTURE-20261012.md).

This is a real headset capture, not fixture output. It does **not** establish
the matching compositor resource, continuous endpoint lifetime, source video
frame, eye, UV mapping or GPU consumption. The explicit-mask shader remained
disabled and rendered pixels were unchanged.

## Exact evidence

The deployed artifact was the
[reviewed raw candidate](CLIENT-RESOURCE-RAW-PREPARATION.md): main ARM library
SHA-256 `98221ae5a31e4aef77bae50818e1759347664b07059c6d23f3881d7a16de4696`,
audit `aa1f16df2bf0b7a0c3105a9950e9da5a826b860f04bd89b42aa225dd0d3b10df`,
and envelope `c84832bf3e62de6f0f6549e6418fe42ba61a4db275ec6bc3cab75952eb7f699f`.
The runtime implementation was `0f53189`; integration source was `f1c8181`.
Its [CI run](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38314848391)
passed before deployment. The six exact own inputs totalled 557,815 bytes.

The complete private capture is **46,541 bytes**, SHA-256
`cc0dc8bbe94498d739f4089b6ae76c1e12023a56ea133a56376bfe9b11318f63`.
It contains a header, **24 records**, and a complete deadline footer:
24 queued, 24 drained, finalized, totals quiescent, transaction completed and
not abandoned. There is exactly one matched raw aggregate and no raw-refused
aggregate or notification-loss record.

| Actual copied/observed fact | Result |
| --- | --- |
| Request | Opcode `0x234`, sequence 2, 67-byte payload, 79 bytes successfully written |
| Reply | Opcode `0x235`, sequence 2, 12-byte header and 9-byte token payload |
| Original transfers | One write, one header read, one payload read |
| Token comparison | Exact wire token = native parsed token = following reference token |
| Native boundaries | Begin lock, final pending unlock and following reference lock succeeded |
| Extra protected scalar copies | 360 bytes |
| Extra wire metadata copies | 33 bytes |
| Total extra private bytes | **393**, within the reviewed 2,101-byte cap |
| Request payload copies | **0**; payload was counted only |
| New kernel queries | **0** |

All six resource census records report current cold-thread ownership. A later
thread-ownership refusal (reason 9) ends acquisition after the matched raw
aggregate. It does not invalidate the already copied local transaction; it
also does not turn the finite cold capability into a continuous lifetime lease.
The following reference was observed at successful lock admission; the
subsequent reference RPC's success was not recorded or inferred.

The first capture was available before the maintainer's live-stream
confirmation. The maintainer subsequently confirmed normal Frame operation and
a visible GloomhavenVR PC-VR stream. Neither confirmation nor capture timing
identifies this particular native import as that game's video frame. Every
namespace, continuous-connection, source-frame, UV, accepted-submit and
GPU-consumption proof flag remains false.

An independent review passed **86 checks** against the actual final capture,
exact deployed artifact inventory, archived files and later restoration
receipts. It verified the full token chain, dense record ordering, caps and
complete footer; it did not treat an offline fixture or a summary as hardware
proof.

## Recovery and next step

The maintainer explicitly authorized both installation/restart and later
manual rollback. The deadline stopped recording only; it did not restart
Steam. After the coordinated rollback, Steam was active in its original state,
all 57 readable user-owned process maps were diagnostic-free, and selected
native hashes, environment-file hash, service configuration and manager
environment matched the preflight baseline. There were no unreadable affected
Steam-service maps. Exact own session/input directories and the runtime
service override were removed after archive/inventory validation. SSH closed.
The Frame is safe for standby; no temporary diagnostic remains installed.

Private captures, manifests, operator recovery instructions and verification
receipts remain under ignored
`build/private-resource-raw-frame-run-20261013/`. Regenerable upload data was
removed. No native, system, game, shader or PC Steam sender file changed.

Next, observe the compositor's corresponding native reply and its actual
public Vulkan resource using a separately reviewed bounded scope. Raw evidence
must stay separate from the strict lease-based carrier. Matching a token alone
cannot supply a cross-endpoint namespace or source-video identity.
