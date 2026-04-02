#version 300 es
precision highp float;

out vec4 FragColor;

in vec4 ClipSpace;
in vec2 TexCoord;
in vec3 ToCameraVector;
in vec3 WorldPos;
in vec3 v_normal;
in vec2 v_bumpMapTexCoord;

uniform sampler2D u_reflection;
uniform sampler2D u_refraction;
uniform sampler2D u_refrDepth;
uniform sampler2D u_waterBump;
uniform sampler2D u_foamTexture;

uniform float u_distortStrength;
uniform float u_waveHeight;
uniform float u_reflectivity;
uniform vec4  u_waterColor;
uniform float u_colorBlendFactor;
uniform float u_time;
uniform float u_depthMult;
uniform float u_foamRange;
uniform float u_foamScale;
uniform float u_foamSpeed;
uniform float u_foamIntensity;
uniform vec2  u_windDirection;

uniform vec4  u_cameraPos;
uniform vec4  u_lightDir;
uniform vec4  u_lightColor;

const float foamCutoff = 0.5;

// Linearise a depth buffer value to view-space distance
float linearDepth(float rawDepth)
{
    const float near = 0.1;
    const float far  = 1000.0;
    float z = rawDepth * 2.0 - 1.0;
    return (2.0 * near * far) / (far + near - z * (far - near));
}

uniform float u_near;
uniform float u_far;

void main()
{
    float distToCamera = length(u_cameraPos.xyz - WorldPos);
    float lodFactor    = smoothstep(50.0, 100.0, distToCamera);

    // ── Bump distortion ──────────────────────────────────────────
    // Fade distortion to zero beyond 40 units — at distance the tiled
    // bump UV (aTexCoord / waveLength) creates visible banding in NDC space.
    float distortFade  = 1.0 - smoothstep(20.0, 40.0, distToCamera);
    vec2 perturbation = (texture(u_waterBump, v_bumpMapTexCoord).rg - 0.5)
                       * u_distortStrength * distortFade;

    // ── NDC coords ───────────────────────────────────────────────
    vec2 ndc             = (ClipSpace.xy / ClipSpace.w) * 0.5 + 0.5;
    vec2 reflectTexCoords = vec2(ndc.x, 1.0 - ndc.y);
    vec2 refractTexCoords = vec2(ndc.x,         ndc.y);

    // ── Depth (raw linearisation, same as old code) ──────────────
    float near = u_near;
    float far  = u_far;

    float rawFloor   = texture(u_refrDepth, refractTexCoords).r;
    float floorDist  = 2.0 * near * far / (far + near - (2.0 * rawFloor  - 1.0) * (far - near));
    float waterDist  = 2.0 * near * far / (far + near - (2.0 * gl_FragCoord.z - 1.0) * (far - near));
    float waterDepth = floorDist - waterDist;

    // Depth precision degrades at distance — allow a distance-scaled tolerance
    // so the oscillating-positive/negative bands don't cause stripe discards.
    float depthTolerance = mix(0.05, 1.5, clamp(distToCamera / 200.0, 0.0, 1.0));
    //if (waterDepth < -depthTolerance) discard;

    // ── Apply distortion ─────────────────────────────────────────
    reflectTexCoords = clamp(reflectTexCoords + perturbation, 0.001, 0.999);
    refractTexCoords = clamp(refractTexCoords + perturbation, 0.001, 0.999);

    vec4 reflColor = texture(u_reflection, reflectTexCoords);
    vec4 refrColor = texture(u_refraction, refractTexCoords);

    // ── Fresnel ──────────────────────────────────────────────────
    vec3  V = normalize(ToCameraVector);
    vec3  N = normalize(v_normal);
    // Use world-up for Fresnel — wave normals oscillate at wave frequency,
    // which creates visible stripes when fed into Fresnel.
    float VdotUp     = clamp(dot(V, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
    float fresnelTerm = pow(1.0 - VdotUp, 3.0);
    fresnelTerm       = mix(0.02, 0.9, fresnelTerm);

    // Depth tint removed — waterDepth is unreliable at tile boundaries and
    // produces geometric patterns. Water color comes from colorBlendFactor below.
    // float depthFactor = ...

    vec4 combinedColor = refrColor * (1.0 - fresnelTerm) + reflColor * fresnelTerm;
    vec4 finalColor    = u_colorBlendFactor * u_waterColor + (1.0 - u_colorBlendFactor) * combinedColor;

    // ── Specular highlight ───────────────────────────────────────
    vec3  L    = normalize(u_lightDir.xyz);
    vec3  H    = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 256.0) * 0.8;
    finalColor.rgb += u_lightColor.rgb * spec;

    // ── Foam system ──────────────────────────────────────────────
    vec2  foamUV1     = WorldPos.xz * u_foamScale + vec2(u_time * u_foamSpeed,             u_time * u_foamSpeed * 0.6);
    vec2  foamUV2     = WorldPos.xz * u_foamScale * 0.7 - vec2(u_time * u_foamSpeed * 0.8, u_time * u_foamSpeed);
    float foamPattern = (texture(u_foamTexture, foamUV1).r + texture(u_foamTexture, foamUV2).r) * 0.5;

    // Shore foam: only within 15 units of camera where depth precision is reliable.
    // foamPattern masks it so it's broken up, not a solid band.
    float shoreReliable = 1.0 - smoothstep(8.0, 15.0, distToCamera);
    float safeDepth     = max(waterDepth, 0.0);
    float shoreFoam     = smoothstep(u_foamRange, 0.0, safeDepth)
                        * smoothstep(0.3, 0.6, foamPattern)
                        * shoreReliable;

    // Wave crest foam (normal-based, safe at any distance)
    float crestFoam  = smoothstep(0.4, 0.8, 1.0 - v_normal.y) * 0.5
                     * smoothstep(0.4, 0.6, foamPattern);

    float foamFactor = max(shoreFoam, crestFoam)
                     * u_foamIntensity
                     * (1.0 - lodFactor * 0.7);

    finalColor.rgb = mix(finalColor.rgb, vec3(0.95, 0.98, 1.0), foamFactor);

    FragColor = vec4(finalColor.rgb, 1.0);
}
