#version 300 es
precision highp float;

in vec2 v_ndc;
out vec4 FragColor;

uniform mat4 u_invViewProj;
uniform vec4 u_cameraPos;
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;

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
