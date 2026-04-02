#version 330 core
layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;
layout(location = 3) in vec2  a_uv;
layout(location = 4) in ivec4 a_boneIds;
layout(location = 5) in vec4  a_boneWeights;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_lightSpacePos;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat4 u_lightSpace;
uniform mat4 u_boneMatrices[100];

void main()
{
    // Accumulate skinning matrix from up to 4 bone influences
    mat4 skinMat = u_boneMatrices[a_boneIds.x] * a_boneWeights.x
                 + u_boneMatrices[a_boneIds.y] * a_boneWeights.y
                 + u_boneMatrices[a_boneIds.z] * a_boneWeights.z
                 + u_boneMatrices[a_boneIds.w] * a_boneWeights.w;

    vec4 worldPos   = u_model * skinMat * vec4(a_position, 1.0);
    v_worldPos      = worldPos.xyz;
    v_normal        = normalize(mat3(transpose(inverse(u_model * skinMat))) * a_normal);
    v_uv            = a_uv;
    v_lightSpacePos = u_lightSpace * worldPos;

    gl_Position = u_proj * u_view * worldPos;
}
