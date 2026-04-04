#version 300 es
precision highp float;

// Depth-only pass for instanced geometry (CSM shadow maps).
// Per-instance model matrix is split across 4 vec4 attributes at locations 6-9
// (set via InstanceBuffer::attachInstances). No u_model uniform needed.

layout(location = 0) in vec3 a_position;

// Per-instance mat4 (divisor = 1)
layout(location = 6) in vec4 a_instanceModel0;
layout(location = 7) in vec4 a_instanceModel1;
layout(location = 8) in vec4 a_instanceModel2;
layout(location = 9) in vec4 a_instanceModel3;

uniform mat4 u_lightSpace;

void main()
{
    mat4 model = mat4(a_instanceModel0,
                      a_instanceModel1,
                      a_instanceModel2,
                      a_instanceModel3);
    gl_Position = u_lightSpace * model * vec4(a_position, 1.0);
}
