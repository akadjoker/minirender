// gbuffer_pbr.frag — GBuffer geometry pass, PBR outputs
// MRT layout:
//   location 0 — gPosition     vec3 world-space pos
//   location 1 — gNormal       vec3 world-space normal
//   location 2 — gAlbedo       vec4 albedo.rgb + alpha
//   location 3 — gORM          vec3 occlusion, roughness, metallic
//
// Pair with deferred_pbr_lighting.frag for the lighting pass.

#version 330 core

in vec3 v_worldPos;
in vec2 v_uv;
in mat3 v_TBN;

layout(location = 0) out vec3 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gAlbedo;  // rgb + alpha
layout(location = 3) out vec3 gORM;     // occlusion, roughness, metallic

uniform sampler2D u_albedo;
uniform sampler2D u_normal;
uniform sampler2D u_orm;

uniform vec4  u_albedoColor;   // (1,1,1,1)
uniform float u_roughness;     // 0.5
uniform float u_metallic;      // 0.0
uniform int   u_hasNormalMap;
uniform int   u_hasORM;

void main()
{
    gPosition = v_worldPos;

    if (u_hasNormalMap == 1)
    {
        vec3 tn = texture(u_normal, v_uv).xyz * 2.0 - 1.0;
        gNormal = normalize(v_TBN * tn);
    }
    else
    {
        gNormal = normalize(v_TBN[2]);
    }

    vec4 albSample = texture(u_albedo, v_uv);
    gAlbedo = vec4(albSample.rgb * u_albedoColor.rgb, albSample.a * u_albedoColor.a);

    if (u_hasORM == 1)
    {
        gORM = texture(u_orm, v_uv).rgb;
    }
    else
    {
        gORM = vec3(1.0, u_roughness, u_metallic);
    }
}
