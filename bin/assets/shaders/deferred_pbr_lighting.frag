// deferred_pbr_lighting.frag — fullscreen PBR lighting pass
// Reads the 4-target PBR GBuffer written by gbuffer_pbr.frag.
// Bind GBuffer textures to units 0-3 before calling.

#version 330 core

in vec2 v_uv;
out vec4 FragColor;

// ── GBuffer (units 0-3) ────────────────────────────────────────
uniform sampler2D gPosition;  // unit 0
uniform sampler2D gNormal;    // unit 1
uniform sampler2D gAlbedo;    // unit 2 — rgb + alpha
uniform sampler2D gORM;       // unit 3 — occlusion, roughness, metallic

// ── Camera ─────────────────────────────────────────────────────
uniform vec3 u_viewPos;

// ── Directional light ──────────────────────────────────────────
uniform vec4 u_dirLightDir;    // toward light (w=0)
uniform vec4 u_dirLightColor;
uniform vec4 u_ambient;

// ── Point lights ───────────────────────────────────────────────
#define MAX_POINT_LIGHTS 16
uniform int   u_numPointLights;
uniform vec3  u_pointPos   [MAX_POINT_LIGHTS];
uniform vec3  u_pointColor [MAX_POINT_LIGHTS];
uniform float u_pointRadius[MAX_POINT_LIGHTS];

// ─────────────────────────────────────────────────────────────
//  PBR helpers
// ─────────────────────────────────────────────────────────────
const float PI = 3.14159265359;

float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 cookTorrance(vec3 N, vec3 V, vec3 L, vec3 lightColor,
                  vec3 albedo, float roughness, float metallic)
{
    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3  F0  = mix(vec3(0.04), albedo, metallic);
    float D   = D_GGX(NdotH, roughness);
    float G   = G_Smith(NdotV, NdotL, roughness);
    vec3  F   = F_Schlick(HdotV, F0);

    vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kD   = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec) * lightColor * NdotL;
}

// ─────────────────────────────────────────────────────────────
void main()
{
    vec3  fragPos  = texture(gPosition, v_uv).rgb;
    vec3  N        = normalize(texture(gNormal,  v_uv).rgb);
    vec4  albA     = texture(gAlbedo,   v_uv);
    vec3  albedo   = albA.rgb;
    vec3  orm      = texture(gORM,      v_uv).rgb;
    float occlusion = orm.r;
    float roughness = clamp(orm.g, 0.04, 1.0);
    float metallic  = orm.b;

    // Discard empty (sky/background) fragments
    if (dot(N, N) < 0.01)
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 V  = normalize(u_viewPos - fragPos);
    vec3 Lo = vec3(0.0);

    // Directional light
    if (dot(u_dirLightColor.rgb, u_dirLightColor.rgb) > 0.0)
        Lo += cookTorrance(N, V, u_dirLightDir.xyz, u_dirLightColor.rgb,
                           albedo, roughness, metallic);

    // Point lights
    for (int i = 0; i < u_numPointLights; ++i)
    {
        vec3  toLight = u_pointPos[i] - fragPos;
        float dist    = length(toLight);
        float radius  = u_pointRadius[i];
        if (dist >= radius) continue;
        float atten = clamp(1.0 - dist / radius, 0.0, 1.0);
        atten *= atten;
        Lo += cookTorrance(N, V, normalize(toLight), u_pointColor[i] * atten,
                           albedo, roughness, metallic);
    }

    // Ambient
    vec3 F0     = mix(vec3(0.04), albedo, metallic);
    vec3 kS     = F_Schlick(max(dot(N, V), 0.0), F0);
    vec3 kD     = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 ambient = kD * albedo * u_ambient.rgb * occlusion;

    vec3 color = ambient + Lo;

    // Reinhard tone map + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, albA.a);
}
