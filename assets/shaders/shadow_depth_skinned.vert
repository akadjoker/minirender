#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 4) in ivec4 a_boneIds;
layout(location = 5) in vec4 a_boneWeights;

uniform mat4 u_model;
uniform mat4 u_lightViewProj;
uniform mat4 u_boneMatrices[100];

void main()
{
    mat4 skinMat = u_boneMatrices[a_boneIds.x] * a_boneWeights.x
                 + u_boneMatrices[a_boneIds.y] * a_boneWeights.y
                 + u_boneMatrices[a_boneIds.z] * a_boneWeights.z
                 + u_boneMatrices[a_boneIds.w] * a_boneWeights.w;

    gl_Position = u_lightViewProj * u_model * skinMat * vec4(a_position, 1.0);
}
