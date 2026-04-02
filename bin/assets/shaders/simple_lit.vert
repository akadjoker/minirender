#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out float v_clipDist;

uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform vec4 u_clipPlane;

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos    = worldPos.xyz;
    v_normal      = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_uv          = a_uv;
    // Clip plane: dot(worldPos, plane) — negative = discard in frag
    v_clipDist    = dot(worldPos, u_clipPlane);
    gl_Position   = u_viewProj * worldPos;
}
