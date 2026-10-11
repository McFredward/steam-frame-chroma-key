# Bounded passive compositor pipeline capture

This diagnostic is a separate Vulkan layer, `VK_LAYER_FRAME_MASK_trace`.
It observes shader-module fingerprints, graphics-pipeline stage tuples, recorded
draws and successful queue submission. It never replaces a shader, changes a
descriptor, copies an image, reads mapped GPU memory or enables passthrough.

The maintainer authorized temporary sessions on 2026-10-11. Initial loading
failed because the manifest version had the wrong JSON type. Corrected attempts
loaded the diagnostic. The fifth attempt supplies 39 complete recorded/submitted
pairings with a healthy footer; later state-capacity reports make its remaining
records incomplete. Neither the footer nor these positive pairs establish an
exhaustive capture or working masks.
The exact procedure, changed paths and verified rollback are recorded
in [the session ledger summary](FRAME-TRACE-SESSION.md). Headset access otherwise
remains read-only, apart from the previously authorized SSH-service setup.

## What this closes

The installed [Layered VS/fragment candidate](FRAME-UV-BINDING-20261011.md) has a
matching interface and a conditional full-eye UV route. Installed-file names
cannot establish the actual pipeline. This layer can determine which module
fingerprints were used together in a recorded draw, with command-buffer and
pipeline generations, and whether that generation was submitted successfully.

That is deliberately narrower than GPU completion or a mask binding proof.
Descriptor-relative uniform values, effective dynamic offsets, eye selection,
decoded-surface provenance, resource fences and PC source-frame association are
not captured. Specialization constants, render targets and viewport/layer routing
are also outside this first capture. A `complete` event means its tracked stage
fingerprints and command bookkeeping are complete, not that these other links
have been established. No mask guard is removed.

## Bounds and forwarding

- Capture requires `FRAME_MASK_TRACE=1` and an executable named `vrcompositor`.
  `FRAME_MASK_TRACE_DISABLE=1` disables it, including explicit loader selection.
  Other processes keep native forwarding and produce no trace.
- Observation uses fixed-capacity shader, pipeline and command records. Handle
  destruction/reset/reuse has a separate generation. Observation contention or
  state-capacity loss makes evidence incomplete; it never delays rendering to
  recover a sample.
- Metadata has a 128-event ring, a 2,048-event lifetime limit and a 1 MiB file
  limit. A background worker performs all file output. There is no shader-file
  scan, image readback, socket I/O or GPU work in a render hook.
  The single reader uses release/acquire publication/consumption positions and
  never takes the producer mutex. Producer contention and overflow remain
  diagnostic losses. The writer closes admission and waits for admitted
  bookkeeping before draining and serializing its stable footer; rendering
  threads never wait for writer shutdown or hold a scope across driver calls.
- One small forwarding record per live Vulkan instance/device is allocated at
  creation and freed at destruction. This lifecycle storage is separate from
  the fixed-capacity observation tables; no per-frame allocation occurs.
  Dispatch lookup has a short mutex critical section for native context lifetime;
  pipeline/event observation uses try-lock/drop. Optional binding observations
  use the bounded journal described below. This is not a universally lock-free layer.
- Filtered capture lasts at most 120 seconds; unfiltered capture at most 30.
  It ends at its event/byte/time bound or process teardown. Native
  Vulkan calls/results are forwarded regardless of diagnostic capacity.
  The writer checks expiration during continuous traffic and drains only its
  bounded queue before the footer. This leaves recovery margin before the
  separate 180-second watchdog when timed recovery is enabled. The maintainer's
  next run explicitly uses manual recovery: recording completion causes no
  runtime restart. Calls after intentional budget closure are ignored; genuine
  contention/capacity loss during an open observation window remains counted.
- Pipeline libraries, unobserved modules, overlarge modules and unsupported
  observation paths cannot provide a complete stage claim. Reordered/missed
  event data must not be treated as reliable resource/frame association.

`FRAME_MASK_TRACE_OUTPUT` selects an absolute, **new** metadata filename.
The background worker creates it exclusively with mode 0600, rejects an existing
file/symlink, and writes a header and bounded footer. Without a selected filename,
the default is `/tmp/frame-mask-trace-<pid>.jsonl`. Retain neither indefinite logs
nor repeated captures in `/tmp`; archive the small evidence and remove completed
artifacts after each run.

Optional `FRAME_MASK_TRACE_FRAGMENT_HASH` is exactly 16 hexadecimal digits of
standard FNV-1a 64. It filters draw/submission evidence to pipelines containing
that fragment module, so unrelated startup rendering does not consume the
capture. FNV is a diagnostic fingerprint, not authentication: confirm the actual
installed file's size and SHA-256 separately before trusting a build-specific
match. Malformed filters disable capture.

The inspected private modules on build `20260922.6101926` have these diagnostic
fingerprints; their SHA-256 values are recorded in the linked UV report:

| Module | Bytes | Standard FNV-1a 64 |
| --- | ---: | --- |
| `distort_ps_achromatic_se2` | 6,620 | `f333a137b4e79ced` |
| `distort_vs_reproject_layered` | 9,460 | `9cc04d186b9c8fe5` |
| `distort_vs_reproject_layered_nd` | 7,336 | `39aff5dc14d026c4` |

These numbers identify candidates only. Finding a pipeline that was merely
created is insufficient; require matching `draw_recorded` and `submit_accepted`
command generations, accounting for `execute_recorded` secondary-buffer links.
Neither an FNV match nor a successful submit establishes GPU completion.
Submission tickets capture the command generation before calling the native
driver. A concurrent completion/reset/re-record cannot be certified as the
original submit; a changed generation remains an incomplete record. Allocated
or reset commands are not treated as executable until a successful end.

Require a healthy footer before using the captured relationships: any nonzero
`dropped_events` or `lost_state`, `byte_cap_reached`, missing footer or failed I/O
leaves the evidence incomplete. A later event's `complete: true` does not repair
an earlier missing draw or secondary-buffer link. This diagnostic observes the
six standard direct/indexed/indirect draw calls in its source; other draw families
are not certified by absence of records.

A healthy footer is necessary, not sufficient. Each certified draw needs its
own complete stage/pipeline/module generations and a complete accepted-submit
record with the same device, command and command generation, or a fully observed
secondary-command execution chain leading to one. State-capacity reports can
taint later events without incrementing the footer's contention counter. Accept
an explicit complete positive pairing before such a gap only within that scope;
never describe later incomplete pairs or the entire capture as established.

## Local build and focused validation

The normal Linux test configuration builds the layer and pure state tests.
The real-loader test copies the original headless fixture executable to the name
`vrcompositor`, enables the layer only in that temporary subprocess, checks exact
stage fingerprints/submitted draws and verifies the original GPU output pixels.
It also checks disabled/wrong-process forwarding and existing-log preservation.
Exact-match, nonmatching and malformed fragment filters are exercised through
the actual loader as well.
All fixture shaders are original test code; all temporary files are automatically
removed on exit. It does not load a headset runtime or Valve shader.

```bash
cmake -S . -B build/mask -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mask --parallel 4
ctest --test-dir build/mask -R '^passive_pipeline_trace' --output-on-failure
```

To compile only the diagnostic library on a Linux development machine:

```bash
cmake -S . -B build/trace -DBUILD_TESTING=OFF -DFRAME_MASK_BUILD_TRACE=ON
cmake --build build/trace --target VkLayer_frame_mask_trace --parallel 2
```

The generated JSON manifest points to the local build's library path. It is not
an installer; never copy a development-machine path or an x86-64 library onto
the AArch64 headset. A staged Frame build must use the correct architecture and
an explicitly reviewed library/manifest path. No service environment, layer
installation, camera setting or runtime restart is changed by these commands.

## Recorded validation, 2026-10-11

The final submit-ticket repair passed the focused pure-state and real Vulkan
loader/GPU suites. Deterministic tests cover reset/re-record between capture and
submit return, failed submissions, handle reuse, secondary links, contention,
capacity bounds and disabled paths. The worker also passed the core under
AddressSanitizer/UndefinedBehaviorSanitizer. The layer cross-compiles with strict
warnings for AArch64. A subsequent standalone dependency-only Frame probe loaded
the library without creating a Vulkan instance/device; the native compositor did
not load it in the attempted capture session.

Unchanged transport, shader-generator and collector checks inherit the earlier
green eight-suite CI checkpoint `f702142`; the two-suite focused pass is not a new
complete foundation gate. Build and test outputs are local, ignored and cleaned
after recording evidence; only the small staged diagnostic library is retained
while needed for a possible authorized hardware capture.

The pushed checkpoint `8fe0088` subsequently passed the complete
[ten-suite CI run](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38131947911),
including the real Vulkan layer/pixel checks, Windows diagnostic-sender
cross-build and protocol sanitizers. This is software evidence; no Frame
capture or genuine-mask hardware result is implied.

## Opt-in resource metadata extension

`FRAME_MASK_TRACE_BINDINGS=1` enables a second, metadata-only diagnostic scope.
The reviewed session helper exposes this as `start --bindings`. Its default
scope remains unchanged. No buffer contents, mapped memory, image pixels or
camera images are read. No GPU commands, descriptor mutation or replacement
shader are introduced.

The extension tracks bounded public Vulkan lifecycle inputs for layouts,
descriptor pools/sets, images/views, samplers, buffers, memory and command
buffers. At most 32 selected graphics draws emit `bindings_recorded`, including
both eyes when one pipeline is used repeatedly in a command buffer. Each record
contains separate resource generations, set/write revisions, image extent and
view subresources, sampler scalar inputs and effective dynamic-buffer offsets
for set 0 bindings 0, 2, 3, 4, 6, 9, 25 and 31.

Dynamic offsets follow Vulkan's set/binding/array order, independent of layout
input order. Incompatible descriptor rebinding cannot retain a certified old
set. See the [Khronos descriptor-set specification](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html).
This conservative implementation requires the exact observed pipeline-layout
identity; it does not infer structural compatibility between different handles.
Unsupported descriptor arrays/extensions, unknown import `pNext` chains, missing
lifecycles, resource reuse and observation loss remain incomplete.
Known scalar identities are retained for investigation even when an extension
prevents complete interpretation. `layout_has_binding31` is independently
`unknown`, `absent` or `present`; unknown never authorizes adding a mask slot.

The JSON header reports `binding_metadata_version: 1` and the 32-snapshot bound.
The footer adds `lost_binding_state`. Require zero loss and a complete source
record before treating its scalar relationship as established. Ordinary draw
and accepted-submit records still establish only command-generation linkage.
`gpu_consumed_coherent` and `video_frame_associated` stay false: recording-time
API metadata does not prove the uniforms consumed by the GPU or the selected
video frame. There is no post-hoc host read of eye or foveation constants.

The live shader inventory is fixed at 1,024 entries, raised from 256 after the
first successful Frame capture exhausted that original table. Pipeline/command
storage, event count, 1 MiB output cap and capture deadlines remain bounded.
The writer performs all serialization. Pipeline/event observation retains
try-lock/drop; optional binding hooks publish owned API scalars to fixed queues
for ordered background replay, avoiding the binding-table observer mutex on
rendering threads. This diagnostic has bounded CPU overhead; it does not claim
that native rendering is cost-free while observation is enabled. See
[the journal implementation and validation](PASSIVE-BINDING-JOURNAL.md) for its
capacities, supported import/YCbCr metadata and remaining proof gaps.

On the Frame, the helper configures a private arm marker so initial Home draws
do not consume the event or 32-snapshot budget before a PC-VR stream is connected. Lifecycle
tracking continues before arming. Only the background writer checks the marker,
at most every 100 ms; hooks read an atomic ready flag. Arming never extends the
120-second/event/byte recording bounds or restarts SteamVR. The real-loader GPU
fixture exercises absent, malformed and accepted markers, plus 48 native draws
with exactly 32 resource records. It also executes 2,400 native draws before
delayed arming and requires six complete subsequent draws/resources/submissions;
the pre-arm draws must consume no capture events. A `bindings_armed` event marks
writer acceptance; the helper's ledger marks only the arm request. A writer-only
file lock lets the helper reject closed/crashed captures, including closure
without a footer. Control-file reads and locking stay off rendering threads.
Header/footer fields distinguish an unarmed capture from a completed resource
sample. Fixed footer counters identify lost-state reasons and capacity tables;
any such loss still invalidates completeness. None proves frame association.
