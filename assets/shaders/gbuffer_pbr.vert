// gbuffer_pbr.vert — GBuffer geometry pass with tangent support
// Use instead of gbuffer.vert when materials have normal maps.
// Output layout matches gbuffer_pbr.frag MRT slots.

#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;   // xyz=tangent, w=handedness
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec2 v_uv;
out mat3 v_TBN;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

void main()
{
    vec4 worldPos  = u_model * vec4(a_position, 1.0);
    v_worldPos     = worldPos.xyz;
    v_uv           = a_uv;

    mat3 normalMat = mat3(transpose(inverse(u_model)));
    vec3 N = normalize(normalMat * a_normal);
    vec3 T = normalize(normalMat * a_tangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * a_tangent.w;
    v_TBN = mat3(T, B, N);

    gl_Position = u_proj * u_view * worldPos;
}
