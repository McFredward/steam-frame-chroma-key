#!/usr/bin/env python3
"""Generate a chroma-keyed variant of vrcompositor's scene shader.

Patches distort_ps_achromatic_se2 (the pass that converts the vrlink NV12
stream to RGB and reprojects/distorts it into the eye buffer).  The original
ends with   OpStore %504 %994   where %994 = vec4(rgb * tint, 1.0).
We insert code that computes alpha and stores premultiplied (rgb*a, a);
vrcompositor's passthrough pass then blends the camera in with
ONE_MINUS_DST_ALPHA wherever a < 1.

usage: gen_key_spv.py <orig.spvasm> <out.spv> rect  x0 y0 x1 y1 [invert]
       gen_key_spv.py <orig.spvasm> <out.spv> chroma R G B threshold softness spill
"""
import subprocess
import sys

STORE = "OpStore %504 %994"
UV = "%883"      # vec2: position within the scene layer's valid rect, 0..1
RGB = "%911"     # vec3: final tinted rgb before the alpha=1 store
FUNC = "%4 = OpFunction %void None %3"


def cbcr(r, g, b):
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    return (b - y) / 1.8556, (r - y) / 1.5748


def f(x):
    return repr(float(x))


def main():
    src, out, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    a = [float(x) for x in sys.argv[4:]]
    asm = open(src).read()
    for marker in (STORE, FUNC, "%1 = OpExtInstImport \"GLSL.std.450\""):
        assert asm.count(marker) == 1, "unexpected shader layout (SteamVR update?): " + marker

    consts = ["%mk_f0 = OpConstant %float 0", "%mk_f1 = OpConstant %float 1"]
    code = []
    if mode == "rect":
        x0, y0, x1, y1 = a[:4]
        invert = len(a) > 4 and a[4] != 0
        consts += ["%mk_x0 = OpConstant %float " + f(x0), "%mk_y0 = OpConstant %float " + f(y0),
                   "%mk_x1 = OpConstant %float " + f(x1), "%mk_y1 = OpConstant %float " + f(y1)]
        code += [
            "%mk_ux = OpCompositeExtract %float " + UV + " 0",
            "%mk_uy = OpCompositeExtract %float " + UV + " 1",
            "%mk_c0 = OpFOrdGreaterThanEqual %bool %mk_ux %mk_x0",
            "%mk_c1 = OpFOrdLessThan %bool %mk_ux %mk_x1",
            "%mk_c2 = OpFOrdGreaterThanEqual %bool %mk_uy %mk_y0",
            "%mk_c3 = OpFOrdLessThan %bool %mk_uy %mk_y1",
            "%mk_a0 = OpLogicalAnd %bool %mk_c0 %mk_c1",
            "%mk_a1 = OpLogicalAnd %bool %mk_c2 %mk_c3",
            "%mk_in = OpLogicalAnd %bool %mk_a0 %mk_a1",
            "%mk_a = OpSelect %float %mk_in " + ("%mk_f1 %mk_f0" if invert else "%mk_f0 %mk_f1"),
            "%mk_rgb = OpVectorTimesScalar %v3float " + RGB + " %mk_a",
        ]
    elif mode == "chroma":
        r, g, b, thr, soft, spill = a[:6]
        soft = max(soft, 1e-4)
        kb, kr = cbcr(r, g, b)
        consts += [
            "%mk_lr = OpConstant %float 0.2126", "%mk_lg = OpConstant %float 0.7152",
            "%mk_lb = OpConstant %float 0.0722",
            "%mk_luma = OpConstantComposite %v3float %mk_lr %mk_lg %mk_lb",
            "%mk_icb = OpConstant %float " + f(1 / 1.8556), "%mk_icr = OpConstant %float " + f(1 / 1.5748),
            "%mk_kb = OpConstant %float " + f(kb), "%mk_kr = OpConstant %float " + f(kr),
            "%mk_key = OpConstantComposite %v2float %mk_kb %mk_kr",
            "%mk_t0 = OpConstant %float " + f(thr), "%mk_t1 = OpConstant %float " + f(thr + soft),
            "%mk_t3 = OpConstant %float " + f(thr + 3 * soft), "%mk_sp = OpConstant %float " + f(spill),
        ]
        code += [
            "%mk_r = OpCompositeExtract %float " + RGB + " 0",
            "%mk_g = OpCompositeExtract %float " + RGB + " 1",
            "%mk_b = OpCompositeExtract %float " + RGB + " 2",
            "%mk_y = OpDot %float " + RGB + " %mk_luma",
            "%mk_bmy = OpFSub %float %mk_b %mk_y",
            "%mk_cb = OpFMul %float %mk_bmy %mk_icb",
            "%mk_rmy = OpFSub %float %mk_r %mk_y",
            "%mk_cr = OpFMul %float %mk_rmy %mk_icr",
            "%mk_cbcr = OpCompositeConstruct %v2float %mk_cb %mk_cr",
            "%mk_d = OpExtInst %float %1 Distance %mk_cbcr %mk_key",
            "%mk_a = OpExtInst %float %1 SmoothStep %mk_t0 %mk_t1 %mk_d",
            # spill suppression near the key hue: g -> min(g, max(r, b))
            "%mk_ws = OpExtInst %float %1 SmoothStep %mk_t0 %mk_t3 %mk_d",
            "%mk_wi = OpFSub %float %mk_f1 %mk_ws",
            "%mk_w = OpFMul %float %mk_wi %mk_sp",
            "%mk_rb = OpExtInst %float %1 FMax %mk_r %mk_b",
            "%mk_gc = OpExtInst %float %1 FMin %mk_g %mk_rb",
            "%mk_g2 = OpExtInst %float %1 FMix %mk_g %mk_gc %mk_w",
            "%mk_ds = OpCompositeConstruct %v3float %mk_r %mk_g2 %mk_b",
            "%mk_rgb = OpVectorTimesScalar %v3float %mk_ds %mk_a",
        ]
    else:
        sys.exit("mode must be rect or chroma")

    code += [
        "%mk_o0 = OpCompositeExtract %float %mk_rgb 0",
        "%mk_o1 = OpCompositeExtract %float %mk_rgb 1",
        "%mk_o2 = OpCompositeExtract %float %mk_rgb 2",
        "%mk_out = OpCompositeConstruct %v4float %mk_o0 %mk_o1 %mk_o2 %mk_a",
        "OpStore %504 %mk_out",
    ]
    asm = asm.replace(FUNC, "\n".join(consts) + "\n" + FUNC)
    asm = asm.replace(STORE, "\n".join(code))
    tmp = out + ".spvasm"
    open(tmp, "w").write(asm)
    subprocess.run(["spirv-as", "--target-env", "vulkan1.1", tmp, "-o", out], check=True)
    subprocess.run(["spirv-val", "--target-env", "vulkan1.1", out], check=True)
    print("wrote", out)


if __name__ == "__main__":
    main()
