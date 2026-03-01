#version 330 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

out vec4 FragColor;

uniform sampler2D u_albedo;
uniform vec3      u_lightDir;    // normalised, world space
uniform vec3      u_lightColor;
uniform vec3      u_ambientColor;

void main()
{
    vec4 albedo = texture(u_albedo, v_uv);
    if (albedo.a < 0.5)
        discard;

    vec3 norm    = normalize(v_normal);

    // Two-sided lighting so both faces of the cross-quad look lit
    float NdotL  = abs(dot(norm, u_lightDir));
    float diff   = max(NdotL, 0.1);

    // Darken at root (uv.y=1) — gives shadowed base look
    // uv.y=1 root → rootFactor=0.5, uv.y=0 tip → rootFactor=1.0
    float rootFactor = 0.5 + 0.5 * (1.0 - v_uv.y);

    vec3 color = albedo.rgb * (u_ambientColor + diff * u_lightColor) * rootFactor;

    FragColor  = vec4(color, 1.0);
}
