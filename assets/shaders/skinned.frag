#version 330 core

in  vec3 v_worldPos;
in  vec3 v_normal;
in  vec2 v_uv;
in  vec4 v_lightSpacePos;
in  float v_clipDists[4];

out vec4 FragColor;

uniform sampler2D u_albedo;
uniform sampler2D u_shadowMap;   // unit 1
uniform vec4      u_lightDir;    // normalised, world space, pointing TOWARDS light (w=0)
uniform vec4      u_lightColor;
uniform vec4      u_ambient;
uniform float     u_shadowBias;  // default 0.005
uniform int       u_clipPlaneCount;

// PCF 3×3 soft shadows
float shadowPCF(vec4 lightSpacePos, float bias)
{
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0)
        return 0.0;

    float shadow    = 0.0;
    vec2  texelSize = 1.0 / vec2(textureSize(u_shadowMap, 0));

    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
        float pcfDepth = texture(u_shadowMap, proj.xy + vec2(x, y) * texelSize).r;
        shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
    }
    return shadow / 9.0;
}

void main()
{
    for (int i = 0; i < u_clipPlaneCount; i++)
        if (v_clipDists[i] < 0.0) discard;

    vec4  albedo = texture(u_albedo, v_uv);
    vec3  normal = normalize(v_normal);

    float NdotL  = max(dot(normal, u_lightDir.xyz), 0.0);
    float bias   = max(u_shadowBias * (1.0 - NdotL), u_shadowBias * 0.1);
    float shadow = shadowPCF(v_lightSpacePos, bias);

    vec3 ambient = u_ambient.rgb * albedo.rgb;
    vec3 diffuse = NdotL * u_lightColor.rgb * albedo.rgb;
    vec3 color   = ambient + (1.0 - shadow) * diffuse;

    FragColor = vec4(color, albedo.a);
}
