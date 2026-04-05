#version 300 es
precision highp float;
precision highp int;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_lightSpacePos;
out float v_clipDists[4];

uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform mat4 u_lightSpace;
uniform vec4 u_clipPlanes[4];
uniform int u_clipPlaneCount;

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    v_normal = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_uv = a_uv;
    v_lightSpacePos = u_lightSpace * worldPos;
    for (int i = 0; i < u_clipPlaneCount; ++i)
        v_clipDists[i] = dot(worldPos, u_clipPlanes[i]);
    gl_Position = u_viewProj * worldPos;
}
