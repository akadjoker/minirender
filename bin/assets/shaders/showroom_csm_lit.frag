#version 300 es
precision highp float;
precision highp int;

#define NUM_CASCADES 4

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_fragViewSpace;
in float v_clipDists[4];

out vec4 FragColor;

uniform sampler2D u_albedo;
uniform sampler2D u_shadowMap[NUM_CASCADES];
uniform vec4      u_lightDir;
uniform vec4      u_lightColor;
uniform vec4      u_ambient;
uniform vec4      u_cameraPos;
uniform int       u_clipPlaneCount;

uniform mat4      u_lightSpace[NUM_CASCADES];
uniform float     u_cascadeSplits[NUM_CASCADES];
uniform vec2      u_shadowMapSize;
uniform vec3      u_albedoTint;
uniform float     u_specularStrength;
uniform float     u_opacity;
uniform int       u_receiveShadow;
uniform float     u_shadowBias;
uniform float     u_shadowFilterScale;
uniform bool      u_showCascades;

float sampleShadowMap(int cascadeIndex, vec2 uv)
{
    if (cascadeIndex == 0) return texture(u_shadowMap[0], uv).r;
    if (cascadeIndex == 1) return texture(u_shadowMap[1], uv).r;
    if (cascadeIndex == 2) return texture(u_shadowMap[2], uv).r;
    return texture(u_shadowMap[3], uv).r;
}

const vec2 kShadowKernel[4] = vec2[](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2(-0.5,  0.5),
    vec2( 0.5,  0.5)
);

float shadowPCF(int cascadeIndex, vec4 lightSpacePos, float bias)
{
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0)
        return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;

    vec2 texelSize = (1.0 / u_shadowMapSize) * max(u_shadowFilterScale, 0.25);
    float shadow = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        vec2 offset = kShadowKernel[i] * texelSize;
        float pcfDepth = sampleShadowMap(cascadeIndex, proj.xy + offset);
        shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
    }

    return shadow * 0.25;
}

vec3 cascadeDebugColor(int cascadeIndex)
{
    if (cascadeIndex == 0) return vec3(1.0, 0.35, 0.35);
    if (cascadeIndex == 1) return vec3(0.35, 1.0, 0.35);
    if (cascadeIndex == 2) return vec3(0.35, 0.55, 1.0);
    return vec3(1.0, 0.95, 0.35);
}

void main()
{
    for (int i = 0; i < u_clipPlaneCount; ++i)
        if (v_clipDists[i] < 0.0)
            discard;

    vec4 albedoTex = texture(u_albedo, v_uv);
    float opacity = clamp(albedoTex.a * u_opacity, 0.0, 1.0);
    if (opacity <= 0.02)
        discard;

    vec3 albedo = albedoTex.rgb * u_albedoTint;
    vec3 normal = normalize(v_normal);
    vec3 lightDir = normalize(u_lightDir.xyz);
    vec3 viewDir = normalize(u_cameraPos.xyz - v_worldPos);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float depth = abs(v_fragViewSpace.z);

    int cascadeIndex = NUM_CASCADES - 1;
    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        if (depth < u_cascadeSplits[i])
        {
            cascadeIndex = i;
            break;
        }
    }

    float bias = max(u_shadowBias * (1.0 - NdotL), u_shadowBias * 0.1);
    float shadow = 0.0;
    if (u_receiveShadow != 0)
    {
        if (cascadeIndex < NUM_CASCADES - 1)
        {
            float fadeStart = u_cascadeSplits[cascadeIndex] * 0.97;
            float blend = smoothstep(fadeStart, u_cascadeSplits[cascadeIndex], depth);
            float s0 = shadowPCF(cascadeIndex,     u_lightSpace[cascadeIndex]     * vec4(v_worldPos, 1.0), bias);
            float s1 = shadowPCF(cascadeIndex + 1, u_lightSpace[cascadeIndex + 1] * vec4(v_worldPos, 1.0), bias);
            shadow = mix(s0, s1, blend);
        }
        else
        {
            shadow = shadowPCF(cascadeIndex, u_lightSpace[cascadeIndex] * vec4(v_worldPos, 1.0), bias);
        }
    }

    vec3 ambient = u_ambient.xyz * albedo;
    vec3 diffuse = NdotL * u_lightColor.xyz * albedo;

    vec3 halfVec = normalize(lightDir + viewDir);
    float specPow = mix(18.0, 72.0, clamp(u_specularStrength, 0.0, 1.0));
    float spec = pow(max(dot(normal, halfVec), 0.0), specPow) * u_specularStrength;
    vec3 specular = spec * u_lightColor.xyz;

    vec3 color = ambient + (1.0 - shadow) * (diffuse + specular);
    if (u_showCascades)
        color *= cascadeDebugColor(cascadeIndex);

    FragColor = vec4(color, opacity);
}
