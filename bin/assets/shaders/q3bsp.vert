#version 300 es
precision highp float;

layout(location=0) in vec3 a_position;
layout(location=2) in vec4 a_tangent;
layout(location=3) in vec2 a_uv;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

out vec2 v_uv;
out vec2 v_lm_uv;

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    gl_Position = u_proj * u_view * worldPos;

    v_uv = a_uv;
    v_lm_uv = a_tangent.xy;
}
