#version 330 core

in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;

uniform sampler2D u_albedo;

void main()
{
    vec4 tex = texture(u_albedo, v_uv);
    // Additive blend — multiply by vertex colour for per-element tint
    FragColor = tex * v_color;
}
