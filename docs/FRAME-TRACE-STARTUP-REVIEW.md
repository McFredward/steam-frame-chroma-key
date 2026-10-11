# Passive trace startup review, 2026-10-11

This review changes no headset configuration. The authorized capture was
withdrawn, its activation files removed and native SteamVR restored. Genuine
mask activation remains blocked. Private process/journal evidence is retained
outside Git under `build/private-frame-trace-20261011` and
`build/private-frame-startup-20261011` in the integration checkout.

## What the evidence establishes

- The capture restart at 12:36:28 CEST created compositor PID 222054. Its initial
  environment contained the intended enable flag, fragment filter and output
  path. The opt-in manifest remained present when inspected later.
- There was no capture file and the inspected compositor did not map the trace
  library. This rejects the run; it supplies no active shader-stage evidence.
- The separately inspected restored compositor had `AT_SECURE=0`, user UID/GID,
  no effective/ambient capabilities and an ordinary executable. Its reported
  executable basename was exactly `vrcompositor`, matching the trace's gate.
  These observations weaken privilege and plain filename hypotheses, but are
  not a snapshot of every startup process or every moment in the failed run.
- A subsequent isolated Frame probe loaded the library with `RTLD_NOW` and
  enumerated the manifest. It created no Vulkan instance/device, so it does not
  establish the compositor's instance-creation layer chain.
- The local GPU fixture now passes implicit discovery through `XDG_DATA_HOME`,
  as well as explicit loading. Earlier green tests used explicit layer selection
  only; they did not exercise the headset installation route.

The subsequently copied service and launchers show that the drop-in environment
is available before `ExecStartPre` launches `vrstartup`; the service's main
process is `vrcmd --background --waitforquit`, not the compositor. The inspected
launcher preserves inherited variables, chooses the SteamVR path and executes
the requested tool. `steamvr_first_init.sh` initializes OpenVR/OpenXR settings,
without touching our manifest. Other pre-start commands apply a pending runtime
path, merge overlays and perform native health tracking; their implementations
were not inspected here. Nevertheless, the manifest survived startup, so a
simple pre-start deletion does not fit the recorded observation.

`Type=simple` service activity proves the launch process is running; it does not
prove that the compositor created its expected Vulkan instance. Also, the
`vrcompositor-la` journal/thread comm differs from the executable name, but the
trace uses `/proc/self/exe`, not comm. Its recorded executable still matches.

## Remaining distinctions

The loader separately discovers, selects and dynamically loads layers. JSON
enumeration alone does not prove selection or loading. Initial `/proc/environ`
also cannot prove that application-side environment changes did not occur before
Vulkan initialization. Native loader filters, overrides, a different loaded
`libvulkan`, or a transient process exit therefore remain questions, not assigned
causes. See the official [Khronos layer interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md).

The latest filtered initial native environment contains Mesa GL/GLSL version
overrides and an unrelated AMD-layer disable flag, but no Vulkan loader filters.
This weakens inherited filtering; it does not exclude later in-process changes.

Structured journal metadata subsequently attributes the Gamescope warning to
the failed-run compositor PID 222054 and its exact executable. This improves
the evidence over the launcher-prefixed textual log.
[Valve's WSI implementation](https://github.com/ValveSoftware/gamescope/blob/master/layer/VkLayer_FROG_gamescope_wsi.cpp)
raises the warning when its surface/swapchain bookkeeping lacks an entry; that
message does not identify our diagnostic as the cause.

The restored compositor PID 226988 maps `/usr/lib/libvulkan.so.1.4.309`, SHA-256
`aa5b75c7984d7d4169422ba7eab2fc013f2f186fb6fd627660c16225e23f60b6`, as well as
the native Gamescope WSI layer. This identifies the restored process's loader;
the exact failed-run loading sequence was not captured.

## Evidence required before another live attempt

Read-only preparation should retain the already captured service/launchers and
filtered Mesa environment, inspect the runtime path/overlay helpers, and
record all current compositor PIDs/executable paths and mapped loader identity.
Compare that loader with the isolated probe rather than assuming they match.

If a further temporary startup capture is authorized and the maintainer is ready
for interruption, reuse the reviewed independent rollback and exact file ledger.
Add bounded startup loader messages (`VK_LOADER_DEBUG=error,warn,layer`) rather
than repeating the previous silent run. Copy startup timestamps and PID/cgroup
attribution before cleanup. The resulting record must distinguish:

1. Manifest not searched/found.
2. Manifest found but layer filtered or context disabled.
3. Layer selected but library loading/negotiation failed.
4. Layer inserted in an instance chain but trace creation refused/failed.
5. A different or short-lived process performed initialization.

Limit the observation to one startup attempt, end early on a missing trace or
native warning, and retain at most 1 MiB/2,048 lines of copied loader metadata.
Record truncation and reject incomplete evidence; the source journal and copied
metadata have different bounds. The independent watchdog must already be alive
before enabling either trace or loader-debug variables, and must initiate
rollback by 180 seconds even if the SSH connection disappears. Use only the
exclusive private user directory and boot-scoped service drop-in from the
reviewed helper. Do not change persistent service configuration or firmware.

This capture should locate the failure stage; it cannot promise a root cause
when messages are incomplete, nor establish UV/frame binding or working masks.

Never force-enable layers, replace native loader policy, change capabilities or
disable Gamescope to obtain a trace. Missing data remains a failed observation.
After every attempt verify activation-file removal, native service operation,
no diagnostic mapping, watchdog exit and private-session-directory cleanup.
