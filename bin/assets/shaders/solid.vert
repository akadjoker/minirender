#version 300 es
precision highp float;
precision highp int;
precision highp int;

layout(location = 0) in vec3 a_position;

out float v_clipDists[4];

uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform vec4 u_clipPlanes[4];
uniform int u_clipPlaneCount;

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    for (int i = 0; i < u_clipPlaneCount; ++i)
        v_clipDists[i] = dot(worldPos, u_clipPlanes[i]);
    gl_Position = u_viewProj * worldPos;
}
