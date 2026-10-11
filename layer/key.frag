#version 450
// Chroma-key alpha pass.  Reads the pixel being shaded via an input attachment
// (same image, same pixel) and writes premultiplied rgb + alpha back.
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput src;
layout(push_constant) uniform PC {
    vec4 key;     // rgb key colour (sRGB 0..1), w = mode (1 rect, 2 chroma)
    vec4 params;  // threshold, softness, spill, invert
    vec4 rect;    // x0 y0 x1 y1 normalized
    vec4 size;    // width, height, layer, isSrgbFormat
} pc;
layout(location = 0) out vec4 outColor;

vec2 cbcr(vec3 c) { // BT.709, input in gamma (sRGB-ish) space
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return vec2((c.b - y) / 1.8556, (c.r - y) / 1.5748);
}
vec3 toGamma(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }
vec3 toLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }

void main() {
    vec4 c = subpassLoad(src);
    bool srgb = pc.size.w > 0.5;
    vec3 g = srgb ? toGamma(c.rgb) : c.rgb;   // key maths in gamma space
    float a;
    if (pc.key.w < 1.5) {
        vec2 uv = gl_FragCoord.xy / pc.size.xy;
        bool inside = all(greaterThanEqual(uv, pc.rect.xy)) && all(lessThan(uv, pc.rect.zw));
        if (pc.params.w > 0.5) inside = !inside;
        a = inside ? 0.0 : 1.0;
    } else {
        float d = distance(cbcr(g), cbcr(pc.key.rgb));
        a = smoothstep(pc.params.x, pc.params.x + max(pc.params.y, 1e-4), d);
        // Spill suppression near the key hue: clamp green to max(r,b).
        float w = pc.params.z * (1.0 - smoothstep(pc.params.x, pc.params.x + 3.0 * max(pc.params.y, 1e-4), d));
        g.g = mix(g.g, min(g.g, max(g.r, g.b)), w);
    }
    vec3 rgb = srgb ? toLinear(g) : g;
    outColor = vec4(rgb * a, a); // premultiplied: keyed pixels become (0,0,0,0)
}
