#version 330 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_fragViewSpace;

out vec4 FragColor;

uniform sampler2D u_albedo;

uniform sampler2D u_shadowMap0;
uniform sampler2D u_shadowMap1;
uniform sampler2D u_shadowMap2;
uniform sampler2D u_shadowMap3;

uniform mat4  u_lightSpaceMatrices[4];
uniform float u_cascadeFarPlanes[4];
uniform int   u_numCascades;

uniform vec3  u_lightDir;
uniform vec3  u_lightColor;
uniform float u_shadowBias;

float shadowPCF(sampler2D shadowMap, vec4 lsPos, float bias)
{
    vec3 proj = lsPos.xyz / lsPos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;

    float shadow    = 0.0;
    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
    {
        float d = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 9.0;
}

void main()
{
    vec4  albedo = texture(u_albedo, v_uv);
    vec3  normal = normalize(v_normal);
    float NdotL  = max(dot(normal, u_lightDir), 0.0);
    float bias   = max(u_shadowBias * (1.0 - NdotL), u_shadowBias * 0.1);

    // Select cascade by view-space depth (positive in front of camera)
    float depth  = -v_fragViewSpace.z;
    int   cascade = u_numCascades - 1;
    for (int i = 0; i < u_numCascades - 1; i++)
        if (depth < u_cascadeFarPlanes[i]) { cascade = i; break; }

    float shadow = 0.0;
    if      (cascade == 0) shadow = shadowPCF(u_shadowMap0, u_lightSpaceMatrices[0] * vec4(v_worldPos, 1.0), bias);
    else if (cascade == 1) shadow = shadowPCF(u_shadowMap1, u_lightSpaceMatrices[1] * vec4(v_worldPos, 1.0), bias);
    else if (cascade == 2) shadow = shadowPCF(u_shadowMap2, u_lightSpaceMatrices[2] * vec4(v_worldPos, 1.0), bias);
    else                   shadow = shadowPCF(u_shadowMap3, u_lightSpaceMatrices[3] * vec4(v_worldPos, 1.0), bias);

    vec3 ambient = 0.15 * albedo.rgb;
    vec3 diffuse = NdotL * u_lightColor * albedo.rgb;
    vec3 color   = ambient + (1.0 - shadow) * diffuse;

    FragColor = vec4(color, albedo.a);
}
