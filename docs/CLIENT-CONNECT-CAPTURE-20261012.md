# Authentic register-only client connection capture

## Hardware result

The repaired hidden-only observer was deployed after the maintainer's
"Leg los" authorization and fresh native/service checks. Steam restarted
normally. The maintainer confirmed both a usable Frame interface and a visible
GloomhavenVR PC-VR stream. No new native assertion dump appeared during the run.
This confirms hardware startup for the repaired observer; it does not reproduce
or establish every internal cause of the earlier Steam UI failure.

The actual receiver process produced a stable **20,897-byte** capture, SHA-256
`307b0e0c98e1cd579d12cc9c5d897a91fe1bfb26bd3c42e0185757b63e81aa1a`.
It contains a header, **12 ordered records** and a deadline footer. The footer
reports 12 queued/drained records, completed native startup and expected
`Finalized` refusal 17. No connection refusal or queue-loss record occurred.
The 120-second deadline ended recording only; it did not restart Steam.

The five client-to-libc binding records match the original native provider RVAs.
The authenticated client mapping and successful embedded-pipe birth precede
one supported native `connect` result. The recorded entry matches the same
born ObjectToken, source return RVA `0x245f9c`, live pipe register and supported
frame/argument geometry. The original FD argument was 8 and original result
was **0**. Preserved `errno` 11 does not turn a successful call into failure:
native success does not require clearing a previously set errno. The following
original VR initialization returns token **1**.

The deployed artifacts were the reviewed 209,904-byte main library
`cfaf1cf6147fbcf7b219f8fa9a630fb688cc1a9caacfcc560039ac0e94a96703`
and 69,080-byte audit library
`169918eaeaa8eab6c8657eaa2b74f123c6f90db4074d828614419383834f275f`.
The plan hash was
`2eaa1452669c6cec1b6941fb7566213775ca2bd6ce60a1d21bf127e7f101448a`.
The unchanged native profiles and current helper files were checked before
staging and activation. All captures and native reference bytes remain private
and untracked.

## What this establishes

This is an actual, successful native connection invocation, beyond the previous
mapping/constructor-only evidence. Admission enforces a source-specific native
lock acquisition and matching connect Before/After; the capture serializes the
successful connect outcome. It does **not** separately serialize the outer
unlock or sampler-complete state. A clean footer must not be presented as a
directly recorded successful unlock.

No additional private field, sockaddr, packet, pixel or shader bytes were read;
the five existing startup-word checks stayed unchanged. The callback scope
remained restricted to authenticated receiver/client bindings, and unrelated
Steam/UI calls retained native libc providers. No PC Steam sender was modified.

The observed FD number and local ObjectToken identify only their named facts
at this invocation. They do not establish continued descriptor ownership,
kernel peer pairing, a resource namespace, source session/video frame/eye,
native UV/reprojection mapping or coherent GPU consumption. Those proof flags
remain false, `Registry::connected()` and strict carrier admission remain
disabled, and no mask shader or passthrough output is enabled.

## Independent review and recovery

A separate worktree review passed **72 bounded read-only checks** of the final
capture, ordering, module/artifact/profile pins, source register geometry,
actual result and proof limits. It ran no build, test suite, network connection
or additional Frame access. Software validation for the unchanged source is
inherited from the already passing focused ARM checks and
[CI at `38b5b16`](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38295295162);
documentation updates are not reported as another complete-gate run.

After the maintainer's explicit rollback readiness, the helper removed its exact
owned activation and restarted original Steam. The original service/manager
environment and environment-file hash matched. A fresh check also confirmed
unchanged native binary hashes. All **57 readable user-owned process maps**
were free of the diagnostic libraries; four restricted processes were outside
the changed Steam service scope. The six uploaded files and staged session were
removed only after their exact inventories and recovery receipts were archived.
SSH was closed and standby readiness was confirmed. The completed worker
worktree and regenerable upload wrapper were removed; small source/review,
capture and recovery evidence remains private.

## Next useful proof

The next step is the connection/resource association, not a replacement shader.
Use the real invocation to guide a fresh, bounded kernel peer-pair snapshot and
native lifecycle review. The existing
`tools/inspect_unix_connection.py` can validate an exact contemporaneous pair
using process-start identity, endpoint inodes and kernel cookies. Its snapshot
does not prove uninterrupted lifetime from this completed capture. Never reuse
the historical FD 8 or match by a timestamp/most-recent record.

Before strict resource-carrier admission, prove descriptor invalidation
coverage and the exact accepted import request/reply/reference route described
in [the adapter plan](CLIENT-CONNECTION-ADAPTER-PLAN-20261012.md). Expanded private
stack/wire acquisition requires a concrete reviewed artifact and its own scope
authorization; this register-only success does not authorize it. Keep the
hidden-only client audit routing and preserve native rendering until source,
eye, resource, UV and GPU association are established.
