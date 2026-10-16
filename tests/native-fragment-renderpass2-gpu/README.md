# Owned RenderPass2 equivalence GPU proof

This focused fixture creates a real ordinary `VkRenderPassCreateInfo2` pass,
then passes that same definition through production `captureRenderPass2()` and
`RenderPassDefinition::view()` to create a separate compatible legacy renderpass.
The original pipeline is built against RP2; the observer pipeline against the
lowered legacy pass. Both run inside original `vkCmdBeginRenderPass2` /
`vkCmdEndRenderPass2` scopes with an actual indexed draw.

Each of two asymmetric input cases records original, observed and restored
outputs in one command buffer. Set0, push constants, dynamic viewport/scissor
and index buffer are set once. The observer adds only Set1 and its pipeline;
the next Vulkan command after its original indexed draw restores the original
pipeline. The subsequent original output must be bit-identical to the baseline.
The inherited original-invocation observer color tolerance is `2e-6`; the
receipt also reports actual bit-identical observer cases. Its complete 128-byte
SSBO record, consumed state and output components are checked after the fence,
including surrounding canaries. A GPU fill resets the record before each
execution, outside the renderpass.

The existing private shader pin is required explicitly and is never copied to
tracked source. The vertex is owned raster fixture code. This exercises the
production renderpass lowering and compatible descriptor/push definitions; it
does **not** bypass the native vertex pin to invoke the full `Clone` factory.
No native renderpass payload, receiver hook, Steam Frame execution, physical
eye/frame association or passthrough presentation is proven.

Run only this proof, with a functioning Khronos validation package available
through `VK_LAYER_PATH` / `LD_LIBRARY_PATH` if it is not installed:

```sh
python3 tests/native-fragment-renderpass2-gpu/run.py \
  --original /private/path/to/distort_ps_achromatic_se2.spv \
  --record /private/path/to/new-rp2-receipt.json --driver both
```

Both Intel and Lavapipe runs require active validation and synchronization
validation. The receipt retains source/module digests, commands, device names,
results, validation messages and explicit scope limits. Each subprocess is
reaped before the temporary executable and derived modules are removed, on
success and failure. Operators must also invoke the main project's
`town_run_retention.cleanup_completed_run` after this standalone run, as for
the other private host fixtures; it never permits sweeping another worker's
inputs or captured evidence. No system packages or Frame files are changed.

The 2026-10-16 focused run passed on Intel Graphics RPL-P and Lavapipe
(`llvmpipe`, LLVM 20.1.2), with 197 checks per driver, two cases and six indexed
draws each. Both observer cases and both restored outputs were bit-identical
on both drivers; the maximum observer error was zero. Khronos validation
1.3.275.0 and synchronization validation reported no errors, including
teardown. The private receipt is
`build/private-native-rp2-gpu-worker-20261016/rp2-execution-r1.json`.
