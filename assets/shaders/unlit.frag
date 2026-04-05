#version 300 es
precision highp float;

in vec2 v_uv;

uniform sampler2D u_albedo;
uniform vec3      u_color;

out vec4 out_color;

void main() {
    out_color = vec4(texture(u_albedo, v_uv).rgb * u_color, 1.0);
}
