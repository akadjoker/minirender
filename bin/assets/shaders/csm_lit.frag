#version 330 core#version 300 es













































































}    FragColor = vec4(color, albedo.a);    vec3 color    = ambient + (1.0 - shadow) * diffuse;    vec3 diffuse  = NdotL * u_lightColor * albedo.rgb;    vec3 ambient  = 0.15 * albedo.rgb;    float shadow  = sampleCascade(cascade, bias);        if (v_viewDepth < u_cascadeFarPlanes[i]) { cascade = i; break; }    for (int i = 0; i < u_numCascades - 1; i++)    int cascade  = u_numCascades - 1;    // Select cascade based on view depth    float bias   = max(u_shadowBias * (1.0 - NdotL), u_shadowBias * 0.1);    float NdotL  = max(dot(normal, u_lightDir), 0.0);    vec3  normal = normalize(v_normal);    vec4  albedo = texture(u_albedo, v_uv);{void main()}    else             return shadowPCF(u_shadowMap3, lsPos, bias);    else if (c == 2) return shadowPCF(u_shadowMap2, lsPos, bias);    else if (c == 1) return shadowPCF(u_shadowMap1, lsPos, bias);    if      (c == 0) return shadowPCF(u_shadowMap0, lsPos, bias);    else             lsPos = u_lightSpaceMatrices[3] * vec4(v_worldPos, 1.0);    else if (c == 2) lsPos = u_lightSpaceMatrices[2] * vec4(v_worldPos, 1.0);    else if (c == 1) lsPos = u_lightSpaceMatrices[1] * vec4(v_worldPos, 1.0);    if      (c == 0) lsPos = u_lightSpaceMatrices[0] * vec4(v_worldPos, 1.0);    vec4 lsPos;{float sampleCascade(int c, float bias)}    return shadow / 9.0;    }        shadow += (proj.z - bias > d) ? 1.0 : 0.0;        float d = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;    {    for (int y = -1; y <= 1; y++)    for (int x = -1; x <= 1; x++)    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0));    float shadow    = 0.0;    if (proj.z > 1.0) return 0.0;    proj = proj * 0.5 + 0.5;    vec3 proj = lsPos.xyz / lsPos.w;{float shadowPCF(sampler2D shadowMap, vec4 lsPos, float bias)// PCF 3x3uniform float u_shadowBias;uniform vec3  u_lightColor;uniform vec3  u_lightDir;    // normalised, pointing TOWARDS the lightuniform int   u_numCascades;uniform float u_cascadeFarPlanes[4];uniform mat4  u_lightSpaceMatrices[4];uniform sampler2D u_shadowMap3;uniform sampler2D u_shadowMap2;uniform sampler2D u_shadowMap1;uniform sampler2D u_shadowMap0;// One shadow map per cascade (up to 4)uniform sampler2D u_albedo;out vec4 FragColor;in float v_viewDepth;in vec2  v_uv;in vec3  v_normal;in vec3  v_worldPos;precision highp float;

#define NUM_CASCADES 4

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_fragViewSpace;

out vec4 FragColor;

uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_ambient;

uniform sampler2D u_shadowMap[NUM_CASCADES];
uniform mat4      u_lightSpace[NUM_CASCADES];
uniform float     u_cascadeSplits[NUM_CASCADES];
uniform vec2      u_shadowMapSize;

uniform sampler2D u_albedo;
uniform bool      u_showCascades;

// if/else dispatch — avoids dynamic sampler indexing issues on some drivers
float sampleShadow(int c, vec2 uv)
{
    if      (c == 0) return texture(u_shadowMap[0], uv).r;
    else if (c == 1) return texture(u_shadowMap[1], uv).r;
    else if (c == 2) return texture(u_shadowMap[2], uv).r;
    else             return texture(u_shadowMap[3], uv).r;
}

// Poisson disk — near cascades (soft shadows)
const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

// Front-face culling on the depth pass eliminates self-shadowing.
// We only need a tiny slope bias for grazing-angle light on back faces.
float shadowDiskPCF(int c, vec4 fragLightSpace)
{
    vec3 proj = fragLightSpace.xyz / fragLightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float NdotL = max(dot(normalize(v_normal), u_lightDir.xyz), 0.0);
    float bias  = 0.0003 * (1.0 - NdotL);   // slope-only, no constant acne

    vec2  texel  = 1.0 / u_shadowMapSize;
    float shadow = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        float d = sampleShadow(c, proj.xy + poissonDisk[i] * texel * 0.68);
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

// Grid PCF — far cascades (cheaper)
float shadowGridPCF(int c, vec4 fragLightSpace)
{
    vec3 proj = fragLightSpace.xyz / fragLightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float NdotL = max(dot(normalize(v_normal), u_lightDir.xyz), 0.0);
    float bias  = 0.0005 * (1.0 - NdotL);

    vec2  texel  = 1.0 / u_shadowMapSize;
    float shadow = 0.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
    {
        float d = sampleShadow(c, proj.xy + vec2(float(x), float(y)) * texel);
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 25.0;
}

float calcShadow(int c, vec4 fragLightSpace)
{
    if (c <= 1) return shadowDiskPCF(c, fragLightSpace);
    else        return shadowGridPCF(c, fragLightSpace);
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
    vec3 albedo = texture(u_albedo, v_uv).rgb;
    vec3 normal = normalize(v_normal);

    vec3  ambient = u_ambient.xyz * albedo;
    float NdotL   = max(dot(normal, u_lightDir.xyz), 0.0);
    vec3  diffuse = NdotL * u_lightColor.xyz * albedo;

    // Cascade selection by view-space depth
    float depth = abs(v_fragViewSpace.z);
    int   ci    = NUM_CASCADES - 1;
    for (int i = 0; i < NUM_CASCADES; ++i)
        if (depth < u_cascadeSplits[i]) { ci = i; break; }

    // Shadow with cascade blending at 85% of current cascade range
    float shadow     = 0.0;
    float blendStart = 0.85;

    if (ci < NUM_CASCADES - 1)
    {
        float cascDist   = u_cascadeSplits[ci];
        float blendRatio = smoothstep(cascDist * blendStart, cascDist, depth);
        if (blendRatio > 0.001)
        {
            float s1 = calcShadow(ci,     u_lightSpace[ci]     * vec4(v_worldPos, 1.0));
            float s2 = calcShadow(ci + 1, u_lightSpace[ci + 1] * vec4(v_worldPos, 1.0));
            shadow   = mix(s1, s2, blendRatio);
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

    vec3 color = ambient + (1.0 - shadow) * diffuse;

    if (u_showCascades)
        color *= cascadeDebugColor(ci);

    FragColor = vec4(color, 1.0);
}
