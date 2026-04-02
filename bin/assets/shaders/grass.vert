#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

// Wind
uniform float   u_time;
uniform float   u_windStrength;  // default 0.3
uniform float   u_windSpeed;     // default 1.2
uniform vec2    u_windDir;       // normalised XZ direction

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);

    // uv.y=1 is root (bottom), uv.y=0 is tip (top) — invert so blade=0 anchors root
    float blade  = 1.0 - a_uv.y;                    // 0 = root (anchored), 1 = tip (max sway)
    float phase  = dot(worldPos.xz, u_windDir) * 0.5 + u_time * u_windSpeed;
    float sway   = sin(phase) * u_windStrength * blade;
    float sway2  = cos(phase * 0.7 + 1.3) * u_windStrength * 0.3 * blade;

    worldPos.x  += u_windDir.x * sway;
    worldPos.z  += u_windDir.y * sway;
    //worldPos.y  += sway2;                            // subtle vertical bounce

    v_worldPos  = worldPos.xyz;
    v_normal    = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_uv        = a_uv;

    gl_Position = u_proj * u_view * worldPos;
}
