# Inert original-fragment observation

This is a private-input, offscreen diagnostic foundation for the Steam Link
explicit-mask investigation. It is not a runtime layer, mask presenter or Frame
installation. The PC Steam sender and GloomhavenVR Build677 remain unchanged.

## Why observe original values

The actual changing-source acquisition recovers one encoded app pair from both
source-eye labels. It does not prove which uniform bytes the original fragment
shader consumed. A post-draw uniform copy can describe different host writes.
The new transformer exports values already computed in the original fragment
invocation, without adding uniform reads or replacing the original computation.

`tools/native_fragment_ssa_probe.py` accepts one exact private original fragment:
6620 bytes, SHA256
`f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2`.
It checks the bounded instruction profile, types, definitions, control flow and
dominance before adding four outputs immediately before the original output
store. Original instruction bodies and output0 remain unchanged. The only
original instruction edit extends the entry-point output interface.

| Output | Owned attachment | Original value |
| --- | --- | --- |
| 0 | RGBA32 float | Unchanged native result |
| 1 | RGBA32 float | Input u.xy and selected patch.xy |
| 2 | RGBA32 float | Selected R0 rectangle |
| 3 | RGBA32 float | Selected R1 rectangle |
| 4 | RGBA32 unsigned integer | Tint RGB bit patterns and consumed raw flags |

The integer attachment preserves flags even when their bits encode float
subnormals or NaNs. The transformer adds no descriptors or UBO loads. Its CLI
runs `spirv-val` before publishing private output files and refuses unknown
originals, reapplication, symlinked paths and existing output files. No native
module, instruction body, source or disassembly is checked into this repository.

## Actual host GPU evidence

`tests/native-fragment-ssa-gpu/` supplies an original owned vertex shader, UBOs,
texture, sampler and five offscreen attachments. It executes both the actual
private original fragment and its transformed counterpart. This fixture uses
explicit asymmetric inputs; it does not reproduce Valve's native vertex stage
or claim native reprojection coordinates.

The initial Intel RPL-P and llvmpipe observations each contain 36 cases and 670
checks. ASan/UBSan also passed the initial llvmpipe subset with driver leak
detection disabled. These runs had no Khronos validation layer. Privately
extracting the official Ubuntu validation package exposed an undersized
sampled-image descriptor array in the fixture. Those initial observations are
not valid-Vulkan evidence; the failed validation record is preserved.

The corrected fixture declares all three required sampled-image descriptors;
they deliberately reference the same owned view, without claiming native source
index equivalence. Intel RPL-P and llvmpipe each now pass 36 cases and 671 checks
with Khronos validation enabled and zero errors. Original output0 is bit-identical.
Exported u, patch, R0, R1, tint and raw flags match the fixture's independent
inputs. Decoder/reconstruction flags and the selected-eye patch causally change
the original output. A valid corrupted probe exporting R1 into R0 is rejected on
both drivers after output0 equivalence passes, without validation errors.

A second defect concerned partial shader-module construction. An intentionally
missing third module reproduces two leaked modules in the old fixture. The
corrected fixture tears down cleanly under validation on both drivers. Four
expected warnings concern baseline attachments1..4, which the unmodified
original fragment does not write and the oracle does not read. Full messages are
retained. Synchronization validation is requested in the private layer settings.
No package is installed globally; its extracted files are removed after testing.
Host five-attachment support never establishes Frame support.

Six focused Python methods pass with the explicit private original, including
type/dominance mutations and exact reconstruction of the original instruction
stream. CI runs the three owned-only methods and skips the private-input class.
CMake compiles the inert host fixture and validates only the owned vertex shader;
it never acquires or downloads Valve modules.

For an explicit private GPU run:

```sh
python3 -B tests/native-fragment-ssa-gpu/run.py \
  --original /absolute/private/original.spv \
  --probe-tool /absolute/checkout/tools/native_fragment_ssa_probe.py \
  --record /absolute/private/new-result.json --driver both
```

The runner compiles only this fixture unless `--fixture` specifies its already
compiled executable. Generated native modules are temporary and removed on
every exit. Original hardware captures, immutable inputs and small evidence
records are retained; completed compiler caches and worker worktrees are removed.

## Remaining boundaries

This proves the observation transformation on actual host GPU execution only
within the explicitly recorded fixture and its validity checks. It does not
prove native draw coherence, source-frame association, physical-eye placement,
app-to-decoded mapping W or execution on the Frame. All live shader replacement
and alpha-presentation permissions remain closed. A fitted pixel map must never
be promoted into those missing runtime contracts.

Future in-band RGB and coverage samples both need their own atlas mapping
composed with the same native reprojection and packed reconstruction. The game
also needs a real coverage export: its current opaque clears and several
transparent/additive shader blend conventions make ordinary `source.a`
insufficient. Additive emission, colored transmission and refraction require
explicit composition rules. No part of this fixture enables that export or
claims working streamed passthrough.

Private evidence: `build/private-native-ssa-probe-20261016/`,
`build/private-native-ssa-gpu-20261016/`,
`build/private-native-ssa-validation-20261016/` and
`build/private-native-ssa-integration-20261016/`. The independent changing-source
recovery is recorded in `docs/CHANGING-SOURCE-REGISTRATION-20261016.md`.
