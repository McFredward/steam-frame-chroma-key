# Next native association adapter

## Decision

The next useful experiment is a **selected-record/resource scalar observer**, not
another descriptor-only capture. It must join the receiver's protected selected
`(native key, content generation, shared token)` tuple to the compositor's
successful resource-import result and then to the already observable Vulkan
image/view generation. Observe the native success and selection boundaries;
do not match equal FDs, dimensions, presentation times or event order.

The [compact native run](FRAME-COMPACT-JOURNAL-RUN-20261011.md) retained all
703,494 lifecycle records, with no replay/publication losses, and produced 32
source-complete binding snapshots. This removes the diagnostic overflow that
blocked earlier resource evidence. It does **not** expose the received native
key, source session or GPU-consumed selector values. Repeating that capture alone
cannot supply those missing columns.

This report specifies future instrumentation. No adapter was deployed, native
executable changed, process attached, mapped memory read or shader activated.
The Frame was not accessed during this audit.

## Newly narrowed boundaries

Addresses below apply only to the exact binaries identified in
[the resource report](FRAME-RESOURCE-ASSOCIATION-20261011.md). They are analysis
locations, not a portable hook ABI.

| Boundary | Exact observation to prepare | Lifetime requirement |
| --- | --- | --- |
| Receiver selected helper `0x33e60` | Its `x1` is the selected capture record. Copy the received key at `+0x28`, content generation at `+0x30`, and shared token at `+0x20`, with receiver/resource namespaces. | The caller acquires that record's `+0x38` mutex at `0x34a4c` and keeps it across the helper until `0x34d70`. Check the active lock-success branch; do not read the reusable record later on a worker. |
| Receiver successful metadata lookup `0x5202c` | After validity and full-key equality succeed, copy the required fields from the metadata record while its native mutex is still held. The block is `0x154` bytes; initially only the first metadata word and the selection-relevant fields are needed. | The lookup unlocks at `0x52038`, then returns its internal pointer. Capturing from that pointer after return is not an immutable snapshot. Copy while protected and track metadata invalidation/reinitialization. |
| Compositor resource import helper `0x137dd0` | On its successful exit, copy the returned token from its output argument, plus the validated client/resource namespace and import invocation context. Link the existing nested Vulkan image creation/binding/view events to that invocation. | Output is initially zero. Only after creation and successful resource registration does `0x137e8c` write the token; `0x13811c` places that same value in the IPC response. An attempted import or a nonzero intermediate pointer is insufficient. |

The metadata lookup must be associated with the selected presentation call site
`0x34aa4`, rather than arbitrary earlier decode/lookups. The helper's `x3` is
**stack-built pose data**, not the original `0x154`-byte metadata pointer. Reading
`x3` as that metadata would be a wrong ABI and could produce false frame matches.

Observe local call nesting with an observer-owned invocation identifier. It is
only a diagnostic join key between entry/exit and nested public API calls; it
must never become a video-frame identity. Publish the import observation only
after the native function reports success. Require exactly one proven image
creation route within its matching synchronous renderer call. Ambiguous nested
creation, asynchronous work or an unverified client/token namespace rejects the
join instead of selecting the newest or best-looking resource.

The resource helper routes through the renderer wrapper `0x6e6c8`, which calls
the concrete texture-creation slot synchronously, obtains its native resource
token through a separate renderer method, and then registers that result. This
provides a concrete entry/exit boundary for the token-to-image association. It
does not establish a general private interface signature, or prove that the
receiver and server share a namespace merely because their token values match.

## Keep identity domains separate

The minimum observation records are:

```text
Selected:
  receiver instance, native resource namespace, selected capture generation,
  received 16-bit key, selected content generation, selected shared token,
  copied metadata revision, observed native reset/retirement state

Imported:
  compositor instance, native client/resource namespace, import token,
  successful import invocation, Vulkan device/image/memory/view generations,
  resource creation and retirement state

Draw:
  accepted command generation, descriptor revision, image-view generation,
  effective selector/metadata-buffer ranges, later coherent eye/UV proof
```

The actual PC source session and full source-frame identity remain separate
required inputs. The native first 32-bit presentation field can be copied as
metadata; it is not yet a unique stream/session identity or the transmitter's
key. A useful sender sidechannel must carry its **observed** composition → encoder
→ transmitter-key association and its explicit session/reuse generation.
Require an explicit authenticated session/carrier match to the receiver before
resolving masks. Do not recover epoch/wrap from packet arrival order or infer a
64-bit frame ID by extending a receive-local counter.

Reusing one image with a new decoded content generation invalidates the previous
content association even when its view, shared token and allocation stay alive.
Preserve separate resource-lifetime and decoded-content generations. Use the
existing bounded [exact association ledger](EXACT-FRAME-ASSOCIATION.md) only with
proved columns; a locally invented namespace does not prove a native namespace.

## Eye and rectangle evidence is now more specific

All 32 fresh snapshots have these exact aliases, checked by handle **and**
generation, effective range, bound allocation and memory offset:

- Vertex binding 0 and fragment binding 4 use the same buffer and same 272-byte
  slice for each draw. The two observed effective offsets are 0 and 640, each
  appearing in 16 snapshots. Slice number does not identify a physical eye.
- Vertex binding 2 and fragment binding 6 use the same 2,800-byte buffer at the
  same effective offset 0, with the same bound allocation. The earlier possible
  explanation that the fragment descriptor was offset to a selected vertex
  record does not apply to these samples.
- Binding 3's 400-byte record has an effective offset of 0. Its contents were
  not read.
- The sampled color image is a DMA-BUF import with the known explicit modifier,
  two memory planes and 1152 × 4608 extent. That is an imported resource layout,
  not proof of two eye halves or of unfoveated source-mask coordinates.

Using [the declaration-derived offsets](FRAME-UV-BINDING-20261011.md), the next
coherent scalar observation needs:

1. Eye selector at `effective offset + 256` in bindings 0/4 and its exact
   physical-left/right route. The alias reduces duplicate reads; it does not
   prove what either slice contains or which GPU draw consumed it.
2. Selected vertex record `K` from binding 2's selector and binding 3's fallback
   and consistency fields, for this accepted command. Do not assume `K == 0`.
3. Vertex `R1` at matrix base `432 + 928*K + 448*E`, and fragment `R1` at
   `432 + 448*E`. Read matrix-column offsets 4, 20, 36 and 52. Because the
   descriptors alias at offset 0, equality follows from the same record only
   when `K == 0`; otherwise explicit rectangle/record coherence must be proved.
4. A native ownership/upload revision showing that these values and source
   association remain unchanged through the GPU draw. Range equality, a later
   CPU memory read or host-coherent memory alone does not prove this.

Prepare a bounded observer of the native immutable upload/reservation boundary
or CPU-supplied Vulkan upload data if that is the verified live route. Copy only
the fields above with buffer/command revisions. Do not add an arbitrary mapped
memory read at `vkQueueSubmit`: the current capture does not establish ownership
or protection against concurrent writes, and no native mapping pointer or memory
property has been established by memory type number alone. If the required
protected upload boundary cannot be verified, leave UV proof unavailable.

## Concrete next experiment and authorization boundary

Implement the fixed-capacity scalar recorder and offline fixtures first. Initial
capture limits can remain 32 selected complete records, a 1 MiB combined output
cap and a 120-second observation deadline. Reuse the existing asynchronous
journal for publication; the deadline stops recording only and must not restart
SteamVR. No allocation, I/O, blocking IPC or new lock acquisition belongs in a
native/render callback.

Before installation, independently verify complete ELF hashes/build IDs, the
exact inspected instruction windows, actual calling convention, client/resource
namespace, protected-read scope, failure exits and retirement/reset callbacks.
Unsupported build, wrong argument shape, unexpected nesting, reused generation,
partial lookup, lost record or unknown fence makes the record incomplete. Never
continue with an inferred signature or reset loss counters to manufacture proof.

The existing public Vulkan layer cannot invoke these private boundaries by
itself. A public `VR_GetGenericInterface` preload wrapper alone also cannot expose
the selected helper's key/content generation. Private-function observation is
required for the proposed receiver tuple unless a supported native diagnostic
supplies it. The instrumentation mechanism must be separately reviewed: a
version-pinned function probe that copies selected scalar arguments/records is
different from today's public API metadata observation, even if it leaves output
unchanged. Kernel probes can also alter executable instructions temporarily;
they are not automatically covered by a permission for read-only attachment.

The following operations **exceed the currently approved passive scope** and
need explicit agreement before runtime use:

- Installing/attaching private receiver or compositor function probes, introducing
  a `vrlink` preload/interface interceptor, or changing its launch environment.
- Copying private native metadata records or mapped uniform contents, including
  small selector/rectangle reads. Permission to record public Vulkan call scalars
  does not authorize arbitrary process-memory reads.
- Instrumenting private IPC resource registration/release or PC encoder/TX
  callbacks. Existing static findings do not authorize executable detours.
- GPU copies, shader/descriptor replacement, image readback or mask activation.
  Those remain a later independent review, after exact association and lifetime
  evidence.

A deployment must stay in an owned user-directory session with an exact manifest,
original launch/service settings recorded, no system-image changes and no native
binary overwrite. Install/uninstall must be idempotent, reject active or foreign
sessions, verify hashes before removing files, and confirm the restored processes
have no diagnostic mapping/environment. SteamVR restarts require the user's
readiness and occur only when explicitly requested; no timed rollback restart.

## Acceptance and audit evidence

Accept the scalar experiment only when repeated selection retains the same
content tuple; content replacement changes its generation; two physical eyes
join the same selected video content with their own proved selectors; import
success and retirement preserve the exact native namespace; and loss/reset/
reconfiguration reject unresolved mappings. Stress duplicate keys, reorder,
restart, 16-bit reuse, descriptor mutation and delayed metadata. Require explicit
retirement/fence evidence rather than treating display order as retirement.

This audit performed 33 focused instruction-word checks against the pinned
private executables and 32 full snapshot alias checks against capture SHA-256
`ad68d34b2a3aa7c2b8e56c0947cd82bc4d134b1cd2f937f5213547dfd934f413`.
Small validation results remain private; no proprietary instruction listings,
image content or binaries are tracked. These are static and captured-public-
metadata checks, not an installed adapter or a functioning passthrough test.
