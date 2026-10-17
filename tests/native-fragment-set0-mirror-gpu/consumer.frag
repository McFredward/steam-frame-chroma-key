#version 450
// Owned follow-up shader: consume native Set0 plus both already-bound higher
// sets. A legal wrong Set0 storage descriptor has a clearly different result.
layout(set=0,binding=0) uniform Dynamic0 { vec4 value; } d0;
layout(set=0,binding=2) uniform Dynamic2 { vec4 value; } d2;
layout(set=0,binding=8,std430) readonly buffer NativeStorage { uint value[]; } s;
layout(set=1,binding=0) uniform Higher1 { vec4 value; } h1;
layout(set=2,binding=0) uniform Higher2 { vec4 value; } h2;
layout(location=0) out vec4 color;
void main() {
    color=vec4(d0.value.x,d2.value.y,h1.value.z,h2.value.w)
        +vec4(s.value[0]==0x1234abcdu?0.0:100.0);
}
