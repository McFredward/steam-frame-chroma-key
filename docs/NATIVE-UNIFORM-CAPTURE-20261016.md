# Final-draw uniform capture, 2026-10-16

The approved optional diagnostic from source
`446027eba3834e24a69099665fec9a6243802503` completed one capture,
`e5138e4b1410ff2f888365cbe66659e5`, while GloomhavenVR676 displayed its
unchanged embedded static source pattern. The maintainer confirmed the pattern
in both eyes. No native shader, PC Steam sender or live alpha output changed.

## Recorded evidence

The exact winning native direct draw is ordinal4, command generation48660,
image generation2790564 and view generation2790566. Publication waited for the
owned completion fence and retirement of that exact reusable command generation.
These guards are producer assertions; the offline reader does not independently
reconstruct native execution from a journal.

| File | Bytes | SHA256 |
|---|---:|---|
| `snapshot.p010` | 15,925,248 | `a43ab64fc43612e40cccdd2127311aa9de6936edbd6eff01a4697c1ff317ac41` |
| `sampler-components.rgba8` | 21,233,664 | `9383ee2d8748f65b21141a4fea57e753ba0b6b8aae8d516839f5baf8761a6b6c` |
| `snapshot.json` | 6,357 | `95edb3b0f24085c8c4f247bad1e17458751bf60169863a240db2c0a86b630f13` |
| `uniform-bytes.bin` | 6,544 | `5677f6bee1cfdd676c4953e575eabda368e303a14f5e4a7dd440173ec44e3f9a` |

All transferred hashes, pixel-plane/sampler digests and five uniform-slice
digests match. Independent recovery finds all six original CRC headers using
the earlier unchanged fitted maps; swapped-eye and raw-Y controls refuse.
Those maps remain static diagnostic fits, not a native spatial authority.
Raw-Y versus native-sampled G differs by MAE98.6477; raw luma is not a substitute
for the native conversion/sampling path.

Bindings0/4 use the same272-byte buffer slice at effective offset960. Bindings2/6
use the same2,800-byte slice at offset0; binding3 contributes400bytes. Dynamic
offsets are zero. Both stage selectors are1 and the projection word is0.
The current record index is0, but its consistency word184686 differs from the
expected184685. The actual shader therefore takes the fallback branch to
binding3's fallback index0. Reading the current index alone would miss that
decision.

The selected vertex and fragment metadata matrices are bit-identical:

```
R0 = [0.2438118905, 0.8315594196, 0.5289604068, 0.9028465748]
R1 = [0, 0.75, 1, 1]
patch origin = [0, 0.5]
raw flags = 0x02018018
```

R1 maps to pixel edges `[0,3456,1152,4608]` in the1152x4608 packed source,
consistent with the earlier static right-source band. This is not independent
proof of which physical eye is displayed.

## Offline reference and independent checks

`tools/pixel_uniform_mapping.py` validates the bounded uniform artifact with
the existing strict reader, follows the current/fallback branch and evaluates
the two packed native texture lookups, reconstruction ramps and optional color
decoding/blending. Input `q` is explicitly **after native reprojection**. It
does not derive preceding vertex transforms, rasterization, physical output
pixels, application rays or PC composition. Explicit sample triples are inputs;
the helper does not invent unobserved sampler addressing, filtering or LOD.

An independent review disassembled the exact local original modules only into
process memory and interpreted the original fragment instructions on24
asymmetric cases, covering four flag combinations, both lookups/decoders,
edges, blending and tint. Maximum UV discrepancy is5.96e-8 and RGB discrepancy
is0. No Valve shader bytes or disassembly are tracked. Twelve focused mapping
test methods pass; their CMake registration is separately exercised. This is
not a new complete mod gate or a headset rendering-equivalence result.

## Rollback and startup health attribution

After explicit approval, the owned activation files were removed and SteamVR
was restarted once. The mapped library was then removed after its process
exited. Final certain mapping scans find no diagnostic mappings, and the four
capture files remain preserved. The maintainer confirms the normal visible,
usable Frame surface and may put the headset in standby.

The initial health check reported a vrserver SIGSEGV. Its exact PID belonged
to the **old server**, recorded before the restart; its crash preceded the
new service's execution start. Two fresh native UI observations and the user
confirmation establish the new runtime's health. The crash's underlying cause
is not established. Original failure data is retained, not rewritten as success.

The host health collector previously omitted vrserver from pre-restart process
identity collection. It now includes exact managed server identity without
making the server a substitute for UI readiness. Only an authenticated old
PID/executable/control-group match can exclude a teardown dump. Current or
unknown crashes remain fatal, and observed PID reuse survives checkpoint/resume.
The72 distinct focused methods and causal replay are composed evidence; unchanged
areas were not repeatedly retested. Future packages must include and review
this changed helper; the spent package is not silently updated or reactivated.

## Remaining path

The next source increment belongs in GloomhavenVR: a fresh app-owned session,
changing pair IDs and dense asymmetric per-eye registration patterns. Both eye
outputs must consume one frozen source pair, with GPU resource ownership proven.
The selected native decoded image must identify that exact pair, including drops,
repeats, motion and composition controls. A constant shift between packed tiles
cannot establish matching application rays under nonlinear streaming transforms.

Real fractional game coverage and an admitted native alpha presenter are still
separate tasks. All live mapping/passthrough authority remains false. There is
no automatic timer, renewed Frame installation or restart authorized by this
offline checkpoint.

Private evidence: `build/private-uniform-final-draw-run-20261016-r1/`,
`build/private-health-server-baseline-20261016/`,
`build/private-native-uniform-map-20261016/`. Original captures, source receipts
and independent review reports are retained; completed regenerable build caches
and worktrees are removed after their processes finish.
