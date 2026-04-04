#version 300 es
precision highp float;

in vec2 v_uv;
in vec2 v_lm_uv;

uniform sampler2D u_albedo;
uniform sampler2D u_lightmap;
uniform vec3 u_color;
uniform int u_useLightmap;
uniform float u_lightmapMul;
uniform float u_lightmapGamma;

out vec4 out_color;

void main()
{
    vec4 albedoTex = texture(u_albedo, v_uv);
    vec3 color = albedoTex.rgb * u_color;

    if (u_useLightmap == 1)
    {
        vec3 lm = texture(u_lightmap, v_lm_uv).rgb;
        lm = max(lm * u_lightmapMul, vec3(0.0));
        lm = pow(lm, vec3(max(u_lightmapGamma, 0.0001)));
        color *= lm;
    }

    out_color = vec4(color, albedoTex.a);
}
