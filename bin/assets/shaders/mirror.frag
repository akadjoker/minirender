#version 330 core
in vec4 v_clipPos;
in vec3 v_normal;
in vec3 v_worldPos;

out vec4 FragColor;

uniform sampler2D u_reflection;
uniform sampler2D u_refraction;
uniform vec3      u_cameraPos;
uniform float     u_distort;    // UV distortion strength (0 = none)

void main()
{
    // Project clip coords → [0,1] UV
    vec2 uv = (v_clipPos.xy / v_clipPos.w) * 0.5 + 0.5;

    // Distort using surface normal deviation
    vec2 offset = v_normal.xz * u_distort;

    // Reflection: horizontally flipped + distortion
    vec2 reflUV = vec2(1.0 - uv.x, uv.y) + offset;
    // Refraction: straight through + small distortion
    vec2 refrUV = uv + offset * 0.5;

    vec3 refl = texture(u_reflection, reflUV).rgb;
    vec3 refr = texture(u_refraction, refrUV).rgb;

    // Fresnel — more reflective at grazing angles
    vec3  viewDir = normalize(u_cameraPos - v_worldPos);
    float cosA    = max(dot(viewDir, v_normal), 0.0);
    float fresnel = mix(1.0, 0.05, pow(cosA, 3.0));   // 0=refractive, 1=reflective

    FragColor = vec4(mix(refr, refl, fresnel), 1.0);
}
