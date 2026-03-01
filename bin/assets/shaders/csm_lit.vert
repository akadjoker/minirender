#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_fragViewSpace;   // view-space position (w=1), .z used for cascade selection

// Scene-wide uniforms uploaded once per frame by SceneUBO.
layout(std140) uniform SceneBlock {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_viewProj;
    mat4 u_invViewProj;
    vec4 u_cameraPos;   // w=1
    vec4 u_lightDir;    // xyz = direction toward light, w=0
    vec4 u_lightColor;  // xyz = rgb, w = intensity
    vec4 u_ambient;     // xyz = color, w=0
};

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
