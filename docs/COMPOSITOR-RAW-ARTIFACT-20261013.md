# Combined compositor import diagnostic

The next concrete step towards explicit-mask Steam Link passthrough is a local
association between a genuinely sent import-reply token and the compositor's
actual public Vulkan image, memory binding and image view. This artifact does
not activate a shader, mask, passthrough, colour replacement or PC Steam patch.
The first Frame startup attempt on 2026-10-13 failed before a VR stream was
requested. The temporary activation and input files were removed, and the
original SteamVR service, native files and readable managed-process mappings
were verified after restoration. The failed artifact must not be deployed again.

## Artifact and admission

`FRAME_MASK_COMPOSITOR_RAW=ON` with `FRAME_MASK_BUILD_IMPORT_ENTRY=ON` builds one
combined ARM64 `libVkLayer_frame_mask_trace.so`. It is both the selectively
preloaded observer and the implicit layer; those paths must identify the same
file. It uses the layer's existing resource Journal and native per-device next
providers, rather than constructing a second resource registry or graphics
dispatch path. Only three **privately named** layer entry points and the owned
audit bridge are public. The ordinary `vkGetInstanceProcAddr`,
`vkGetDeviceProcAddr` and negotiation names are not exported by this DSO; the
manifest selects `frame_compositor_raw_` aliases instead. Eleven native
forwarding entries and the readiness marker remain hidden. The raw build cannot
opt back into the unsafe ordinary names.

The offline preparer validates the final ELF, complete separately reviewed file
hash, scope marker, static TLS, hidden entries, real compatibility imports,
native profiles and provider addresses. It produces an immutable audit library,
the exact 8,272-byte checksummed envelope, layer manifest and deployment plan.
No Valve executable, code dump or image data is packaged. The observer's
constructor checks actual mapped identities again. Incorrect executable,
provider, profile, mode, file ownership or output protection leaves it inert.

The actual native executable uses mutex functions in its constructors. The
selective audit therefore permits authenticated **forwarding only** once the
owned cold-ready marker is set. Recording remains unarmed until the actual
loader `la_preinit` callback. Early or conflicting bindings cannot silently
recover later. The readiness marker is not permission to read native memory.

## Preparation and bounded capture

The first source-specific import lock selects one IPC worker. Before its native
pipe lock is acquired, the authenticated pthread helpers obtain that worker's
usable stack bounds once. This explicit cold, non-render preparation may take
libc's descriptor lock, allocate affinity scratch and query affinity through
its SDK implementation. It is not allocation-, lock- or syscall-free. It never
runs in a Vulkan graphics callback. The constructor caches the authenticated
MAIN-thread identity and rejects that thread before `pthread_getattr_np`,
excluding its `/proc`/cancelling-stdio fallback. The supported worker path is
source-reviewed; no caller cancellation mode is changed.

After the unchanged original lock succeeds, one 16-byte parent-pair copy checks
the source-pinned dispatch caller. Private raw copies require this admitted
current worker, original entry pairing, exact call route, live mapped providers,
correct order and the remaining budget. Root counts actual copies even when a
later ownership check fails. No main-thread identity, invocation label, frame
address, reply sequence or stack range becomes a source-frame proof.

| Scope | Limit |
| --- | ---: |
| Native import transactions | 1 |
| Parent-pair bytes | 16 |
| Sparse stack-scalar bytes | 672 |
| Successful reply-prefix bytes | 21 |
| Total mutable native bytes copied | 709 |
| Positive partial reply writes | 8 |
| Raw output | 32 records, 64 KiB, 120 seconds |
| Existing passive Journal output | 1 MiB, bounded queues and records |

No request payload, pixels, native heap, shader binary, private uniform or
additional socket/kernel diagnostic query is copied. The cold mapped-code
verification uses the separately bounded existing module-identity verifier.
Expiry only disarms recording; it never restarts Steam or restores installation.

Vulkan callbacks copy only source-approved scalars and genuine public API results
into fixed owned records. The existing Journal replays tagged creations on its
background owner. Receipts are delivered only after draining and validation.
The root writer checks the exact image/view/memory identities, formats, extents,
bindings and diagnostic tag, then validates currentness again before publishing
the local CPU association. Tags and cookies are local correlation labels;
Journal generations retain their original domain.

## Lifetime and recovery

Callbacks and the audit bridge are accounted as producers. Finalization blocks
new acquisition, waits only on the background writer for bounded callback
completion, drains owned records and reports incomplete scopes honestly. Process
fork retirement uses atomics only: no inherited mutex, producer wait, join,
filesystem operation or service restart. The consumer shuts down before the
owning Journal's destruction. Native cancellation/unwind continues through the
existing entry veneers.

The session helper owns only its protected user-directory artifacts, one runtime
`steamvr.service` drop-in and one user implicit-layer manifest. It snapshots the
selected service/environment and native files, refuses conflicting activation,
and requires separately acknowledged scope and restart readiness. Recovery
removes only authenticated owned files and restarts SteamVR at an agreed time.
No timed rollback, system file, PC sender change or automatic Steam restart is
included. Unload verification is separate from configuration restoration; keep
the Frame powered until that final verification is reported.

Complete local repairs, tests and final artifact review before requesting a
headset action. Prepare and read-check the inactive session before requesting
restart readiness. Consume that confirmation immediately for the coordinated
restart; request the VR stream only after confirming healthy compositor startup.
If local investigation becomes necessary, restore first and explicitly release
the maintainer from the headset. No testing or packaging queue should run while
the maintainer is waiting for an already requested action.

## Failed startup and coverage gap

The first hardware attempt produced the bounded raw header, cold preparation,
the actual audit attachment and three original-provider binding records. It
produced **no native import candidate, raw native copy or resource association**,
and no completion footer. SteamVR stopped at `vkCreateQueryPool: Invalid device`.
This is a failed startup, not evidence of a usable Vulkan diagnostic or
passthrough. No replacement shader or image transformation was enabled.

The combined artifact was loaded both through `LD_PRELOAD` and the implicit
Vulkan-layer manifest. Its ordinary public Vulkan entry-point names could
interpose the application's loader API and bypass its dispatch setup. The
earlier graphics fixture activated only the manifest; the ARM preload fixture
verified libc forwarding without constructing a Vulkan device. Neither check
covered that combined topology. A real-loader/Mesa control now demonstrates
that the old layer works through the manifest alone but its preloaded
`vkGetInstanceProcAddr` replaces the loader's global entry and prevents ordinary
instance bootstrap. The host failure occurs earlier than the Frame error; it
does not reproduce the exact Frame query-pool crash.

The repair must expose private, manifest-selected layer entry points while
leaving the application's public Vulkan API at the native loader. The final
artifact and generated plan must reject the old public-entry-point form, and a
new real-loader fixture must exercise **preload and implicit-layer activation
together**, including device, query pool, memory, image and view lifetimes. The
loader's documented negotiation supplies the layer's internal dispatch pointers;
the alternate negotiation name must appear in the manifest's `functions` map.
See the [Vulkan Loader layer interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md).

The restoration helper also encountered changed native graphical-session
manager metadata after the restart. Inspection confirmed the original selected
service and unchanged environment files. Post-restart compositor comparisons now
permit only validated existing `XDG_SESSION_ID` and
`/org/freedesktop/DisplayManager/SessionN` metadata, plus the existing two bounded
Gamescope file-path exceptions. Staging and pre-restart activation comparisons
remain exact. Unrelated environment changes and native service/file changes
still fail closed; no stale session value is written back. Receiver-session
comparisons retain their earlier path-only policy.

## Evidence and remaining goal

Local checks cover bootstrap/audit forwarding, source-specific worker admission,
actual adapter callbacks, byte ledgers, failed and partial copies, owning-Journal
receipts, fork handling, writer protection, archive/recovery and the final ELF.
The exact ARM library is also executed under QEMU with the retained hash-pinned
Frame libc/loader; public calls remain native, hidden forwarding preserves
results and `errno`, and read cancellation crosses the final entry correctly.
These are software/ABI results, not Frame-kernel, GPU or headset evidence.

A successful hardware capture would prove only this **local CPU resource join**.
Continuous client/compositor namespace and content lifetime, contemporaneous
receiver association, exact PC source session/frame/eye, native UV/foveation and
GPU/mask lifetime still require proof. All of those proof flags stay false in
every raw record. A mask must remain opaque whenever those associations are
missing; the final passthrough path may not substitute a latest-mask heuristic.
