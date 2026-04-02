#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;   // xyz=tangent, w=handedness
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec2 v_uv;
out mat3 v_TBN;         // tangent-space → world-space matrix
out vec4 v_fragViewSpace;
out float v_clipDists[4];

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform vec4 u_clipPlanes[4];
uniform int  u_clipPlaneCount;

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos    = worldPos.xyz;
    v_uv          = a_uv;

    // Build TBN in world space
    mat3 normalMat = mat3(transpose(inverse(u_model)));
    vec3 N = normalize(normalMat * a_normal);
    vec3 T = normalize(normalMat * a_tangent.xyz);
    T = normalize(T - dot(T, N) * N); // re-orthogonalise
    vec3 B = cross(N, T) * a_tangent.w;
    v_TBN = mat3(T, B, N);

    v_fragViewSpace = u_view * worldPos;

    for (int i = 0; i < u_clipPlaneCount; i++)
        v_clipDists[i] = dot(worldPos, u_clipPlanes[i]);

    gl_Position = u_proj * v_fragViewSpace;
}
