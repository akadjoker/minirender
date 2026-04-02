#version 300 es
precision highp float;

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in float v_clipDists[4];

out vec4 FragColor;

uniform vec4 u_cameraPos;
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_ambient;
uniform int  u_clipPlaneCount;

uniform sampler2D u_albedo;

void main()
{
    for (int i = 0; i < u_clipPlaneCount; i++)
        if (v_clipDists[i] < 0.0) discard;

    vec4  albedo  = texture(u_albedo, v_uv);
    vec3  N       = normalize(v_normal);

    float NdotL   = max(dot(N, u_lightDir.xyz), 0.0);
    vec3  diffuse = NdotL * u_lightColor.rgb * albedo.rgb;
    vec3  ambient = u_ambient.rgb * albedo.rgb;

    // Simple specular
    vec3  V       = normalize(u_cameraPos.xyz - v_worldPos);
    vec3  H       = normalize(u_lightDir.xyz + V);
    float spec    = pow(max(dot(N, H), 0.0), 32.0) * 0.3;
    vec3  color   = ambient + diffuse + u_lightColor.rgb * spec;

    FragColor = vec4(color, albedo.a);
}
