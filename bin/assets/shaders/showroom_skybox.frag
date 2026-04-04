#version 300 es
precision highp float;

in vec3 v_dir;
out vec4 FragColor;

uniform samplerCube u_skyCube;
uniform float u_skyExposure;

void main()
{
    // Force base LOD to avoid visible seams between cubemap faces
    // caused by derivative/mip transitions on cube edges.
    vec3 color = textureLod(u_skyCube, normalize(v_dir), 0.0).rgb;
    color *= max(u_skyExposure, 0.01);
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
