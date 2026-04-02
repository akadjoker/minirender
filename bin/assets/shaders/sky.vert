#version 300 es
precision highp float;

out vec2 v_ndc;  // normalized device coordinates [-1, 1]

// Fullscreen triangle — covers the entire clip space in 3 vertices.
// z = w so depth resolves to 1.0 (far plane), rendering behind all geometry.
void main()
{
    vec2 pos[3];
    pos[0] = vec2(-1.0, -1.0);
    pos[1] = vec2( 3.0, -1.0);
    pos[2] = vec2(-1.0,  3.0);
    v_ndc       = pos[gl_VertexID];
    gl_Position = vec4(v_ndc, 1.0, 1.0);
}
