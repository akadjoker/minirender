#version 330 core

in vec2 v_uv;
out vec4 FragColor;

// ── GBuffer samplers (units 0-2) ───────────────────────────────
uniform sampler2D gPosition;    // unit 0 — world-space pos
uniform sampler2D gNormal;      // unit 1 — world-space normal
uniform sampler2D gAlbedoSpec;  // unit 2 — albedo.rgb + specular

// ── Camera ─────────────────────────────────────────────────────
uniform vec3 u_viewPos;

// ── Directional light ──────────────────────────────────────────
uniform vec3  u_dirLightDir;    // normalised direction TOWARD the light
uniform vec3  u_dirLightColor;  // (0,0,0) = disabled

// ── Point lights ───────────────────────────────────────────────
#define MAX_POINT_LIGHTS 16
uniform int   u_numPointLights;
uniform vec3  u_pointPos   [MAX_POINT_LIGHTS];
uniform vec3  u_pointColor [MAX_POINT_LIGHTS];
uniform float u_pointRadius[MAX_POINT_LIGHTS];

// ── Blinn-Phong shading ────────────────────────────────────────
vec3 blinnPhong(vec3 lightDir, vec3 lightColor,
                vec3 normal,   vec3 viewDir,
                vec3 albedo,   float spec)
{
    float NdotL   = max(dot(normal, lightDir), 0.0);
    vec3  halfVec = normalize(lightDir + viewDir);
    float specTerm = pow(max(dot(normal, halfVec), 0.0), 32.0) * spec;
    return (NdotL * albedo + specTerm) * lightColor;
}

void main()
{
    vec3  fragPos  = texture(gPosition,   v_uv).rgb;
    vec3  normal   = normalize(texture(gNormal, v_uv).rgb);
    vec4  albSpec  = texture(gAlbedoSpec, v_uv);
    vec3  albedo   = albSpec.rgb;
    float specular = albSpec.a;

    // Discard empty fragments (background — position == 0 and normal == 0)
    if (dot(normal, normal) < 0.01)
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 viewDir = normalize(u_viewPos - fragPos);
    vec3 result  = 0.05 * albedo; // ambient

    // Directional light
    if (dot(u_dirLightColor, u_dirLightColor) > 0.0)
        result += blinnPhong(u_dirLightDir, u_dirLightColor,
                             normal, viewDir, albedo, specular);

    // Point lights
    for (int i = 0; i < u_numPointLights; ++i)
    {
        vec3  toLight = u_pointPos[i] - fragPos;
        float dist    = length(toLight);
        float radius  = u_pointRadius[i];
        if (dist >= radius) continue;

        float atten   = clamp(1.0 - dist / radius, 0.0, 1.0);
        atten        *= atten; // quadratic falloff

        result += blinnPhong(toLight / dist, u_pointColor[i] * atten,
                             normal, viewDir, albedo, specular);
    }

    FragColor = vec4(result, 1.0);
}
