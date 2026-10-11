#version 450
// Reference for the explicit-mask math, not an active compositor replacement.
// The caller supplies a same-frame R8_UNORM mask and opaque sampler borders.
// Actual SteamVR source UVs require validation on the supported headset build.
layout(set = 0, binding = 31) uniform sampler2D opacityMask;
layout(location = 0) in vec2 sourceUv;
layout(location = 1) in vec3 tintedRgb;
layout(location = 0) out vec4 outColor;

void main() {
    float alpha = textureLod(opacityMask, sourceUv, 0.0).r;
    outColor = vec4(tintedRgb * alpha, alpha);
}
