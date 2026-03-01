#version 300 es
precision highp float;

in vec2 v_ndc;
out vec4 FragColor;

// Must match SceneData struct exactly (std140, binding 0)
layout(std140) uniform SceneBlock {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_viewProj;
    mat4 u_invViewProj;
    vec4 u_cameraPos;   // xyz = world pos
    vec4 u_lightDir;    // xyz = direction TOWARD sun (normalised)
    vec4 u_lightColor;  // xyz = sun rgb
    vec4 u_ambient;
};

uniform vec3 u_skyTop;      // zenith colour
uniform vec3 u_skyHorizon;  // horizon colour
uniform vec3 u_groundColor; // below-horizon colour

void main()
{
    // Unproject NDC → world direction
    vec4 worldFar = u_invViewProj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(worldFar.xyz / worldFar.w - u_cameraPos.xyz);

    float elevation = dir.y; // -1=down, 0=horizon, 1=up

    // Sky gradient
    vec3 sky;
    if (elevation >= 0.0)
        sky = mix(u_skyHorizon, u_skyTop, pow(elevation, 0.5));
    else
        sky = mix(u_skyHorizon, u_groundColor, pow(-elevation, 0.4));

    // Sun disk + halo
    vec3  sunDir = normalize(u_lightDir.xyz);
    float sd     = max(dot(dir, sunDir), 0.0);
    float disk   = pow(sd, 1024.0);          // sharp disk
    float halo   = pow(sd,    6.0) * 0.25;  // wide soft halo
    sky += u_lightColor.xyz * (disk + halo);

    FragColor = vec4(sky, 1.0);
}
