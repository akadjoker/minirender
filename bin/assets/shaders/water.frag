#version 300 es
precision highp float;
precision highp int;

out vec4 FragColor;

in vec4 ClipSpace;
in vec2 TexCoord;
in vec3 ToCameraVector;
in vec3 WorldPos;
in vec3 v_normal;
in vec2 v_bumpMapTexCoord;
in float v_clipDists[4];

uniform sampler2D u_reflection;
uniform sampler2D u_refraction;
uniform sampler2D u_refrDepth;
uniform sampler2D u_waterBump;
uniform sampler2D u_foamTexture;

uniform vec4 u_cameraPos;
uniform float u_waveHeight;
uniform vec4 u_waterColor;
uniform float u_colorBlendFactor;
uniform float u_time;
uniform float u_depthMult;
uniform float u_near;
uniform float u_far;
uniform float u_depthDiscardCutoff;
uniform int u_foamEnabled;

uniform float u_foamRange;
uniform float u_foamScale;
uniform float u_foamSpeed;
uniform float u_foamIntensity;

uniform int u_clipPlaneCount;

const float foamCutoff = 0.5;

void main()
{
    for (int i = 0; i < u_clipPlaneCount; ++i)
        if (v_clipDists[i] < 0.0)
            discard;

    float distToCamera = length(u_cameraPos.xyz - WorldPos);
    float lodFactor = smoothstep(50.0, 100.0, distToCamera);

    vec4 bumpColor = texture(u_waterBump, v_bumpMapTexCoord);
    vec2 perturbation = u_waveHeight * (bumpColor.rg - 0.5);
    perturbation *= (1.0 - lodFactor * 0.5);

    vec2 ndc = (ClipSpace.xy / ClipSpace.w) * 0.5 + 0.5;
    vec2 baseReflectTexCoords = vec2(ndc.x, 1.0 - ndc.y);
    vec2 baseRefractTexCoords = vec2(ndc.x, ndc.y);
    vec2 reflectTexCoords = baseReflectTexCoords;
    vec2 refractTexCoords = baseRefractTexCoords;

    float nearPlane = u_near;
    float farPlane = u_far;

    float depth = texture(u_refrDepth, baseRefractTexCoords).r;
    float floorDistance = 2.0 * nearPlane * farPlane /
        (farPlane + nearPlane - (2.0 * depth - 1.0) * (farPlane - nearPlane));

    depth = gl_FragCoord.z;
    float waterDistance = 2.0 * nearPlane * farPlane /
        (farPlane + nearPlane - (2.0 * depth - 1.0) * (farPlane - nearPlane));
    float waterDepth = max(floorDistance - waterDistance, 0.0);
    float normalizedDepth = clamp(max(waterDepth - u_depthDiscardCutoff, 0.0) / u_depthMult, 0.0, 1.0);

    perturbation *= clamp(normalizedDepth, 0.0, 1.0);
    reflectTexCoords = clamp(reflectTexCoords + perturbation, 0.001, 0.999);
    refractTexCoords = clamp(refractTexCoords + perturbation, 0.001, 0.999);

    float distortedDepth = texture(u_refrDepth, refractTexCoords).r;
    if (distortedDepth >= 0.9999)
        refractTexCoords = baseRefractTexCoords;

    vec4 reflectColor = texture(u_reflection, reflectTexCoords);
    vec4 refractColor = texture(u_refraction, refractTexCoords);

    vec3 eyeVector = normalize(u_cameraPos.xyz - WorldPos);
    vec3 upVector = vec3(0.0, 1.0, 0.0);
    float fresnelTerm = max(dot(eyeVector, upVector), 0.0);

    vec4 combinedColor = refractColor * fresnelTerm + reflectColor * (1.0 - fresnelTerm);
    vec4 finalColor = u_colorBlendFactor * u_waterColor + (1.0 - u_colorBlendFactor) * combinedColor;

    

    FragColor = finalColor;
}
