#version 300 es
precision highp float;

// ── Inputs ────────────────────────────────────────────────────
in vec3  v_worldPos;
in vec2  v_uv;
in mat3  v_TBN;
in vec4  v_fragViewSpace;
in float v_clipDists[4];

out vec4 FragColor;

// ── Clip planes ───────────────────────────────────────────────
uniform int u_clipPlaneCount;

// ── Camera ────────────────────────────────────────────────────
uniform vec4 u_cameraPos;

// ── PBR Textures (all optional — fall back to uniform values) ─
uniform sampler2D u_albedo;       // unit 0 — sRGB
uniform sampler2D u_normal;       // unit 1 — tangent-space normal map
uniform sampler2D u_orm;          // unit 2 — occlusion(r) roughness(g) metallic(b)
uniform sampler2D u_emissive;     // unit 3 — emissive colour

// ── PBR scalar fallbacks (used when texture not bound) ────────
uniform vec4  u_albedoColor;      // default (1,1,1,1)
uniform float u_roughness;        // default 0.5
uniform float u_metallic;         // default 0.0
uniform float u_emissiveStrength; // default 0.0
uniform vec3  u_emissiveColor;    // default (1,1,1)
uniform int   u_hasNormalMap;     // 1 = sample u_normal, 0 = use TBN[2]
uniform int   u_hasORM;           // 1 = sample u_orm,    0 = use scalars
uniform int   u_hasEmissive;      // 1 = sample u_emissive

// ── Directional light + ambient ───────────────────────────────
uniform vec4 u_lightDir;          // world-space, pointing TOWARD light (w=0)
uniform vec4 u_lightColor;
uniform vec4 u_ambient;

// ── Point lights ──────────────────────────────────────────────
#define MAX_POINT_LIGHTS 16
uniform int   u_numPointLights;
uniform vec3  u_pointPos   [MAX_POINT_LIGHTS];
uniform vec3  u_pointColor [MAX_POINT_LIGHTS];
uniform float u_pointRadius[MAX_POINT_LIGHTS];

// ── CSM Shadows ───────────────────────────────────────────────
#define NUM_CASCADES 4
uniform sampler2D u_shadowMap[NUM_CASCADES];
uniform mat4      u_lightSpace[NUM_CASCADES];
uniform float     u_cascadeSplits[NUM_CASCADES];
uniform vec2      u_shadowMapSize;
uniform int       u_receiveShadow;  // 0 = disable shadows

// ─────────────────────────────────────────────────────────────
//  PBR functions — GGX / Cook-Torrance
// ─────────────────────────────────────────────────────────────

const float PI = 3.14159265359;

// Normal Distribution Function — GGX/Trowbridge-Reitz
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Geometry — Schlick-GGX (single term)
float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Geometry — Smith (view + light)
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel — Schlick approximation
vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance BRDF for a single light
vec3 cookTorrance(vec3 N, vec3 V, vec3 L, vec3 lightColor,
                  vec3 albedo, float roughness, float metallic, float shadow)
{
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // Base reflectance: dielectric F0=0.04, metallic=albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    vec3  F = F_Schlick(HdotV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    // Diffuse: Lambertian (metals have no diffuse)
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * NdotL * (1.0 - shadow);
}

// ── Shadow helpers (same as csm_lit.frag) ────────────────────

float sampleShadow(int c, vec2 uv)
{
    if      (c == 0) return texture(u_shadowMap[0], uv).r;
    else if (c == 1) return texture(u_shadowMap[1], uv).r;
    else if (c == 2) return texture(u_shadowMap[2], uv).r;
    else             return texture(u_shadowMap[3], uv).r;
}

const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624,-0.39906216), vec2( 0.94558609,-0.76890725),
    vec2(-0.09418410,-0.92938870), vec2( 0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232,-0.87912464),
    vec2(-0.38277543, 0.27676845), vec2( 0.97484398, 0.75648379),
    vec2( 0.44323325,-0.97511554), vec2( 0.53742981,-0.47373420),
    vec2(-0.26496911,-0.41893023), vec2( 0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2( 0.19984126, 0.78641367), vec2( 0.14383161,-0.14100790)
);

float shadowPCF(int c, vec4 fragLS)
{
    vec3 proj = fragLS.xyz / fragLS.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float bias   = 0.0003;
    vec2  texel  = 1.0 / u_shadowMapSize;
    float shadow = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        float d = sampleShadow(c, proj.xy + poissonDisk[i] * texel * 0.68);
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

float cascadeShadow()
{
    if (u_receiveShadow == 0) return 0.0;
    float depth = abs(v_fragViewSpace.z);
    int   ci    = NUM_CASCADES - 1;
    for (int i = 0; i < NUM_CASCADES; ++i)
        if (depth < u_cascadeSplits[i]) { ci = i; break; }
    vec4 fragLS = u_lightSpace[ci] * vec4(v_worldPos, 1.0);
    return shadowPCF(ci, fragLS);
}

// ─────────────────────────────────────────────────────────────
void main()
{
    // Clip planes
    for (int i = 0; i < u_clipPlaneCount; i++)
        if (v_clipDists[i] < 0.0) discard;

    // ── Sample textures ──────────────────────────────────────
    vec4 albedoSample = texture(u_albedo, v_uv);
    vec3 albedo = albedoSample.rgb * u_albedoColor.rgb;
    float alpha = albedoSample.a * u_albedoColor.a;

    // sRGB → linear (textures loaded as sRGB should be converted here)
    albedo = pow(albedo, vec3(2.2));

    float roughness, metallic, occlusion;
    if (u_hasORM == 1)
    {
        vec3 orm = texture(u_orm, v_uv).rgb;
        occlusion = orm.r;
        roughness = orm.g;
        metallic  = orm.b;
    }
    else
    {
        occlusion = 1.0;
        roughness = u_roughness;
        metallic  = u_metallic;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // ── Normal ───────────────────────────────────────────────
    vec3 N;
    if (u_hasNormalMap == 1)
    {
        vec3 tn = texture(u_normal, v_uv).xyz * 2.0 - 1.0;
        N = normalize(v_TBN * tn);
    }
    else
    {
        N = normalize(v_TBN[2]); // geometry normal
    }

    vec3 V = normalize(u_cameraPos.xyz - v_worldPos);

    // ── Directional light + CSM shadow ───────────────────────
    float shadow = cascadeShadow();
    vec3 Lo = cookTorrance(N, V, u_lightDir.xyz, u_lightColor.rgb,
                           albedo, roughness, metallic, shadow);

    // ── Point lights (no shadow) ─────────────────────────────
    for (int i = 0; i < u_numPointLights; i++)
    {
        vec3  toLight = u_pointPos[i] - v_worldPos;
        float dist    = length(toLight);
        float radius  = u_pointRadius[i];
        if (dist >= radius) continue;

        float atten = clamp(1.0 - (dist / radius), 0.0, 1.0);
        atten *= atten;

        vec3 L = normalize(toLight);
        Lo += cookTorrance(N, V, L, u_pointColor[i],
                           albedo, roughness, metallic, 0.0) * atten;
    }

    // ── Ambient (occlusion-weighted) ─────────────────────────
    vec3 F0      = mix(vec3(0.04), albedo, metallic);
    vec3 kS      = F_Schlick(max(dot(N, V), 0.0), F0);
    vec3 kD      = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 ambient = (kD * albedo * u_ambient.rgb) * occlusion;

    // ── Emissive ─────────────────────────────────────────────
    vec3 emissive = vec3(0.0);
    if (u_hasEmissive == 1)
        emissive = texture(u_emissive, v_uv).rgb * u_emissiveColor * u_emissiveStrength;
    else
        emissive = u_emissiveColor * u_emissiveStrength;

    vec3 color = ambient + Lo + emissive;

    // Reinhard tone map + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, alpha);
}
