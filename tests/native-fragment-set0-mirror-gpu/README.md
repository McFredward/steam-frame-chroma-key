# Compatible one-set mirror GPU oracle

This private-input, owned-raster test checks the **Vulkan call pattern** used by
the compatible Set0 mirror. It does not call production `Clone` or `Transaction`,
replay the original vertex stage, or authorize a receiver substitution.

The exact original VS and PS are explicit, private inputs. The VS is checked by
the production mirror transformer, not drawn. The control PS changes only the
independently verified constant-zero image-array extent to one; its executable
original instructions remain unchanged. The probe adds the separately pinned
128-byte record at Set0/binding8. The fixture owns the raster vertex, descriptor
data, later consumer shader and all GPU resources.

The finite eight-binding Set0 definition contains the seven statically used
source bindings and the unused storage binding8. It is **not** the complete
native 29-binding layout census. Two distinct Set0 layout objects have identical
complete definitions; the mirror is populated once by seven descriptor copies.
Native storage8 remains untouched, and the mirror's storage8 refers only to the
owned record. Two sorted, nonzero dynamic offsets and both already-bound higher
sets are consumed by later draws. A subsequent native-style rebind must still
produce the exact expected values. All draws here use separate compatible
render-pass instances; production same-pass callback order is tested separately.

Thirty-six cases compare control, probe and restored output bit-for-bit and
check the same-invocation record. Two legal causal mutations omit the Set0
restore or restore incorrect dynamic offsets. Each must produce its exact wrong
value, followed by complete recovery on the next native-style bind. Real Khronos
and synchronization validation must report no error, including those mutations.

Run with Vulkan development headers, `g++`, `glslc`, `spirv-val`, the Khronos
validation layer, and the requested installed ICDs:

```sh
python3 -B tests/native-fragment-set0-mirror-gpu/run.py \
  --vertex /private/distort_vs_reproject_layered.spv \
  --original /private/distort_ps_achromatic_se2.spv \
  --record /private/exclusive-result.json --driver both
```

Temporary binaries, shaders and validation working files are removed after all
children terminate. Preserve the result and invoke the integration checkout's
`cleanup_completed_run(..., process_reaped=True)` completion API after the run;
remove any exclusively owned external validation-package cache after its last
consumer exits. No Valve input or generated shader is redistributed.
