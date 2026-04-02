#version 330 core

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_uv;

// ── MRT outputs ────────────────────────────────────────────────
layout(location = 0) out vec3 gPosition;    // world-space position
layout(location = 1) out vec3 gNormal;      // world-space normal
layout(location = 2) out vec4 gAlbedoSpec;  // albedo.rgb + specular

uniform sampler2D u_albedo;
uniform float     u_specular;   // default 0.5 — set per-material

void main()
{
    gPosition   = v_fragPos;
    gNormal     = normalize(v_normal);
    gAlbedoSpec = vec4(texture(u_albedo, v_uv).rgb, u_specular);
}
