#version 450
layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 rgb;
layout(push_constant) uniform Color { vec4 value; } color;
void main() {
    vec2 point = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = point;
    rgb = color.value.rgb;
    gl_Position = vec4(point * 2.0 - 1.0, 0.0, 1.0);
}
