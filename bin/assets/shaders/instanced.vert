// instanced.vert — reads per-instance model matrix from locations 6-9
// Compatible with simple_lit.frag, pbr.frag, csm_lit.frag (any forward frag).
// The per-instance mat4 replaces u_model — u_model uniform is ignored here.

#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;

// Per-instance model matrix split into 4 vec4 columns (divisor=1)
layout(location = 6) in vec4 a_instanceModel0;
layout(location = 7) in vec4 a_instanceModel1;
layout(location = 8) in vec4 a_instanceModel2;
layout(location = 9) in vec4 a_instanceModel3;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out mat3 v_TBN;
out vec4 v_fragViewSpace;
out float v_clipDists[4];

uniform mat4 u_view;
uniform mat4 u_proj;
uniform vec4 u_clipPlanes[4];
uniform int  u_clipPlaneCount;

void main()
{
    mat4 model = mat4(a_instanceModel0,
                      a_instanceModel1,
                      a_instanceModel2,
                      a_instanceModel3);

    vec4 worldPos = model * vec4(a_position, 1.0);
    v_worldPos    = worldPos.xyz;
    v_uv          = a_uv;

    // Normal matrix — cofactor of upper-left 3x3 (avoids inverse for uniform scale)
    mat3 normalMat = transpose(inverse(mat3(model)));
    vec3 N = normalize(normalMat * a_normal);
    vec3 T = normalize(normalMat * a_tangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * a_tangent.w;
    v_TBN    = mat3(T, B, N);
    v_normal = N;  // for shaders that don't use TBN

    v_fragViewSpace = u_view * worldPos;

    for (int i = 0; i < u_clipPlaneCount; i++)
        v_clipDists[i] = dot(worldPos, u_clipPlanes[i]);

    gl_Position = u_proj * v_fragViewSpace;
}
