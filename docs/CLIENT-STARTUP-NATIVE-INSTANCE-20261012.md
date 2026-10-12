# Distinguishing the first native pipe from the target instance

The first authentic Frame startup capture reached the prepared MAIN observer,
the actual Steam-runtime API, the deep-bound client-to-libc mutex entry and the
independently validated client mapping. It then refused `Singleton` before
copying private constructor stack words. The target address was correct;
the candidate filter had admitted an earlier, different embedded pipe and
consumed the one permitted discovery attempt.

This audit independently decodes the retained ARM64 client ELF with SHA-256
`19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0`.
Addresses below are ELF-relative. Native bytes, disassemblies, the capture and
validation program remain private in ignored `build/` directories. No native
binary execution, device access, installation or production edits were part of
this source review.

## Two objects share the same public mutex return site

The global initializer at `0xa8e20` derives the HMD singleton from the actual
ADRP page `0x647000` and ADD immediate `0xd78`. It passes **`0x647d78`** to
the HMD constructor at `0x1b9438` through the call at `0xa924c`.

| Native path | Object arithmetic | Resulting pipe |
| --- | --- | --- |
| Earlier HMD subobject: call `0x1b94ec` → constructor `0x3ac5a8` → call `0x3ac5d8` → pipe constructor `0x244da8` | HMD `+0xfe0`, then embedded pipe `+0x48` | **`0x648da0`** |
| Intended resource-manager pipe: call `0x1b9578` → pipe constructor `0x244da8` | HMD `+0x1158`, then `+0x1120`, totaling `+0x2278` | **`0x649ff0`** |

Both pipe instances enter platform initialization through `0x244db8`. That
helper restores its own frame and tail-forwards to public `pthread_mutex_init`.
Consequently both have incoming native LR **`0x244dbc`**, x0 = x19 = their
pipe address, and x1 = pipe +48. A common return site, 16-byte alignment and
matching argument/register shape do not identify the intended instance.

The captured first entry normalizes to x0 = x19 = `0x648da0`, x1 = `0x648dd0`
and LR = `0x244dbc`. Its x20 and x21 additionally match the earlier wrapper's
subobject fields at `0x648d68` and `0x648d88`. These values agree with the source
arithmetic without changing any expected address to fit the capture.

## Constructor frames and ordering

The ELF instructions and native unwind information independently establish
frame sizes of 128 bytes for the global initializer, 384 for the HMD constructor,
48 for the earlier wrapper, and 32 for the pipe constructor. At the public mutex
entry, the platform helper's frame has already been removed.

| Relationship relative to mutex-entry SP/FP | Earlier embedded pipe | Intended pipe |
| --- | --- | --- |
| Pipe's saved parent FP | SP +32 | SP +32 |
| Pipe's saved parent LR | `0x3ac5dc` | `0x1b957c` |
| Pipe's saved inherited x19 | `0x648d58` | `0x647d78` |
| HMD frame | SP +80 | SP +32 |
| Global initializer frame | SP +464 | SP +416 |
| Known full frame span | 592 bytes | 544 bytes |

The earlier frame relationships are **source-derived**, not observed private
stack copies: the original absolute singleton check refused before those five
words were acquired. They must not be reported as successful hardware evidence.

The client has 104 relocated `.init_array` entries. HMD initialization is
zero-based entry 37 at slot `0x607b30`. A conservative direct B/BL graph across
23,228 native unwind function ranges finds no path from the preceding 37
initializer roots to the pipe constructor. The HMD body calls the earlier
embedded constructor before the intended resource-manager pipe. Earlier direct
platform-mutex calls preserve other return sites and therefore fail the existing
LR prefilter.

The complete executable-image direct-call census finds ten B/BL references to
pipe constructor `0x244da8`:

| Sites | Source category |
| --- | --- |
| `0x1b9578`, `0x3ac5d8` | The two HMD embedded instances above |
| `0x245008` | Heap object after successful socket acceptance |
| `0x253934` | Heap subobject at allocation +72 |
| `0x253a38`, `0x253e00`, `0x254138`, `0x2544b8`, `0x2546c8` | Temporary stack instances |
| `0x361e84` | Tail-forwarded wrapper subobject +24 |

This direct graph is not a proof that arbitrary indirect or external callbacks
can never construct another pipe. A heap or stack instance can coincidentally
share a page offset. Unsupported cases must still fail closed; the cheap
register filter must never stand in for mapping or constructor identity.

## Bounded repair and native behavior

Because the validated client bias is 4096-byte aligned, the intended pipe's
page offset is `0xff0`; the earlier instance's offset is `0xda0`. Rejecting
non-`0xff0` candidates using registers alone avoids discovering, reading or
retiring on this supported earlier instance. The full absolute pipe address,
actual code hash, public module mapping, audit lifetime, current stack bounds
and exact parent-frame checks remain mandatory. The intended constants remain
`HMD = 0x647d78` and `pipe = 0x649ff0`. Replacing them with the observed earlier
address would authenticate the wrong object.

The pipe constructor ignores the platform-init result and continues its native
state initialization. Request-sequence/peer/FD stores follow the public init
entry; they are not available as before-call facts. Observer rejection must
still forward the unchanged native init and preserve its result/errno. Successful
evidence requires the target's actual native success; a failed mutex init,
failed public VR initialization, unwind or missing target retires evidence.
None of these local facts proves a connected token namespace, video-frame
association, reprojection UV mapping or GPU-resource ownership.

## Why earlier checks missed the failure

The earlier static review proved that the intended constructor path existed,
but did not establish that it was the first matching public mutex entry. The
model/host fixtures supplied the target directly and often reused production
constants to construct their positive input. They verified gate safety after
target selection, without exercising the real earlier same-LR object followed
by the target. ABI, loader and audit-binding tests exercised independent layers;
their passing results did not establish constructor-instance selection.

The private independent audit now passes **104** native instruction, address,
ordering, relocation, frame, direct-caller and capture-consistency checks.
Its small receipts are `source-validation.json` and
`direct-initializer-paths.json` under the review worktree's ignored
`build/private-native-instance-review/`. The focused implementation regression
must exercise the earlier instance followed by the unchanged target, plus a
wrong absolute instance with the same `0xff0` offset. A scoped future Frame
capture is still required to demonstrate a successful native target birth.
