# Read-only Steam Link inspection, 2026-10-11

## Scope and result

The maintainer started a PC Steam Link stream for this inspection. Existing
Frame processes, executable identities and bounded log tails were read over SSH.
No receiver, Vulkan layer, driver hook or diagnostic package was installed.
No runtime was initialized, restarted or reconfigured. The independently
authorized persistent SSH service setup is not a mask activation.

This confirms the deployed decoder/compositor builds and real decoder extent.
It does **not** establish a source-to-decoded-frame association, resource
generation, mask descriptor binding or native eye/foveation mapping. Genuine
mask composition remains disabled.

## Observed runtime

| Property | Observation |
| --- | --- |
| Frame OS | SteamOS 0.3.0, build 20260922.6101926, variant `vr` |
| Streaming process `comm` | `vrlinkrunthread`; executable basename `vrlink` |
| `vrlink` ELF build ID | `0a51addb6a9ef05dc52c055b1076947908f20acc` |
| `vrcompositor` ELF build ID | `ba0d798c2694e05985421e848b67ced8984fe2d3` |
| `vrserver` ELF build ID | `5aa95363d05998027c870b98e01281f46d2b3ab5` |
| Client-reported OpenVR SDK | 2.17.10; this is not a SteamVR release number |
| Decoder capture extent | 1152 × 4608, format label `C01Q` |
| Decoder capture buffers | 16 |

The live build identities match the binaries used for the private static
investigation. The capture extent is the packed decoded image, **not** a
verified per-eye mask extent. It must not be used as an assumed mask UV layout.

The October 4 `vrlink.txt` available before the stream was stale. During the
new stream the log was recreated and grew with current decoder and FEC events.
Its old allocation/reset failures must not be reported as current failures.

Follow-up [static dataflow analysis](FRAME-ASSOCIATION-DATAFLOW.md) distinguishes
the actual received packet sequence from the decoder buffer index and follows
it into metadata lookup. This is stronger receiver-side evidence than a log
label, but still does not establish the PC encoder's source identity.

The final bounded observer run read 291,129 bytes in 27 ms. It scanned 3,857
complete tail lines, retained four decoder-configuration records and sampled
16 of 50 FEC retry lines. The expected `fec_record_limit` warning means the
retry sample was capped; the observer continued scanning for association fields.
No missing-metadata or decoder-allocation-failure record was found in those
tails. This does not establish that the entire streaming session was error-free.

## What the existing logs do not provide

The FEC retry diagnostic reports packet count, skipped and last-sub values,
group and expected size. These belong to FEC reassembly. The inspected tails
contain no association records proving that those values equal an encoder,
decoder or compositor frame identifier.

The fresh client log records a limited compositor client and an internal camera
passthrough interface. It contains no supported mask-import contract or verified
private compositor signature. Existing diagnostics cannot supply the missing
association simply by running the observer repeatedly.

## Next input

Inspect the **PC driver used for this stream** with the
[PC source collector](PC-MASK-SOURCE.md). Its actual display component and
encoder input determine where composed coverage and a matching source identity
can be obtained. Public mirror polling supplies neither a documented encoder
association nor a guarantee of retained coverage.

The currently inspected GloomhavenVR commit `ca4f8ff79` also makes the PC
chroma background opaque (`MixedReality.cs` sets `key.a = 1f`). A later explicit
alpha-export mode must preserve coverage before encoding; reading the current
PC background's alpha cannot replace that change. No GloomhavenVR file was
modified by this fork work.

Raw logs, executable copies and shader disassemblies remain private and ignored.
The committed report contains only the selected build/configuration findings.
For the observer's bounds and commands, see
[read-only live inspection](LIVE-BRIDGE-INSPECTION.md).

## Validation scope

The two new CTest suites passed: 27 live-observer tests and 11 PC-collector tests,
including private-data omission, read bounds, stale logs, exact process aliases
and noisy FEC logs followed by association records. They are original synthetic
fixtures. The observer was also run through `python3 -B -` on the real Frame.

Transport, shader generation and Vulkan rendering did not change. Their earlier
passing evidence remains applicable; this is a focused integration check, not
a new complete gate or a headset passthrough test.

Code checkpoint `f702142` also passed
[CI run 38129068756](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38129068756):
all eight CTest suites, the Windows endpoint cross-build and protocol
ASan/UBSan checks. The later static-analysis corrections change documentation
only and reuse that code validation; no headset mask activation is implied.
