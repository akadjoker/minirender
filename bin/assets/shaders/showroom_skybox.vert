#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;

uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_dir;

void main()
{
    v_dir = a_position;

    mat4 viewNoTranslation = mat4(mat3(u_view));
    vec4 clipPos = u_proj * viewNoTranslation * vec4(a_position, 1.0);
    // OpaquePass uses depth func GL_LESS. If z==w exactly (depth=1.0),
    // fragments can fail against the cleared depth (also 1.0), producing
    // triangle-shaped holes. Push depth slightly inside the far plane.
    gl_Position = vec4(clipPos.xy, clipPos.w * 0.9999, clipPos.w);
}
