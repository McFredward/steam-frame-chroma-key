# Native Frame compositor startup review, 2026-10-11

This is a read-only review of the installed native compositor after the
[temporary passive trace](FRAME-TRACE-SESSION.md) was removed. It does not
activate a layer, change SteamVR, or establish a working mask bridge. Private
binaries and disassembly remain in ignored developer evidence; none are
redistributed here.

## Identity and evidence

The inspected `/opt/steamvr/bin/linuxarm64/vrcompositor` is a stripped AArch64
ELF PIE, 5,964,624 bytes, SHA-256
`9b1bd5fe4463592cbc71b2517b9ceb0a0fcd66cd6885a19dca4be0784bf6c265`,
ELF build ID `ba0d798c2694e05985421e848b67ced8984fe2d3`.
Addresses below are virtual addresses relative to that executable's load base,
not stable ABI entry points for other builds.

The native service and its selection/first-initialization scripts were also
read without changing them. The service sets its Mesa environment from
`/usr/share/deckard/mesavars.sh`; its selection script changes to the selected
SteamVR tool directory and executes that original tool. The inspected scripts
contain no diagnostic-layer name or explicit Vulkan-layer filtering directive.
Their existing recovery behavior must remain untouched.

## Proven startup path

The executable does not link Vulkan through `DT_NEEDED`. Its loader helper at
`0x165dd8` opens `libvulkan.so.1` with `RTLD_NOW`; it falls back to `libvulkan.so`
only if the first open fails. It resolves `vkGetInstanceProcAddr` through
`dlsym`. There is no `RTLD_DEEPBIND` or alternate namespace on this observed
load. The helper at `0x165d60` obtains the ordinary global instance creation,
instance-extension enumeration, instance-layer enumeration and instance-version
functions through that proc-address route. The wrapper at `0x162660` forwards
to the resolved loader function.

Two native initialization routines, `0xadca0` and `0xb40a8`, call this loader
helper and enumerate available instance layers before creating an instance.
The first observed creation call at `0xae1ec` uses the resolved ordinary
`vkCreateInstance`; its non-validation branch has zero explicitly requested
layers. This is not evidence that implicit layers are disabled: an application's
explicit layer list and the loader's implicit layer selection are different
mechanisms. Static existence of these routines does not identify every branch
selected in the actual headset session.

A separate generic device/instance setup route at `0x30d7a0` enumerates available
layers and optionally requests `VK_LAYER_KHRONOS_validation` through its
validation facet. Nothing in the inspected instance-creation paths supplies a
hardcoded allowlist containing only the passive trace or its competitors.
This does not rule out policies in another library or in the loader.

## Environment and live loader observations

The executable imports `getenv`, `setenv` and `unsetenv`, so absence of an
initial `/proc/<pid>/environ` value cannot prove that it was never changed.
The inspected runtime-escape routine at `0x17b6f0` uses fork/exec and explicitly
removes these literals before launching its child:

- `STEAMVR_ENV`
- `STEANM_RUNTIME` (this spelling is present in the inspected binary)
- `QT_PLUGIN_PATH`
- `PRESSURE_VESSEL_RUNTIME`
- `SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM`
- `LD_LIBRARY_PATH`, unless a system library path is restored

Those operations do not themselves remove `FRAME_MASK_TRACE` or a Vulkan-layer
variable. The executable's searched string references contain no
`VK_LOADER_*`, `VK_LAYER_PATH`, `VK_INSTANCE_LAYERS`, `FRAME_MASK_TRACE`, or
`XDG_DATA_HOME` policy literal. This is a bounded static negative finding,
not proof that libraries cannot change environment values dynamically.

The restored native compositor maps `/usr/lib/libvulkan.so.1.4.309`, SHA-256
`aa5b75c7984d7d4169422ba7eab2fc013f2f186fb6fd627660c16225e23f60b6`, and
`libVkLayer_FROG_gamescope_wsi_aarch64.so`. Therefore this is not a process that
universally bypasses every Vulkan layer. The existing Gamescope layer's presence
does not prove how the diagnostic was selected or rejected.

The filtered initial environment of the restored compositor contains Mesa
version overrides and `DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1=1`, with no
observed `VK_*` loader-filter variable. This is evidence from the restored
process, separate from the attempted capture's proven inherited
`FRAME_MASK_TRACE*` values. Neither snapshot reveals later application changes.

The warning about a non-hooked swapchain really came from the attempted
compositor PID and executable. Its journal command name was
`vrcompositor-la`, but the diagnostic gate uses `/proc/self/exe`'s basename,
which is `vrcompositor`; the command-name alias cannot explain its process-gate
failure. The warning is a Gamescope bookkeeping symptom, not proof that the
unmapped diagnostic caused it.

## Remaining cause and next bounded check

These observations eliminate the inspected executable's custom direct-ICD
loader or a hardcoded Vulkan-layer environment cleanup as established causes.
This static review alone did **not** explain why the attempted diagnostic
produced neither a mapping nor a capture. The subsequent
[targeted loader run](FRAME-TRACE-STARTUP-REVIEW.md#manifest-rejection-established-by-the-targeted-startup-capture)
identified a numeric manifest version rejected by the native parser. That field
is now corrected to a string. Its isolated dependency/discovery probe did not
create a Vulkan instance and checked only the name, so its original success
did not establish a valid manifest or native instance chain.

The next useful evidence, after correcting and validating the manifest, is the
actual loader's startup decision: discovery
paths, implicit enable/disable evaluation, selected layer order, library load,
interface negotiation and instance-chain creation. Obtain that in a bounded,
reviewed diagnostic session with the same recorded rollback protections,
rather than repeating the unchanged installation or forcing a shader. First
compare any SteamVR layer-enumeration log with the reviewed manifest and native
loader identity. A loader report of discovery, an accepted negotiation, and an
actual `vkCreateInstance` entry are distinct checkpoints.

No conclusion here authorizes an explicit mask replacement, a system-wide layer
filter change, or a persistent compositor modification. Frame access remains
read-only outside a specifically authorized temporary diagnostic.

## Validation and cleanup

No implementation changed, so no repeated rendering or full test gate was run.
The report was checked against the actual ELF metadata, selected bounded native
function regions, native service/scripts and filtered process evidence. Its
private analysis files are small. The worker's dependency environment was
removed after this completed review; the private reference and analysis record
are retained for audit.
