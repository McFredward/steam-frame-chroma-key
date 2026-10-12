# SteamOS 0.4.5: compositor import and success-boundary audit

## Result

The updated compositor still has a synchronous DMA-BUF import route, but its
addresses changed non-uniformly. Its native resource token is **not** a Vulkan
image, an image output pointer, or the next word beside that output pointer.
Reusing the earlier compositor offsets or comparing equal file descriptors
would produce an invalid association.

A narrower observation route exists without a private executable detour:
observe an import-specific native message invocation, its nested public Vulkan
creation operations, and the exact successful native reply sent through public
`write`. The public pipe mutex, response allocation/copy and write boundaries
are identifiable for this build. They can establish a **candidate successful
import-to-image association**, provided their invocation, client and resource
lifetime checks pass. They do not yet establish a complete selected-video draw
or activate passthrough.

This is an offline audit of already retained private references. No Frame
connection, deployment, process-memory read, new runtime capture, executable
patch or rendering change was performed. The existing permission covers the
receiver's six protected scalars; the compositor/IPC observations proposed here
require a separate scope and an independently reviewed implementation.

## Exact inputs and evidence

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Updated `vrcompositor` | 6,026,096 | `0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96` |
| Updated `vrlink` | 2,425,024 | `31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153` |
| Authorized receiver capture | 32 selections | `7b2bfffae81c0e326b1a13e2e4fbd74976a46b9b389a0a0cb8d8053fc2c15c07` |

The compositor ELF build ID is
`f608305627cd1579a5bbd714d4209922c892da0a`; the receiver build ID is
`bfe671fed2382cad717d2994fa3a62e5e99fb859`. All addresses below are ELF virtual
addresses for these exact files, not a portable API or permission to hook them.

The private audit checks **116 instruction/branch/relocation/string points** and
eight versioned public procedure imports. It also independently checks the
32-record receiver capture: all records are valid protected selection attempts;
there are 32 distinct keys/content generations and 12 resource tokens, with
every token reused for later content. The capture explicitly denies accepted
submission and source-frame association. Allocation/token lifetime is therefore
insufficient to identify the video content.

Private validation source/results remain in ignored
`build/import045-audit/{validate.py,validation.json}` in the audit worktree.
No Valve binary, disassembly, shader listing or image content is tracked.

## Current native import route

| Boundary | Verified role |
| --- | --- |
| Receiver `0x34604` | Calls the resource-manager import slot with output at capture-record `+0x20`; its subsequent reference uses that same token. The selected presentation helper reads it at `0x34c24` and copies it to both eye structures. |
| Message dispatcher `0x153f70` | The RTTI/vtable-specific DMA-BUF import handler. Its registered request opcode is `0x234`, response opcode `0x235`, and native method is `0x13bd78`. |
| Native request handler `0x13bd78` | Receives the plane handles, supplies the actual client-context identifier from `context + 8`, and calls the import helper. Only helper success sets the response token-presence bit and stores the token at response `+0x30`. |
| Import helper `0x13bb80` | Clears its output before validation, synchronously creates the native texture, registers the resource, and publishes the token only after successful registration. Registration failure destroys the created texture and leaves output zero. |
| Renderer wrapper `0x70ad0` | Calls the concrete creation slot `208` synchronously, then queries a separate resource-token slot `320`. |
| Concrete Vulkan creation `0x2c71f0` | RTTI/vtable-resolved `CVulkanVRRenderer` method. Its DMA-BUF branch calls `vkCreateImage`, imported-memory allocation, `vkBindImageMemory`, and `vkCreateImageView`, each with native failure branches. |

The old import helper `0x137dd0` is now `0x13bb80`; the old renderer wrapper
`0x6e6c8` is now `0x70ad0`. These deltas differ. Applying one global relocation
delta to the old adapter is invalid.

The fresh API-name loader and GOT relocations were checked. Relevant public
Vulkan calls are at `0x2c75a8` (image), `0x2c7680` (image-memory binding), and
`0x2c7800` (image view). The memory allocator `0x2be430` duplicates a plane FD
and submits DMA-BUF import metadata with the dedicated image to
`vkAllocateMemory`. These are resource operations, not proof of the native
registration's eventual success or a video's source session/frame.

## Token and object layouts: the tempting shortcut is wrong

At the image-creation call, output pointer `P` is the native texture object's
`+0x50` image field. `P + 0x10` is that texture's `+0x60` **view storage**, which
is explicitly cleared later at `0x2c7860`. It is not the shared token.

The actual getter `0x2bc1e8` invokes a native texture accessor, follows a separate
shared GPU resource through the texture's `+0xd0`, obtains its virtual-base
adjustment, and reads the adjusted shared resource's `+0x60`. The implementation
at `0x2ab7c8` exposes that shared-resource field. Those identically numbered
offsets belong to different objects; reading a guessed `P + 0x10` would report
the wrong identity.

The separate shared resource's export initializer `0x2b40a0` constructs its
64-bit token from a native counter shifted by 32 and a 32-bit native namespace
at `0x2b40cc`, stores it at `0x2b40d0`, then initializes its IPC shared state.
The namespace helper `0x186518` uses an explicit native override when active,
otherwise native `getpid`. Its setter and namespace-server request/reply routes
exist; their live state was not read. A PID alone is therefore not a reliable
substitute for the namespace, and the token counter is not a video-frame ID.

Use the full successful reply token. Keep compositor-instance generation,
connection/client identity, token/resource lifetime and selected content
generation separate. Do not reconstruct a namespace from dimensions, arrival
time, a raw FD or a guessed token bit pattern.

## Why a receiver-style mutex-only observer is insufficient

Native registration at `0x10b950` uses the custom recursive/read-write lock
helpers `0x1880b8` and `0x188160`. Their normal paths use native atomics; a
registration can succeed without invoking public `pthread_mutex_lock` or
`pthread_mutex_unlock`. A public pthread caller-site recorder cannot observe
every registration merely by changing the receiver's known offsets.

Likewise, `VK_SUCCESS` at image creation precedes native registration. It is
possible to create and bind an image, then fail resource registration and
destroy it. A public Vulkan creation record must remain provisional until the
exact native success reply for the same invocation is established.

## Narrower public observation boundaries

The following are **future hook candidates**, statically verified here; none
was implemented or loaded by this audit.

| Public boundary / return address | Useful evidence and required gate |
| --- | --- |
| `pthread_mutex_lock`, `0x153fc0` | Import-specific message invocation begins under its actual pipe mutex. The request sequence is already stored in the native handler frame at `+108`. Require real lock success and the exact pinned native caller; allocate only an observer invocation ID, never a video-frame ID. |
| Nested `vkCreateImage`, `0x2c75ac` | Exact image generation, format/extent/modifier and output identity for this synchronous invocation. Verify the complete pinned creation-wrapper/import-handler stack route and active invocation before accepting the candidate. Reject unrelated/nested/ambiguous creation. |
| `malloc`, `0x154288` | Reached only after the selected native handler returns success and a response is configured. This identifies the response construction phase; it does not yet prove that bytes reached the pipe. |
| `memcpy`, `0x172ee8` | Copies the completed serialized response into the outgoing packet while the native pipe mutex is held. Its header is three 32-bit fields: opcode, exact request sequence, payload length. A present import token is a nine-byte protobuf fixed64 field; reject any unreviewed payload shape or extra fields. |
| `write`, `0x172c00` | Sends the exact packet span through the actual pipe FD. The native loop advances buffer and remaining length after partial writes. Commit only after every byte of this same response has a positive successful write result, with no close/reconnect, conflict or observer loss. |
| `pthread_mutex_unlock`, `0x172fbc` | The native send-loop result is retained before unlock. This can confirm the send's completion/closure when the exact parent import-response caller is verified; it is not evidence that the client accepted or presented the image. |

The import-specific parent caller of the send helper is `0x1542ac` (return
`0x1542b0`). Its response buffer remains owned until the synchronous send returns
and is then freed. In the normal fixed64-only response, the complete outgoing
packet is 21 bytes: 12-byte header plus nine-byte payload. The native parser,
constructor, token-presence branch, serializer and byte-size route were checked.
Do not infer token validity from a nonzero number in arbitrary `write` data.

The main resource request/reply channel here uses public **`read` / `write`**.
`recvmsg` / `sendmsg` and the plane-transfer FD helper are separate routes; their
FD integers do not identify the successful import response.

The native read helper calls `getsockopt(SOL_SOCKET, SO_PEERCRED)` before reading
payloads and records the peer PID. Its `read` return address is `0x172998`.
The pipe's close path calls public `shutdown` and `close`, then writes FD `-1`.
An observer can preserve actual peer-credential and descriptor-lifetime evidence
without adding syscalls on the rendering path. An FD number without its observed
socket/close generation is not an association.

The public callback plan still requires carefully bounded inspection of active
native **stack/client scalars** to validate call nesting, the request sequence
and the actual client context. During nested creation, the import handler retains
the typed client context with native shared references. Those references are
released before reply serialization; reading the same private pointer later on
a worker is not a protected-lifetime shortcut. Capture necessary scalars while
their exact active ownership is established, or leave the join unavailable.

The extra inspection must use pinned frame layouts, preserved registers and
pre-established thread stack bounds, with an assembly-entry contract or another
reviewed way to preserve native context. A generic C++ backtrace or a later heap
scan does not establish this proof. Unknown stack route, missing success,
unsupported process/library, changed tuple, unexpected nesting or capacity loss
rejects the record.

## Retirement and namespace limits

Resource registration indexes the full token and keeps a native resource entry
with client reference counts. Unref request/response opcodes are `0x228` /
`0x229`; its handler is `0x13b7d0`. The native unref routine `0x10b678` decrements
counts under an entry mutex and only attempts tree removal when the aggregate
count reaches zero. Its direct public pre-unlock return address is `0x10b7c8`.

The unref message handler returns a successful message ACK even when the
underlying removal fails. **An unref ACK is not a retirement barrier.** A public
Vulkan destroy event proves retirement of that local image generation, but
neither it nor an unref request proves that every submitted draw has completed.
Native reference/lifetime transitions, exact image/view destruction and GPU
fence ownership must remain explicit independent requirements.

The current receiver evidence proves selected record → token, and the fresh
static import path proves reply token → native registration result. It does not
prove that two equal token values from independently sampled processes share the
same live native namespace. A complete cross-process bridge must observe the
exact request/response carrier and connection epoch on both sides, including
the receiver's actual resource-manager library. That library's current hash,
caller sites, reply validation and invalidation paths are not among this audit's
inputs. Do not silently inherit its ABI from an older build or declare equal
reply/selection numbers to be a complete namespace join.

## Concrete next scope

Prepare a compositor-only bounded diagnostic offline using the verified public
boundaries above. Record at most 32 complete import attempts, with a fixed
invocation/socket/resource table, a 1 MiB output limit and a recording deadline
that stops observation only. Keep raw packet data out of output; copy only the
reviewed opcode/sequence/token and required client/ownership scalars. Reuse the
existing asynchronous public Vulkan resource journal for image, allocation,
view and destruction generations. Do not allocate, perform I/O, wait, acquire
new locks or inspect arbitrary memory inside native/render callbacks.

Test native success/failure, partial writes, zero/error returns, lost records,
socket close/FD reuse, foreign peers, repeated token/content, duplicate/reordered
sequences, multiple threads, nested imports, frame reuse, missing image/view and
unsupported payloads. Incomplete records must retain opaque output. Native
return values, `errno`, locking and dispatch behavior must remain unchanged.

Before any deployment, obtain separate permission for the compositor preload,
the precise active-stack/client scalar scope and public IPC metadata. Preserve
the existing owned user-directory manifest, native files, readiness-controlled
manual restarts and complete rollback verification. This proposal does not
authorize installing a private probe, reading mapped uniforms, GPU copies,
descriptor/shader replacement or mask activation.

After that diagnostic establishes real successful import-to-image records,
the receiver-side namespace/library bridge and retirement checks still need
their own review. Current-build sampled descriptor/view evidence, coherent
physical-eye selectors and UV/upload ownership must also be refreshed. The PC
composition → encoder → transmitter key/session bridge remains separate.
The earlier complete public journal is historical evidence; it is not a fresh
0.4.5 compositor capture and its GPU/UV claims cannot be inherited by address.
