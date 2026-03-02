#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_fragViewSpace;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

void main()
{
    vec4 worldPos   = u_model * vec4(a_position, 1.0);
    v_worldPos      = worldPos.xyz;
    v_normal        = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_uv            = a_uv;
    v_fragViewSpace = u_view * worldPos;
    gl_Position     = u_proj * v_fragViewSpace;
}
