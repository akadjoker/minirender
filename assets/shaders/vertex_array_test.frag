#version 300 es
precision highp float;

in vec2 v_uv;
uniform vec3 a_color;

out vec4 out_color;

void main()
{
    out_color = vec4(a_color * vec3(v_uv, 1.0 - 0.5 * (v_uv.x + v_uv.y)), 1.0);
    //out_color = vec4( color, 1.0);
}
