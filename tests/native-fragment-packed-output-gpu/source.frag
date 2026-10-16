#version 450
// Owned offscreen transparency/additive producer; no native shader is embedded.
// Source-over pipelines use straight RGB with SRC_ALPHA blending. The emission
// pipeline uses ONE/ONE RGB and ZERO/ONE alpha: nonzero RGB with alpha0 survives.
layout(constant_id=0) const float red = 0.0;
layout(constant_id=1) const float green = 0.0;
layout(constant_id=2) const float blue = 0.0;
layout(constant_id=3) const float coverage = 0.0;
layout(location=0) out vec4 outputColor;
void main() { outputColor = vec4(red, green, blue, coverage); }
