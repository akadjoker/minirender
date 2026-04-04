#version 300 es
precision highp float;
precision highp int;

in vec3 v_worldPos;
in vec3 v_worldNormal;
in vec2 v_uv;
in float v_clipDists[4];

out vec4 FragColor;

uniform vec4 u_cameraPos;
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_ambient;

uniform sampler2D u_albedo;
uniform int u_hasAlbedo;
uniform vec3 u_albedoTint;

uniform samplerCube u_envMap;
uniform sampler2D u_planarReflection;
uniform float u_reflectivity;
uniform float u_roughness;
uniform float u_fresnelPower;
uniform float u_exposure;
uniform float u_opacity;
uniform int u_captureMode;
uniform int u_usePlanarReflection;
uniform int u_clipPlaneCount;
uniform int u_debugPlanarMode;
uniform mat4 u_reflectionViewProj;
uniform vec3 u_planarCenter;
uniform float u_planarRadius;
uniform float u_planarSoftness;
uniform float u_planarViewFalloff;

void main()
{
    for (int i = 0; i < u_clipPlaneCount; i++)
        if (v_clipDists[i] < 0.0) discard;

    vec3 N = normalize(v_worldNormal);
    vec3 V = normalize(u_cameraPos.xyz - v_worldPos);
    vec3 L = normalize(u_lightDir.xyz);

    vec3 albedo = u_albedoTint;
    float alpha = 1.0;
    if (u_hasAlbedo == 1)
    {
        vec4 tex = texture(u_albedo, v_uv);
        albedo *= tex.rgb;
        alpha = tex.a;
    }

    // Basic direct lighting
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo * (u_ambient.rgb + NdotL * u_lightColor.rgb);

    // Specular term
    vec3 H = normalize(V + L);
    float specExp = mix(64.0, 8.0, clamp(u_roughness, 0.0, 1.0));
    float spec = pow(max(dot(N, H), 0.0), specExp);
    vec3 specular = spec * u_lightColor.rgb;

    // Environment reflection
    vec3 base = diffuse + specular;
    vec3 color = base;

    if (u_captureMode == 0)
    {
        vec3 R = reflect(-V, N);
        vec3 cubeEnv = texture(u_envMap, R).rgb;
        vec3 env = cubeEnv;
        bool hasPlanar = false;
        if (u_usePlanarReflection == 1)
        {
            vec4 reflectionClip = u_reflectionViewProj * vec4(v_worldPos, 1.0);
            if (reflectionClip.w > 1e-5)
            {
                vec3 reflectionNdc = reflectionClip.xyz / reflectionClip.w;
                vec2 planarUV = reflectionNdc.xy * 0.5 + 0.5;
                planarUV.y = 1.0 - planarUV.y;

                if (u_debugPlanarMode == 1)
                {
                    FragColor = vec4(planarUV, 0.0, 1.0);
                    return;
                }
                if (u_debugPlanarMode == 2)
                {
                    FragColor = vec4(reflectionNdc * 0.5 + 0.5, 1.0);
                    return;
                }

                if (reflectionNdc.z >= -1.0 && reflectionNdc.z <= 1.0 &&
                    planarUV.x >= 0.001 && planarUV.x <= 0.999 &&
                    planarUV.y >= 0.001 && planarUV.y <= 0.999)
                {
                    vec3 planarEnv = texture(u_planarReflection, planarUV).rgb;
                    float distToCenter = distance(v_worldPos.xz, u_planarCenter.xz);
                    float areaMask = 1.0 - smoothstep(u_planarRadius,
                                                      u_planarRadius + max(u_planarSoftness, 1e-4),
                                                      distToCenter);
                    float NdotV = max(dot(N, V), 0.0);
                    float viewMask = mix(1.0, NdotV, clamp(u_planarViewFalloff, 0.0, 1.0));
                    float planarMask = areaMask * viewMask;
                    env = mix(cubeEnv, planarEnv, planarMask);
                    hasPlanar = planarMask > 0.001;
                }
            }
        }

        float fresnel = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0),
                            max(u_fresnelPower, 0.01));
        fresnel = mix(0.04, 1.0, fresnel);

        float reflectAmount = clamp(u_reflectivity * fresnel, 0.0, 1.0);
        if (hasPlanar)
            reflectAmount *= mix(0.35, 1.0, max(dot(N, V), 0.0));
        color = mix(base, env, reflectAmount);
    }

    // Rougher surfaces keep less sharp reflection feel
    color = mix(color, base, clamp(u_roughness * 0.55, 0.0, 1.0));

    // Tone map + gamma
    color *= max(u_exposure, 0.01);
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, alpha * u_opacity);
}
