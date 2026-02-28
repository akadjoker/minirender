#version 330 core

// Fullscreen triangle — zero vertex data, driven by gl_VertexID
// Call with: glDrawArrays(GL_TRIANGLES, 0, 3)
//
//   ID 0 → (-1,-1)   ID 1 → ( 3,-1)   ID 2 → (-1, 3)
//   UV : (0,0)              (2,0)              (0,2)
//   (the triangle is twice as big as the screen — clips to NDC naturally)

out vec2 v_uv;

void main()
{
    vec2 pos    = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv        = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
