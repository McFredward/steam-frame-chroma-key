# Compositor resource token namespace audit (2026-10-13)

The current native registry is keyed by the **full 64-bit token across one
resource-manager instance**, not by a pipe, FD or client ID. Successful
registration rejects another live value at the same key. That supports a
possible registry-based **local token-to-resource** association. It does not
establish permanent token uniqueness for a compositor process or boot, and the
existing raw records must not be promoted into strict carrier proof.

This is a source audit of the privately retained current `vrcompositor`, SHA-256
`0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96`
(ELF build ID `f608305627cd1579a5bbd714d4209922c892da0a`). No hardware
capture, native file modification, shader activation or sender change was made.
The private receipt records exact instruction/relocation checks. Native binary
bytes and disassembly remain private.

## Allocation and registry identity

| Current native boundary | Source finding | Consequence |
| --- | --- | --- |
| Registration `0x10b950` | Takes token in `x2`, client ID in `w1`, and resource/backend pointers separately. `0x10a718` rejects zero token or missing required resource pointers, but does not reject client zero. | Registration does not itself allocate a token. Client zero is a real available value. |
| Registry search `0x10b9b4`–`0x10ba18` | Searches manager tree at `+0x268`; node key at `+32` is compared as an unsigned 64-bit value against the full token. | The key is neither a truncated handle nor a `(client, token)` tuple. The same manager is shared by its clients. |
| Duplicate guard `0x10ba18`–`0x10ba30` | A non-null existing value at node `+40` leads to registration failure. Publication at `0x10bb94` fills the selected key's value. | One live registry value per full token is enforced under the registry lock. This is current-entry uniqueness, not lifetime-wide uniqueness. |
| Producer route `0x13bb80`–`0x13bc3c` | Creates the DMA-BUF-backed resource, obtains its backend token, registers it, and only publishes the token to its output after registration succeeds. | A successful nonzero reply needs the actual native producer/registration transaction, not a guessed token or a recent image. |
| Vulkan backend vtable | Relocations resolve create slot `0xd0` to `0x2c71f0` and token-export slot `0x140` to `0x2bc1e8`. The latter delegates to the underlying shared object; the ordinary getter at `0x2ab7c8` returns its full token. | Exact source routing can relate a producer invocation to its exported token; the export is not a video-frame identifier. |
| One native creator `0x2b40a0` | Stores `(counter << 32) OR creator32` in object `+96`. The 64-bit counter at RVA `0x5bf780` starts at 1 and is incremented with ordinary load/add/store instructions. | Only the low 32 counter bits survive the shift. Wrap and caller serialization cannot be dismissed. |
| Creator helper `0x186518` / override `0x186538` | Normally uses `getpid`; an enabled override supplies another 32-bit creator identity. | The token's low word is not independently sufficient proof of the compositor's PID or process lifetime. |
| Alternate setter `0x2b4168`, used by `0x2b5de0` | Stores a supplied 64-bit token unchanged before opening that resource path. | Not every token originates in the local counter allocator. The counter alone is not a universal namespace certificate. |

The direct counter-reference scan found the allocator's load/store pair and no
other direct reference within its bounded source-address model. It does **not**
prove the absence of indirect writes, external producer tokens or all creation
interleavings. Permanent process-wide uniqueness is consequently unproven even
before considering process restart and PID reuse.

## Reference, retirement and reset

Lookup `0x10af18` accepts a full token and output pointer, searches the same
manager tree and exports the found resource. It does not take a pipe/client
key for that search. Per-client reference counts are lifetime bookkeeping, not
a source-proven authentication or frame-ownership check.

Reference `0x10b248` first finds the full-token entry in the same manager tree.
It then changes the resource's total count and its separate 32-bit client-keyed
reference map. The initial reference can be consumed without incrementing when
the reference client matches the creator; zero participates in that comparison
and map just like other available client IDs. There is no source rule making
zero an absent client. Unreference `0x10b678` updates those counts and, when the
total is no longer positive, reaches token removal `0x10bf30`.

Client teardown at `0x13df8c` invokes `0x10ab58`: it removes that client's
references across the manager and can erase resources that lose their last
reference. Therefore reconnecting a client does not give existing token
associations an automatic new lifetime. Other clients' retained references can
also keep entries alive. Native reference counts are not GPU submission or
completion fences.

Clear `0x10aae0` erases the manager's entire token tree under its lock; the
shutdown route includes a call at `0x13bfd4`. Construction `0x10a4b8` initializes
an empty tree. Those are registry-incarnation boundaries even if the process
remains alive or the same address is reused. Removal, clear, manager destruction,
replacement, observer loss and process exit must invalidate association evidence.
A later registration at an expired key is a **new generation**. Replaying an old
client reply against it is invalid.

## Possible narrower association route

A source-authenticated current entry can make all descriptor-duplication
lifetimes unnecessary **for this local registry join**, because its full-token
key does not depend on which pipe transported it. The following evidence would
still be required together:

- The exact current compositor process identity and the native manager's
  observed incarnation, rather than PID, executable hash or boot alone.
- A successful observed native producer/registration and reply, tied to the
  copied native invocation/client context and the public Vulkan image, memory
  and view results for that invocation.
- A current registry-entry generation, with complete relevant lifecycle
  observation or a reviewed native lifetime/reference mechanism that keeps that
  same entry alive. A copied pointer or an observer-created ordinal is not such
  a generation or lease.
- The actual client's accepted full-token reply and selected live native record,
  plus authenticated contemporaneous native-peer/process identity establishing
  that this is the same compositor registry. A cached peer PID, token low word,
  socket pathname, approximate timestamp or earlier socket snapshot is
  insufficient.
- The owning public resource journal's validated current image/memory/view
  generation receipt, checked again before publication and use. A native token
  does not replace public GPU resource generations or destruction evidence.

This is a candidate additional proof route with a distinct contract. It is **not**
a relaxation of `frame_import_pipe::Lease`, the strict endpoint carrier or raw
observer requirements. A client receiving a token and a different registry
later containing that number remains a negative case. The same applies after
counter wrap, explicit-token reuse, registry clear/reconstruction, disconnected
owner retirement, stale journal generations, unobserved mutation or ambiguous
peer identity.

The successful client raw capture establishes only the local native
request/reply/pending/reference hand-off. It was already present before the
operator confirmed the VR stream; it cannot identify a selected PC video image.
Even a future valid local registry join would leave source session/frame/eye,
accepted presentation, reprojection/UV mapping and coherent GPU consumption as
separate outstanding proofs. Keep the explicit-mask path opaque until those
conditions are established.
