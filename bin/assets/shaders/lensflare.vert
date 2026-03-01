#version 330 core

// EffectVertex layout: position(vec3) + uv(vec2) + color(vec4)
// Position is already in NDC — no MVP transform.
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

out vec2 v_uv;
out vec4 v_color;

void main()
{
    v_uv        = a_uv;
    v_color     = a_color;
    // Position is NDC — emit directly
    gl_Position = vec4(a_position, 1.0);
}
