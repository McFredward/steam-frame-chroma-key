#version 450
// Owned raster input for an offscreen fragment-only diagnostic. These are not
// native reprojection coordinates or a reconstruction of Valve's vertex stage.
layout(push_constant) uniform FixtureInput { vec4 u; } fixture;
layout(location=0) noperspective out vec4 unused0;
layout(location=1) noperspective out vec4 originalInput;
layout(location=2) noperspective out vec4 unused2;
layout(location=3) flat out uint unused3;
void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
    originalInput = fixture.u;
    unused0 = vec4(0.125, 0.375, 0.625, 0.875);
    unused2 = vec4(0.75, 0.5, 0.25, 1.0);
    unused3 = 0u;
}
