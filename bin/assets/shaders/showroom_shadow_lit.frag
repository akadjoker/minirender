#version 300 es
precision highp float;
precision highp int;

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_lightSpacePos;
in float v_clipDists[4];

out vec4 FragColor;

uniform sampler2D u_albedo;
uniform sampler2D u_shadowMap;
uniform vec4      u_lightDir;
uniform vec4      u_lightColor;
uniform vec4      u_ambient;
uniform vec4      u_cameraPos;
uniform int       u_clipPlaneCount;

uniform vec3      u_albedoTint;
uniform float     u_specularStrength;
uniform float     u_opacity;
uniform int       u_receiveShadow;
uniform float     u_shadowBias;
uniform float     u_shadowFilterScale;

float shadowPCF(vec4 lightSpacePos, float bias)
{
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0)
        return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;

    vec2 texelSize = (1.0 / vec2(textureSize(u_shadowMap, 0))) * max(u_shadowFilterScale, 0.25);
    float shadow = 0.0;

    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
        float pcfDepth = texture(u_shadowMap, proj.xy + vec2(float(x), float(y)) * texelSize).r;
        shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
    }

    return shadow / 9.0;
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
    float bias = max(u_shadowBias * (1.0 - NdotL), u_shadowBias * 0.1);
    float shadow = (u_receiveShadow != 0) ? shadowPCF(v_lightSpacePos, bias) : 0.0;

    vec3 ambient = u_ambient.xyz * albedo;
    vec3 diffuse = NdotL * u_lightColor.xyz * albedo;

    vec3 halfVec = normalize(lightDir + viewDir);
    float specPow = mix(18.0, 72.0, clamp(u_specularStrength, 0.0, 1.0));
    float spec = pow(max(dot(normal, halfVec), 0.0), specPow) * u_specularStrength;
    vec3 specular = spec * u_lightColor.xyz;

    vec3 color = ambient + (1.0 - shadow) * (diffuse + specular);
    FragColor = vec4(color, opacity);
}
