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

uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_ambient;
uniform int  u_clipPlaneCount;

uniform sampler2D u_shadowMap[NUM_CASCADES];
uniform mat4      u_lightSpace[NUM_CASCADES];
uniform float     u_cascadeSplits[NUM_CASCADES];
uniform float     u_cascadeTexelSize[NUM_CASCADES];
uniform vec2      u_shadowMapSize;

uniform sampler2D u_albedo;
uniform vec3      u_albedoTint;
uniform float     u_specularStrength;
uniform int       u_receiveShadow;
uniform float     u_opacity;
uniform float     u_shadowBiasMul;
uniform float     u_shadowFilterScale;
uniform bool      u_showCascades;

float sampleShadow(int c, vec2 uv)
{
    if      (c == 0) return texture(u_shadowMap[0], uv).r;
    else if (c == 1) return texture(u_shadowMap[1], uv).r;
    else if (c == 2) return texture(u_shadowMap[2], uv).r;
    else             return texture(u_shadowMap[3], uv).r;
}

const vec2 poissonDisk[8] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.38277543,  0.27676845),
    vec2( 0.44323325, -0.97511554), vec2( 0.19984126,  0.78641367)
);

float adaptiveBias(int c, float NdotL)
{
    float slope   = 1.0 - NdotL;
    float texelSz = u_cascadeTexelSize[c];
    float scale_  = max(u_shadowBiasMul, 0.1);
    float floor_  = texelSz * 0.0008 * scale_;
    float slope_  = texelSz * 0.0060 * slope * scale_;
    return floor_ + slope_;
}

float shadowDiskPCF(int c, vec4 fragLightSpace)
{
    vec3 proj = fragLightSpace.xyz / fragLightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float NdotL = max(dot(normalize(v_normal), normalize(u_lightDir.xyz)), 0.0);
    float bias  = adaptiveBias(c, NdotL);

    vec2 texel   = (1.0 / u_shadowMapSize) * max(u_shadowFilterScale, 0.25);
    float shadow = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        float d = sampleShadow(c, proj.xy + poissonDisk[i] * texel * 0.70);
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 8.0;
}

float shadowGridPCF(int c, vec4 fragLightSpace)
{
    vec3 proj = fragLightSpace.xyz / fragLightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float NdotL = max(dot(normalize(v_normal), normalize(u_lightDir.xyz)), 0.0);
    float bias  = adaptiveBias(c, NdotL);

    vec2 texel   = (1.0 / u_shadowMapSize) * max(u_shadowFilterScale, 0.25);
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
        float d = sampleShadow(c, proj.xy + vec2(float(x), float(y)) * texel);
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 9.0;
}

float calcShadow(int c, vec4 fragLightSpace)
{
    //if (c <= 1) 
    return shadowDiskPCF(c, fragLightSpace);
    //return shadowGridPCF(c, fragLightSpace);
}

vec3 cascadeDebugColor(int c)
{
    if      (c == 0) return vec3(1.0, 0.3, 0.3);
    else if (c == 1) return vec3(0.3, 1.0, 0.3);
    else if (c == 2) return vec3(0.3, 0.3, 1.0);
    else             return vec3(1.0, 1.0, 0.3);
}

void main()
{
    for (int i = 0; i < u_clipPlaneCount; i++)
        if (v_clipDists[i] < 0.0) discard;

    vec4 albedoTex = texture(u_albedo, v_uv);
    float opacity = clamp(albedoTex.a * u_opacity, 0.0, 1.0);
    if (opacity <= 0.02)
        discard;

    vec3 albedo  = albedoTex.rgb * u_albedoTint;
    vec3 normal  = normalize(v_normal);
    vec3 lightV  = normalize(u_lightDir.xyz);
    vec3 viewDir = normalize(-v_fragViewSpace.xyz);

    vec3 ambient = u_ambient.xyz * albedo;
    float NdotL  = max(dot(normal, lightV), 0.0);
    vec3 diffuse = NdotL * u_lightColor.xyz * albedo;

    float depth = abs(v_fragViewSpace.z);
    int ci = NUM_CASCADES - 1;
    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        if (depth < u_cascadeSplits[i])
        {
            ci = i;
            break;
        }
    }

    float shadow = 0.0;
    if (u_receiveShadow != 0)
    {
        float blendStart = 0.88;
        if (ci < NUM_CASCADES - 1)
        {
            float cascDist = u_cascadeSplits[ci];
            float blendRatio = smoothstep(cascDist * blendStart, cascDist, depth);
            if (blendRatio > 0.001)
            {
                float s1 = calcShadow(ci,     u_lightSpace[ci]     * vec4(v_worldPos, 1.0));
                float s2 = calcShadow(ci + 1, u_lightSpace[ci + 1] * vec4(v_worldPos, 1.0));
                shadow = mix(s1, s2, blendRatio);
            }
            else
            {
                shadow = calcShadow(ci, u_lightSpace[ci] * vec4(v_worldPos, 1.0));
            }
        }
        else
        {
            shadow = calcShadow(ci, u_lightSpace[ci] * vec4(v_worldPos, 1.0));
        }
    }

    vec3 halfVec = normalize(lightV + viewDir);
    float specPow = mix(18.0, 72.0, clamp(u_specularStrength, 0.0, 1.0));
    float spec = pow(max(dot(normal, halfVec), 0.0), specPow) * u_specularStrength;
    vec3 specular = spec * u_lightColor.xyz;

    vec3 color = ambient + (1.0 - shadow) * (diffuse + specular);

    if (u_showCascades)
        color *= cascadeDebugColor(ci);

    FragColor = vec4(color, opacity);
}
