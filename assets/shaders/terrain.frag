#version 300 es
precision highp float;
precision highp int;

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_uvDetail;
in vec4 v_lightSpacePos;
in float v_clipDists[4];

out vec4 FragColor;

uniform vec4 u_color;
uniform vec4 u_cameraPos;
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_ambient;
uniform sampler2D u_albedo;
uniform sampler2D u_detail;
uniform float u_detailBlend;
uniform sampler2D u_shadowMap;
uniform float u_shadowBias;
uniform int u_shadowEnabled;
uniform int u_clipPlaneCount;

float sampleShadow(vec4 lightSpacePos, float bias)
{
    if (u_shadowEnabled == 0)
        return 0.0;

    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0 ||
        proj.z > 1.0)
        return 0.0;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMap, 0));
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
        float depth = texture(u_shadowMap, proj.xy + vec2(x, y) * texelSize).r;
        shadow += (proj.z - bias > depth) ? 1.0 : 0.0;
    }
    return shadow / 9.0;
}

void main()
{
    for (int i = 0; i < u_clipPlaneCount; ++i)
        if (v_clipDists[i] < 0.0)
            discard;

    vec4 base = texture(u_albedo, v_uv);
    vec4 detail = texture(u_detail, v_uvDetail);
    vec3 mixed = mix(base.rgb, base.rgb * detail.rgb, u_detailBlend);
    vec4 albedo = vec4(mixed, base.a) * u_color;

    vec3 n = normalize(v_normal);
    float ndotl = max(dot(n, u_lightDir.xyz), 0.0);
    float shadow = sampleShadow(v_lightSpacePos, max(u_shadowBias * (1.0 - ndotl), u_shadowBias * 0.2));
    vec3 diffuse = ndotl * u_lightColor.rgb * albedo.rgb;
    vec3 ambient = u_ambient.rgb * albedo.rgb;
    vec3 viewDir = normalize(u_cameraPos.xyz - v_worldPos);
    vec3 halfDir = normalize(u_lightDir.xyz + viewDir);
    float spec = pow(max(dot(n, halfDir), 0.0), 24.0) * 0.15;
    vec3 color = ambient + (1.0 - shadow) * (diffuse + (u_lightColor.rgb * spec));
    FragColor = vec4(color, albedo.a);
}
