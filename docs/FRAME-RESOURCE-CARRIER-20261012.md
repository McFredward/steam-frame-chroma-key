# SteamOS 0.4.5 resource-manager carrier audit

## Result and proof limits

The current client library provides a source-proven request/reply route from
the receiver's `ImportDmabuf` call to the compositor's successful import reply.
Its public boundaries can record the exact sequence, opcode, reply token and
subsequent receiver reference without a private executable detour or a deferred
heap read. Two important native details invalidate tempting shortcuts:

* The receiver uses an embedded derived resource-manager interface, rather
  than the standalone manager's vtable address.
* The native reply waiter parses payloads before checking their sequence and
  ignores the parser result. A parsed token alone is insufficient proof that
  this invocation accepted that reply.

The native source route is now established for the retained binaries. There
is still no runtime observation of this `vrclient.so` mapping or cross-endpoint
socket pair. Local FD generations plus `SO_PEERCRED` do not uniquely identify
the two ends of a Unix stream. The complete namespace join must stay unavailable
until that connection relationship and the exact transaction are established.
This audit performed no Frame connection, runtime inspection, installation,
native execution, pixel read or rendering change.

## Exact private inputs

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `vrclient.so` | 6,511,792 | `19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0` |
| `vrcompositor` | 6,026,096 | `0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96` |
| `vrlink` | 2,425,024 | `31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153` |
| Actual Frame libc | 1,722,992 | `be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b` |
| Actual Frame loader | 204,048 | `2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e` |

The separately authorized read-only acquisition retained
`import-provider-preflight.json`. The receiver was not running during that
copy, so a pathname/hash match does not prove the actual library mapping in a
future receiver process. All addresses below are ELF virtual addresses for
these exact inputs and require validated process-specific load biases.

## The actual receiver interface

At receiver `0x345ec`, an `IVRClientInternal_XXX` interface's slot 40 returns
the resource manager; receiver `0x34604` invokes that manager's slot 56.
The subsequent reference at `0x34628` invokes slot 24 using the same record's
token. These slots are resolved independently through current library
construction and relocations:

| Current client-library object | Verified route |
| --- | --- |
| `CHmdSystemLatest` internal secondary interface | Address point `0x6174e8`, offset-to-top `-24`; slot 40 is `0x1c31a8`. |
| Internal getter | Adds `0x21e8` to the internal interface pointer, returning HMD object `+0x2200`. |
| Embedded manager construction | Constructor `0x1b954c` installs address point **`0x620ed0`** at HMD `+0x2200`; GOT `0x62c620` resolves the enclosing vtable at `0x620cc8`, then adds `0x208`. |
| Enclosing derived type | RTTI identifies `CVRCompositorClient`, containing the manager base subobject. |
| Actual embedded slot 56 | Relocation `0x620f08` resolves `ImportDmabuf` at **`0x362178`**. |
| Actual embedded slot 24 | Relocation `0x620ee8` resolves `RefResource` at **`0x362ce8`**. |

The standalone `CVRIPCResourceManager` address point `0x619148` resolves the
same two methods, but it is not the constructed receiver interface's address
point. Pinning only that standalone vtable would reject or misidentify the
actual receiver route. The latest `IVRIPCResourceManagerClient_004` name also
exists and is registered; that does not replace the receiver's demonstrated
internal getter route.

## Import request and native accepted-response boundary

Client `ImportDmabuf` at `0x362178` allocates 352 bytes with `FP = SP`.
It initializes the caller output to zero. After validating and exporting its
plane handles, it constructs the request, then locks the actual native pipe.

| Boundary | Live evidence |
| --- | --- |
| Public mutex lock, return `0x3623c0` | Import-specific begin; pipe argument and saved pipe at frame `+136`. The sequence has not yet been loaded/incremented here. |
| Native sequence assignment | Reads pipe `+164` into `w25` at `0x3623c8`, increments the native counter at `0x3623d8`; the actual outgoing packet supplies the authoritative sequence. |
| Request send, caller return `0x362418` | Sends opcode **`0x234`** through `0x245938`; sequence in `w25`, pipe in `x21`, serialized payload in `x23`. |
| Native response wait, caller return `0x362440` | Calls `0x364ad8` with expected opcode **`0x235`**, the exact sequence and response object at import frame `+184`. |
| Import's final public unlock, return `0x3624c4` | Live `w24` is native wait success; parsed token-presence bit is frame `+224`, full parsed token is frame `+232`. Caller output pointer remains live in `x20`. |
| Output publication | At `0x3624dc`, the full token is written to that caller output, after presence validation. |

The caller output is receiver record `+32`, supplied at receiver `0x345f8`.
The original receiver import-call return is `0x34608`. In the direct route,
`importFP + 352` is the receiver's active FP, with the expected saved LR
`0x34608`. Source-known active frame checks must precede any copying; a generic
backtrace or a later caller-pointer scan is not a substitute.

Do not read `w25` at the import begin and pretend it already contains the
sequence: its assignment occurs after that public lock. Similarly, a final
unlock record is a **pending publication**, not proof that the caller output
store has already happened.

## Reply parser contamination: validate actual wire bytes

The waiter at `0x364ad8` allocates 192 bytes. Its expected sequence is frame
`+116`, received header opcode/sequence/payload length are `+148/+152/+156`.
It reads the header through `0x245220` and payload through `0x2453c8`.
The header public `read` return is **`0x245334`**; the payload public `read`
return is **`0x245460`**. Both helpers support partial reads.

Payload parsing occurs at `0x364d88` through public `ParseFromArray`; its
return value is ignored. Only afterwards do `0x364be4..0x364bf0` check the
sequence and `0x364bf4..0x364bfc` check the opcode. A sequence mismatch loops
for another header after already parsing the previous payload into the response
object. Consequently, a later native true result or present token alone cannot
exclude state retained from a mismatched or malformed packet.

The observer must reconstruct only bounded, successful read spans for the
actual native invocation and pipe lifetime. The eligible complete reply is
exactly **21 bytes**: little-endian opcode `0x235`, the exact outgoing request
sequence, length 9, protobuf fixed64 tag `0x09`, and the full 64-bit token.
Require the native parsed presence/token at the final boundary to equal that
exact matching reply. Reject mismatched, malformed, duplicate, extra, partial,
lost or invalidated evidence; never inherit native parser success implicitly.
No raw image or mapped-uniform data is involved.

## Observe actual receiver publication through the following reference

The receiver's next `RefResource` call provides a public boundary after the
output store, avoiding a private instruction hook:

* `RefResource` at `0x362ce8` allocates 240 bytes with `FP = SP`.
* Its public lock has return PC **`0x362d58`**. Live `x24` is the full token
  actually passed by the receiver; it is also stored at frame `+224`.
* Its saved native LR must be receiver **`0x3462c`**, and `refFP + 240` must
  equal the same receiver caller FP as the preceding import.
* The method's saved caller `x19` at frame `+16` is the receiver record pointer.
  Require it to match the preceding import output pointer minus 32.
* Require that live reference token, pending import token and exact reply token
  agree, with the same proven connection/resource lifetime and no observer loss.

This establishes that the native receiver has read its assigned record token
and invoked the reference API. A successful reference request/response is
opcode **`0x226` / `0x227`**; its begin alone does not prove the reference
operation succeeded. The receiver's selected content generation and eventual
accepted video submission remain separate proofs. Reused native resource tokens
must not be turned into video-frame IDs.

## Public socket lifecycle and credential timing

| Operation | Client `vrclient.so` public return PC | Compositor public return PC |
| --- | --- | --- |
| Accepted socket | `0x244ff0` | `0x172528` |
| Connected socket creation | `0x245f20` | Independently prove any eligible server-side creation route. |
| Successful connect | `0x245f9c` | Native accepted endpoint is a different FD. |
| Payload `SO_PEERCRED` | `0x245428` | `0x172960` |
| Header read | `0x245334` | `0x17286c` |
| Payload read | `0x245460` | `0x172998` |
| Write | `0x2456c8` | `0x172c00` |
| Native close | `0x244ee0` | `0x172418` |

On the client, socket creation assigns FD at `0x245f20`; connect is performed
under the native pipe mutex. Accepted sockets are assigned to their new pipe
at `0x245014`. Native shutdown/close invalidates pipe FD at `0x244ee4`, and
pipe destruction calls that same close path. Reconnect closes the old endpoint
before creating another. Observe the successful kernel return rather than
assuming the following native store happened already.

Native headers use direct `read` and **do not query credentials**. The first
compositor `SO_PEERCRED` occurs during payload reading, after import begin
`0x153fc0`. Therefore a strict core which requires the peer to be known at
that first begin would reject valid first imports. Begin provisionally with
the observed socket lifetime; attach successfully copied native credentials
for that same lifetime before creation/commit. An earlier credential can be
reused only while continuous connection ownership is already proved.

The payload helper asks `SOL_SOCKET`, `SO_PEERCRED`, expected size 12, then
stores the returned PID at native pipe `+168`. Preserve the actual public
result, length, PID/UID/GID and FD generation; do not manufacture credentials
from `getpid` or an expected process name. Native failure retains the old PID
field, so reading that field alone is insufficient.

Maintain bounded local descriptor generations for successful `socket`,
`accept`/`accept4` and `connect`, and invalidate on close/shutdown, disconnect,
duplication/replacement, observer loss, process exit or unsupported lifecycle.
Unknown inherited descriptors remain unavailable. Do not invoke new socket
queries, waits or allocations in callbacks. General `dup`, `dup2`, `dup3` and
duplication-capable `fcntl` operations need conservative invalidation even
though this particular library does not import `fcntl`. A duplicated plane FD
belongs to a separate DMA-BUF export route, not the main RPC connection.

## Cross-endpoint connection and namespace gate

The native pipe pointers and FD numbers differ across processes. Peer PID and
server pathname are not a unique endpoint pair: one process can establish
several connections, each with its own counter, and counters can reset.
Matching opcode/sequence/token values does not establish their common epoch.

A concrete independent way to establish the endpoint pair is a **kernel Unix
socket peer-inode relation**, obtained outside callbacks through reviewed
Unix-socket diagnostics. Tie each endpoint inode to its process start identity
and observed descriptor generation, then revalidate through its lifetime.
A `/proc/net/unix` entry without the peer relation is insufficient. The
retained inputs contain no such runtime relation, and this audit does not
implement or authorize acquiring it. A source-proven native shared connection
identifier would also suffice, but none is established by this audit.

Once an actual endpoint pair is proven, the exact client request, compositor
import invocation/client scalar, successful full-token reply, client matching
reply and subsequent native reference can form the carrier bridge for that
registry lifetime. This is stronger than equal numeric tokens. Native token
allocation namespace, resource generation, retirement and selected content
generation remain separate; neither the peer PID nor sequence is a namespace.

Client unreference sends **`0x228` / `0x229`** from `0x362b10`. An ACK is not
a renderer/GPU retirement barrier. The compositor can acknowledge even when
resource removal fails. Keep public image/view destruction generations and
native registry lifetime evidence explicit; never infer completed GPU usage
from this ACK or a disconnected client alone.

## Actual forwarding provider and next implementation gate

The newly retained actual libc verifies both `__write@@GLIBC_2.17` and
`write@@GLIBC_2.17` as `WEAK FUNC`, address `0xdfee0`, size 204. The former
missing-provider-export blocker is resolved by actual bytes, not a sysroot.
Actual distinct forwarding exports also exist for `__read`, `__close`,
`__connect` and `__fcntl`. Fresh live provider/mapping identity still has to
match before deployment.

`accept`, `getsockopt` and `dup` have no corresponding distinct public
`__accept`, `__getsockopt` or `__dup` export in this provider. Their wrappers
need a separately reviewed bootstrap/forwarding mechanism outside callbacks;
hot-path symbol lookup or replacing cancellation-aware libc calls with raw
syscalls is unsuitable. `__socket` is `GLIBC_PRIVATE`, not a stable public ABI.

Private `build/resource-carrier-audit/{validate.py,validation.json}` passes
**80 exact instruction/branch/relocation checks** and actual libc export
checks. No binaries, disassembly or packet/image capture is tracked. Native
process mapping, active entry assembly, actual endpoint pairing, accepted
receiver submission, source-frame/UV/GPU ownership and passthrough are all
unproven. Keep pixels unchanged and opaque until those independent gates pass.

## Focused follow-up: lifecycle without accept/getsockopt interposition

A narrower source-proven route avoids those missing forwarding aliases for
fresh native accepted pipes. Public `pthread_mutex_init` and destroy have
separately verified distinct provider aliases. Their caller context must be
interpreted correctly: the native platform init helper **tail-branches** to
public init after restoring its frame and saved registers.

| Public init entry | Compositor | Client library |
| --- | --- | --- |
| Actual incoming LR | `0x1722f4` | `0x244dbc` |
| Pipe constructor | `0x1722e0`, frame 32 | `0x244da8`, frame 32 |
| Init helper tail branch | `0x187f30` | `0x259c70` |
| Accepted-pipe constructor's saved LR | `0x172544` | `0x24500c` |
| Parent accept function frame | 192 bytes | 192 bytes |

At that exact public init entry, `FP = SP` is the **pipe constructor** frame,
and its saved FP must equal `FP + 32`. Validate its saved LR against the
accepted constructor caller above. Public arguments are `x0 = pipe`,
`x1 = pipe + 48`; live native `x19` is that pipe. Because the platform helper
already restored `x20`, live **`w20` is the actual accepted FD**, and `x23` is
the newly allocated pipe. The accepted-FD assignment has not happened yet:
both constructors still initialize the native FD to `-1` before returning;
their accept callers assign the captured FD afterwards at `0x17254c` and
`0x245014`.

Therefore successful public init establishes object birth plus a
**pending accepted-FD binding**, not an already active connection. Activate it
only at a later source-proven protected pipe boundary which observes that exact
FD and object generation. Reject negative or conflicting FD, wrong constructor
route, missing initialization success, intervening destruction/close, stale
generation, capacity loss or an unmatched assignment. An exception before the
assignment must leave this binding incomplete. This route observes the actual
native accept result retained in the active caller, without wrapping `accept`
or asking the kernel a new question in the callback.

For client pipes created outside this accepted route, init establishes only
**object birth**. A supported public `connect` at return PC `0x245f9c` can
establish its local connection generation: the exact native connect function
`0x245e88` has a 192-byte frame with `FP = SP`, retains the pipe in `x19`,
closes any previous FD, creates a new socket at `0x245f1c`, assigns that new FD
at `0x245f20`, then calls public connect. Require an observed object generation,
argument FD equal to the protected pipe's FD, original connect success and no
conflicting lifecycle event. Birth alone or a later arbitrary lock on an
inherited unknown pipe is not enough. Unsupported assignments, unobserved
duplication/replacement and direct external descriptor operations still
invalidate the evidence.

Pipe destruction also uses **two tail branches** after releasing its own frame:
compositor `0x172660 -> 0x185358 -> pthread_mutex_destroy`, and client
`0x245128 -> 0x257098 -> pthread_mutex_destroy`. Public destroy therefore
inherits the destructor's *caller* LR, not one universal pipe-destruction LR.
Conservatively invalidate a tracked object when its mutex address is destroyed;
do not guess a single saved return site. Public close invalidates every tracked
binding of that FD before another observed generation can reuse the number.

The minimum direct-alias public set for this narrow accepted-pipe diagnostic
is mutex init/destroy/lock/unlock, read/write and close, plus the already reviewed
Vulkan entries; the client connect route additionally uses its verified
`__connect` alias. This does not establish general Unix-socket lifecycle coverage
for unknown native/global assignment routes. Every accepted record must remain
within the exact supported object/connection routes and independently paired
kernel endpoint epoch.

### Copying native FD and PID under existing protection

At the compositor import's first unlock entry with LR `0x1540b0`, the actual
pipe remains locked. The source-proven pipe pointer in the native frame
`+128` can supply native FD **`+172`** and peer PID **`+168`** before the
original unlock is forwarded. Require successful original lock/unlock, true
native payload-read result, exact invocation and active object/connection
generation. Do not read either field later on an output worker.

For a **fresh accepted object**, the constructor's combined 64-bit store at
`+164` initializes sequence 1 and peer PID 0. The payload reader updates PID
only after successful native `SO_PEERCRED`. Thus a positive PID observed under
that first protected unlock is a credential fact acquired since this fresh
object's birth, avoiding a `getsockopt` wrapper. The native header reader still
does not provide that fact at begin. The analogous client import final unlock
with LR `0x3624c4` remains protected and can copy the same offsets from its
source-proven saved pipe.

**This inference cannot be generalized to reconnects.** Native close/connect
does not clear peer PID. A failed lookup on a reused object can leave its old
positive PID, so that value is not proof of fresh credentials for a new epoch.
Refuse that credential join unless a separately reviewed actual lookup result
or independently verified current kernel credential/endpoint relation supplies
it. Copying PID alone also does not provide UID/GID; do not claim those fields
were observed through this narrow route.

FD/PID are additional precisely scoped protected native scalars and need to be
listed in the future runtime permission request. This follow-up extends private
validation to **114 exact native checks**; it does not implement the adapter,
observe native object birth, or establish continuous socket lifetime on hardware.
