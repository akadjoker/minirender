#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normalMatrix;
uniform vec4 u_clipPlanes[4];
uniform int  u_clipPlaneCount;

out vec3 v_worldPos;
out vec3 v_worldNormal;
out vec2 v_uv;
out float v_clipDists[4];

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    v_worldNormal = normalize(u_normalMatrix * a_normal);
    v_uv = a_uv;
    for (int i = 0; i < u_clipPlaneCount; i++)
        v_clipDists[i] = dot(worldPos, u_clipPlanes[i]);

    gl_Position = u_proj * u_view * worldPos;
}
