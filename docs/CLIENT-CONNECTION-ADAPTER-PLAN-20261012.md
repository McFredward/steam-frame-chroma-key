# Next passive client connection adapter

The register-only first step described below has now produced a successful
[native hardware capture](CLIENT-CONNECT-CAPTURE-20261012.md). The remaining
private-field, lifecycle and resource-carrier scopes remain deferred; that
completed invocation does not satisfy their proof or authorization gates.

## Result and smallest useful scope

The [authentic startup capture](CLIENT-STARTUP-CAPTURE-20261012.md) now proves
the actual client mapping and successful birth of its intended embedded pipe at
client RVA **`0x649ff0`**, followed by native public initialization token 1.
It does not observe that pipe's later connection, descriptor lifetime, resource
transaction or either endpoint of the compositor channel. The completed run
cannot be extended retrospectively into such evidence.

The smallest next diagnostic is **one supported native connect-call record**
from a fresh start, using **registers and native outcomes only**. Keep the
existing first-load gate, exact mapping/provider authentication and
`GenericClient` `ObjectToken`. Add source-client audit forwarding for real mutex
**lock/unlock and connect only**: three additional bindings, five libc bindings
including existing init/destroy. At the genuine connect entry, real x0 already
contains the FD loaded by the pinned native instruction at `0x245f8c`, and live
x19 identifies the unchanged born static pipe. Record those existing entry
registers and the original result/errno while its real native mutex is held.
**No additional private field or stack byte**, socket query, payload, image,
native executable detour or PC Steam sender change is necessary for this scope.

This first record certifies only the supported native invocation at its original
return, **not a future-use connection candidate or lease**. In particular, do
**not** call `Registry::connected()` yet: that existing core sets
`local_socket_lifetime_observed` on its successful lease. A client-only callback
set does not establish process-wide descriptor invalidation coverage. Returning
such a trusted lease before that obligation is satisfied would overstate the
evidence. Keep the authentic object in its existing ObjectOnly state, use its
existing object label in the record, and leave lookup/strict carrier admission
unavailable. A record ordinal identifies the observation, not a new socket
epoch. No new generic lifecycle or carrier core is needed.

Suggested candidate facts are `supported_connect_observed`,
`native_connect_succeeded`, actual argument FD, actual original result/errno,
existing ObjectToken, current copied module/audit views and the matched native
lock scope. Set `continuous_connection_owned`, `local_socket_lifetime_observed`,
peer credentials and native namespace proof **false**. There is no copied
`native_fd` or `native_peer_pid`; do not manufacture either to satisfy a core
interface. The optional private-field scope below is deferred and is not
needed to establish this first native operation.

## Independently checked native inputs

| Input | SHA-256 |
| --- | --- |
| `vrclient.so` | `19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0` |
| `vrcompositor` | `0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96` |
| `vrlink` | `31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153` |
| Actual Frame libc | `be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b` |

The actual receiver API is the Steam-runtime library described in
[the corrected API audit](RECEIVER-STEAMRT-API-20261012.md), not the different
API used by the compositor. Preserve its `RTLD_NOW | RTLD_DEEPBIND` flags.
Every RVA below requires the independently validated actual mapping and a
current audit-open lifetime. This source review used retained private bytes and
the saved startup capture; it did not connect to or modify the Frame.

## Exact supported native connect timeline

The client connect function starts at `0x245e88`, allocates 192 bytes and sets
FP = SP. Its live x19 is its pipe pointer throughout the eligible connect path.

| Native boundary | Source-established facts |
| --- | --- |
| Public mutex lock, return `0x245ea8` | x0 = x19 = pipe; original return 0 establishes this thread's real native lock acquisition. Do not inspect fields in the before-lock callback. |
| Existing descriptor branch | Native code reads pipe +172 and calls close helper `0x244eb0` before replacing a previous FD. That nested target lock makes this first probe refuse the route; reconnect is not supported. |
| Socket return / native store | Public socket returns at `0x245f20`; the instruction there stores the actual w0 into pipe +172. This store has completed by the later connect entry. |
| Public connect, return `0x245f9c` | x19 = pipe, w0 = the actual FD loaded from pipe +172, x1 = native sockaddr at connect FP +80, w2 = native sockaddr length. Copy registers only; neither pipe fields nor sockaddr/stack bytes are read. |
| Connect result | Original result 0 is the successful local operation. The observer copies the actual result and errno; failure, unwind or missing after-event cannot yield success. A result -1 can be recorded as an actual failed call, never a connected endpoint. |
| Success-path public unlock, return `0x245fb0` | Occurs after connect's original return. Its before callback ends the observer's protected scope before the native unlock can release ownership. |
| Failure / unwind unlock | Failure has return `0x245ff0`; exception cleanup has `0x246044`. Native failure normally calls the nested close helper first, which this deliberately narrower probe refuses; exceptional/missing completion cannot retain pending state. |

After this one eligible native connect return and matched outer unlock, stop
the connection-only sampler with a terminal completed state. Do **not** return
to admission Idle and mistake subsequent ordinary RPCs for lost connection
proof: `0x1ccae8` continues immediately into handshake and locks this same pipe
at call `0x1ccc80` / return **`0x1ccc84`**. No future FD lease exists, so those
later native operations do not negate the historical recorded call. Native
forwarding, startup completion and global loss/shutdown/module retirement
handling continue unchanged.

Omitting public close is safe **for this invocation-only statement**: the record
copies the actual original connect argument/result, and no later code uses it as
an owned descriptor. It cannot report that the FD remains open, that no other
thread replaced it, or that an endpoint still has that connection. It creates no
trusted Registry lease, copied credentials or retrospective kernel epoch. An
external close/replacement is therefore an explicit unobserved lifetime gate,
not an ignored event in a supposedly continuous proof. During the active
supported window, nested target lock/close-helper paths are refused before
making the successful-call statement; normal public providers still execute.

### Deferred close/destruction lifecycle expansion

The close helper `0x244eb0` acquires the same native mutex at public lock return
`0x244ec4`, reads pipe +172, calls shutdown at `0x244ed4`, calls public close at
`0x244edc` (return **`0x244ee0`**), and only then writes FD -1 at `0x244ee4`.
For a later lifecycle adapter, observe/invalidate at close **entry**, even if native close fails or is cancelled;
never wait for that later private store to infer retirement. Also retire the
future-use candidate when the tracked pipe's close-helper lock succeeds, before its native
shutdown begins. This narrower source event avoids introducing a shutdown
forwarder in that fuller lifecycle probe. It does not observe arbitrary shutdown
calls outside that helper.

Pipe destruction at `0x245098` first calls that close helper, then frees native
containers and restores its 48-byte frame. It tail-branches at `0x245128` to
`0x257098`, which itself tail-branches to public mutex destroy. Consequently
destroy has the destructor's caller LR, **not one universal destruction return
site**. The already born pipe's exact public mutex argument identifies which
tracked object to retire; preserve the actual native destroy outcome. Unsupported
or failed destruction, module retirement, observer loss or a process child must
leave all candidate evidence unavailable.

The close helper's ordinary unlock is a **tail branch after its 32-byte frame
has been restored**. During a connect/reconnect call it therefore retains the
outer caller LR `0x245ebc`, or failure-path `0x245fe4`. Treat these as the nested
close-helper release, not the outer connect release. There is no universal
`0x244efc` close-helper unlock LR. The outer connect frame/SP remains unchanged
across the eligible lock, connect and final unlock callbacks. Check x1 = this
frame +80 using arithmetic on entry scalar values; never read that address.

The native sockaddr builder returns string length +3 after copying at most
106 path bytes. An observation limit of 3..110 bytes is a conservative fixed
sockaddr cap, **not** a proof that every native input satisfies it. Refuse larger
or malformed argument shapes unchanged-native; do not copy paths or invoke a
new length query.

The pipe is recursive: the native init helper sets mutex type 1 before the init
tail branch. The preferred first probe tracks only its one eligible outer
lock scope and **refuses** recursive target acquisition; it does not implement
reconnect, recursive close or future descriptor ownership. End local ownership
before the matching unlock. Do not read the native pipe's private +64 logical
recursion counter or introduce an observer mutex. Missing acquisition, unmatched
release, notification recursion, signal/nesting ambiguity or changed object
lifetime loses the observation. An original connect may be a
cancellation point; existing true-entry CFI and forwarding must propagate unwind,
and pending state must never be committed without its real after-event.

## Preferred acquisition: no private read

At the exact audited connect public entry, check the full born target address
in x19, actual authenticated source-client mapping, exact LR `0x245f9c`,
positive same-thread tracked native lock ownership, and a valid nonnegative
32-bit FD argument. Record that true entry and original result/errno; refuse
changed module/object/scope, missing after-event or observer loss. The native
function's existing store/load/order proves what FD argument native connect
received; it does not establish that a descriptor has remained connected after
that invocation. Native success is actual original result 0.

This needs no private stack window or per-thread bounds discovery. The existing
entry snapshot retains original SP/FP/LR/registers as scalar values; do not
dereference them. Later caller threads are permitted only for this register-only
supported-call observation and its own genuine matching lock scope, never by
pretending they inherited the startup thread's stack bounds. No new hot syscall,
`pthread_getattr_np`, `/proc` read or module lookup is introduced.

## Deferred optional field-only acquisition

The two fields are:

| Private owned scalar | Actual RVA | Meaning |
| --- | --- | --- |
| signed 32-bit pipe +168 | **`0x64a098`** | Cached native peer PID; diagnostic only at connect. |
| signed 32-bit pipe +172 | **`0x64a09c`** | The native connection FD assigned before public connect. |

If a later diagnostic actually needs these fields, the exact target is inside
the client's RW `PT_LOAD` range
**`[0x607a08, 0x65cff0)`**; these fields are in its BSS portion. Authenticate
that complete mapped-segment geometry and resident readable ownership during
the allowed cold preparation/discovery, then retain its current module lease.
An authenticated RX image alone is not permission or proof to read its RW bytes.
The target is already born and has a static source address, so no arbitrary
heap pointer, pathname-derived bias or saved FP chain is needed.

For this separately approved limited route, genuine source-client public entry,
exact source return
PC, x19 equal to the **full validated static target**, the current ObjectToken,
and the previously observed same-thread successful native lock are sufficient
to authorize those two fields. At connect before/after, require actual argument
FD equal to the copied native FD, unchanged module/object/lock scope and no loss.
Recheck after original forwarding while native ownership still holds. This is
not a generic assertion that any plausible LR permits memory reads.

This optional follow-up copies **8 private bytes** per eligible before/after observation:
two fixed s32 scalars, never a surrounding struct. It adds **zero private stack
bytes**. The original full entry snapshot still supplies native registers and
SP through the existing assembler; those values are recorded, not dereferenced.
Later caller threads need no inherited startup stack bounds for this field-only
route, because it reads no stack. Do not call `pthread_getattr_np`, inspect `/proc`
or introduce a fresh stack discovery on a hot callback. The original five-word
constructor acquisition remains separately unchanged.

Native reconnect does **not** reset pipe +168. The payload helper queries real
`SO_PEERCRED` at return `0x245428` and stores its returned PID at `0x245434`;
native failure can retain the old PID. A positive cached PID at connect is not
fresh credentials, UID/GID or a unique peer endpoint. The probe must not query a
socket itself, fabricate PID 0 from a past constructor, or turn the cached value
into a trusted namespace. The preferred initial register-only probe does not
acquire either field, so it cannot report even a cached peer PID.

## Startup and caller threads

The direct B/BL census across 23,228 native unwind function ranges finds eleven
callers of `0x245e88`; they include unrelated embedded/temporary pipes and must
not be adopted. For the observed static target, `0x1bbcb0` passes HMD +`0x1158`
to `0x1ccae8`; that function adds `0x1120` and calls connect at `0x1ccb4c` or
retry `0x1ccba8`. The resulting pipe is the unchanged HMD +`0x2278` target.

An actual direct synchronous path exists inside native core Init:
`0x1bebe8` → call `0x1bf8e4` / `0x1dedc8` → call `0x1dee0c` / `0x1be240`
→ tail branch `0x1be250` / `0x1bbe90` → call `0x1bcc28` / `0x1bbcb0`
→ call `0x1bbd20` / `0x1ccae8` → `0x1ccb4c` / `0x245e88`.
This makes delaying connection instrumentation until `InitAfter`, after
`dlopen`, or until a writer/worker's later map refresh invalid: it could miss the
first connection while genuine initialization is still executing. Prepare all
providers and register the immutable notification subscriber before forwarding
the first natural Init. Extend the selective DEEPBIND audit binding before any
eligible client call can occur. Preserve lazy/eager native binding behavior.

This path proves that a same-initializer-thread connection can occur, not that
all future calls use that thread or that this path was taken in the saved run.
The reconnect vtable includes `0x1cd680`, which closes native state and
tail-forwards to `0x1ccae8`. Later receiver imports occur through the separately
proved resource-manager route. No process-first thread or stack snapshot may be
reused to certify arbitrary later threads or calls.

## Exact hot providers and existing code reuse

The actual libc's public and distinct versioned aliases independently match:

| Function | Actual provider RVA / bytes | Existing distinct forwarding alias |
| --- | --- | --- |
| mutex init | `0x86a00` /556 | `__pthread_mutex_init@GLIBC_2.17` |
| mutex destroy | `0x86980` /56 | `__pthread_mutex_destroy@GLIBC_2.17` |
| mutex lock | **`0x87370` /760** | `__pthread_mutex_lock@GLIBC_2.17` |
| mutex unlock | **`0x88f74` /20** | `__pthread_mutex_unlock@GLIBC_2.17` |
| connect | **`0xebcb0` /208** | `__connect@GLIBC_2.17` |
| close, deferred lifecycle | `0xda610` /180 | `__close@GLIBC_2.17` |
| read, for later carrier | `0xdf330` /204 | `__read@GLIBC_2.17` |
| write, for later carrier | `0xdfee0` /204 | `__write@GLIBC_2.17` |

Append the three connection-only operations to the existing audit/bridge enum
and immutable preparation; retain existing enum values and refusal behavior.
Admit only bindings from the current authenticated client cookie to the current
authenticated MAIN libc cookie and exact provider RVAs. Other namespaces,
alternate/dlsym bindings, unknown source images, stale views or partial opt-in
remain native/unavailable. Sticky binding loss must notify MAIN immediately;
already installed veneers still forward the real provider after evidence retires.

Reuse `native_import_entry.S`'s existing entry macro and
`import_forwarding.cpp`'s existing lock/unlock/connect dispatchers. Link a
minimal selector for those three veneers beside the existing two mutex entries,
without duplicating symbols or pulling in fake Vulkan providers. Verify the
final artifact's original registers, SP/FP/LR, native result/errno, CFI and forced
unwind behavior. Do not create C wrappers that change the original caller LR,
resolve symbols on a callback, use audit-local libc/TLS as the native provider,
replace libc with raw syscalls or mutate native connection state.

Reuse the existing fixed queue, writer, manifest, scope/recovery helper and
32-record /64-KiB /120-second limits. The hot callback publishes copied candidate
values only. File formatting/output remain on the existing background writer.
At record/time/capacity loss, retire sampling and candidates without restarting
Steam. Manual removal/restart remains separately coordinated. Preserve the small
receipts and remove generated build/disassembly caches after each completed run.

## Continuous connection and the later carrier are separate gates

Before enabling the existing `Registry::connected()`/`check_lease()` and feeding
`client_carrier_core`, prove uninterrupted descriptor invalidation coverage:
close and shutdown, duplicate/replacement operations (`dup`, `dup2`, `dup3`,
rebind-capable `fcntl`), direct/external descriptor operations, module/process
retirement and observer loss. Source-client DEEPBIND routing alone does not cover
other libraries, other namespaces, direct syscalls or missed bindings. The client
imports both `dup` and `syscall`; absence of `fcntl` in this library is not a
process-wide guarantee. Actual libc has no distinct public `__dup` or
`__shutdown` export, so extending coverage needs its own cold-verified original
provider forwarding/ABI review; it is not supplied by this plan.

Once that obligation is satisfied, reuse the existing Registry rather than
minting a new epoch from the first FD snapshot. Its connected lease is local;
independent kernel peer-inode/process-start matching, outside callbacks and in
its own reviewed scope, is still required to pair the compositor endpoint.

The next resource step reuses `native_client_route` and `client_carrier_core`
exactly: import lock `0x3623c0`, write `0x2456c8`, reply header/payload reads
`0x245334` / `0x245460`, pending unlock `0x3624c4`, following reference lock
`0x362d58`. It additionally needs a **new** approved private active-stack/span
scope, beyond the register-only probe. Its required stack scalar slots are:

* Import frame: +0,+8,+16,+136; Pending also s32 +224 and u64 +232.
* Request write frame: +0,+8,s32 +32,s32 +40; send frame +0,+8,+24,+64;
  import frame slots above. The packet VLA extent derives from bounded actual
  length and its checked source prologue; arbitrary FP chasing is forbidden.
* Reply read frame: +0,+8,+16; waiter +0,+8,s32 +116; payload also s32 +156;
  import frame slots above. Header buffer is a source-owned 12-byte live stack
  allocation at read FP +80; the reply payload is exactly nine native-owned
  bytes. Copy only each successful original transfer's owned prefix.
* Reference frame: +0,+8,+16,+104,+224; the decoder matches the preceding
  copied import/receiver/record identity, never a deferred private heap read.

Request payload bytes are counted, **not read or retained**; only its 12-byte
wire header is copied. The accepted response is exactly a 12-byte header plus
9-byte fixed64 payload. Native `ParseFromArray` runs before sequence/opcode
rejection and its return is ignored, so a private nonzero parsed token by itself
does not prove the accepted reply. Require complete matching wire bytes, native
wait success, parsed presence/token agreement, and the following real receiver
reference after native token publication. No descriptor lease, constructor
receipt or equal numerical token substitutes for those transaction checks.

That later adapter must prove its own active fixed windows/ownership on every
caller thread; the startup thread's cached bounds do not supply them. Do not
enable it as part of the register-only connection probe or report an incomplete
connection as a strict `continuous_connection_owned` carrier route.

## Evidence and remaining scope

The private validation performs **100** exact hash/instruction/provider,
mapped-segment/field, direct-caller and saved-capture checks. Its source and
small JSON receipts remain under ignored
`build/private-client-connection-source-plan/`. Native disassembly is a
regenerable private cache and is removed after completion. This proves the
bounded source plan, not a new runtime connection or provider installation.
The narrowed three-binding scope adds **7** direct-byte/hash checks, including
the source-proven same-pipe handshake lock after the supported call. These are
focused static checks, not another native hardware capture.

The independent implementation source review verified publication immediately
after the authentic target birth, before native Init returns; immutable copied
module metadata; a single global sampler owner and completed state across
thread-local scopes; strict original Before/After matching; and global target
destroy/re-init refusal before sampler filters. The re-init guard belongs to the
libc notification subscriber, not the separate OpenVR Init subscriber. Ordinary
post-connect target RPCs must not reactivate the completed sampler. The private
`integration-source-review.json` records the reviewed source hashes, resolved
findings and proof limits. This is read-only source evidence, not final-artifact
verification or a successful native connection capture.

New permission is required for the expanded register-only callback set. It
introduces **zero additional private bytes** beyond the unchanged five startup
words. The optional two-field acquisition and later carrier stack/wire copies
require their own new scopes; approval for five startup-stack words does not
authorize them.
Prepare and review the concrete final artifact, focused positive/refusal/native
ABI fixtures and recovery manifest before requesting that permission. A fresh
coordinated Steam start is necessary to observe birth and first connection.

Even a successful connection candidate leaves continuous FD ownership,
cross-endpoint namespace, accepted receiver video submission, source session/
frame/eye, native UV/reprojection mapping and coherent GPU consumption **false**.
No mask shader or passthrough image may be enabled from this evidence alone.
