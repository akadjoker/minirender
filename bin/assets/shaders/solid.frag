#version 300 es
precision highp float;
precision highp int;

out vec4 FragColor;
in float v_clipDists[4];

uniform vec4 u_color;
uniform int u_clipPlaneCount;

void main()
{
    for (int i = 0; i < u_clipPlaneCount; ++i)
        if (v_clipDists[i] < 0.0)
            discard;
    FragColor = u_color;
}
