#version 330 core#version 300 es


























}    gl_Position = u_proj * viewPos;    v_viewDepth = -viewPos.z;   // positive = in front of camera    v_uv        = a_uv;    v_normal    = normalize(mat3(transpose(inverse(u_model))) * a_normal);    v_worldPos  = worldPos.xyz;    vec4 viewPos  = u_view  * worldPos;    vec4 worldPos = u_model * vec4(a_position, 1.0);{void main()uniform mat4 u_proj;uniform mat4 u_view;uniform mat4 u_model;out float v_viewDepth;   // positive depth in front of camera (view-space -z)out vec2  v_uv;out vec3  v_normal;out vec3  v_worldPos;layout(location = 3) in vec2 a_uv;layout(location = 1) in vec3 a_normal;layout(location = 0) in vec3 a_position;precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_fragViewSpace;   // view-space position (w=1), .z used for cascade selection

uniform mat4 u_view;
uniform mat4 u_proj;

uniform mat4 u_model;

void main()
{
    vec4 worldPos   = u_model * vec4(a_position, 1.0);
    v_worldPos      = worldPos.xyz;
    v_normal        = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_uv            = a_uv;
    v_fragViewSpace = u_view * worldPos;
    gl_Position     = u_proj * v_fragViewSpace;
}
