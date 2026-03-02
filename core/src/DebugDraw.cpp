#include "DebugDraw.hpp"
#include <SDL2/SDL.h>

// ─── Inline GLSL sources (GLES 3.0) ────────────────────────────────────────

static const char *kVS = R"(
#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 v_uv;
void main() {
    v_uv        = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Colour RGBA sampler
static const char *kFS_Color = R"(
#version 300 es
precision mediump float;
in  vec2      v_uv;
uniform sampler2D u_tex;
out vec4 FragColor;
void main() {
    FragColor = texture(u_tex, v_uv);
}
)";

// Depth sampler — linearised greyscale 
static const char *kFS_Depth = R"(
#version 300 es
precision mediump float;
in  vec2      v_uv;
uniform sampler2D u_tex;
out vec4 FragColor;
void main() {
    float d    = texture(u_tex, v_uv).r;
    // simple near/far linear remap for visualisation
    float near = 0.1;
    float far  = 1000.0;
    float lin  = (2.0 * near) / (far + near - d * (far - near));
    FragColor  = vec4(vec3(lin), 1.0);
}
)";

// ─── Singleton ──────────────────────────────────────────────────────────────

DebugDraw &DebugDraw::instance()
{
    static DebugDraw s;
    return s;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

GLuint DebugDraw::compileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[DebugDraw] shader compile error: %s", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint DebugDraw::buildProgram(const char *vsSrc, const char *fsSrc)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return 0; }

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char buf[512];
        glGetProgramInfoLog(p, sizeof(buf), nullptr, buf);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[DebugDraw] program link error: %s", buf);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// ─── init / release ─────────────────────────────────────────────────────────

bool DebugDraw::init()
{
    if (ready_) return true;

    prog_  = buildProgram(kVS, kFS_Color);
    progD_ = buildProgram(kVS, kFS_Depth);
    if (!prog_ || !progD_) return false;

    uTex_  = glGetUniformLocation(prog_,  "u_tex");
    uTexD_ = glGetUniformLocation(progD_, "u_tex");

    // VAO + VBO (6 vertices × {x,y,u,v}  — updated each drawQuad call)
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    ready_ = true;
    return true;
}

void DebugDraw::release()
{
    if (vbo_)   { glDeleteBuffers(1, &vbo_);       vbo_   = 0; }
    if (vao_)   { glDeleteVertexArrays(1, &vao_);  vao_   = 0; }
    if (prog_)  { glDeleteProgram(prog_);           prog_  = 0; }
    if (progD_) { glDeleteProgram(progD_);          progD_ = 0; }
    ready_ = false;
}

// ─── drawQuad ───────────────────────────────────────────────────────────────

void DebugDraw::drawQuad(int x, int y, int w, int h,
                         GLuint texId, int vpW, int vpH, bool isDepth)
{
    if (!ready_) { if (!init()) return; }
    if (!texId || vpW <= 0 || vpH <= 0) return;

    // Convert pixel coords (top-left origin) to NDC
    auto toNDC_x = [&](int px) { return  2.f * px / vpW - 1.f; };
    auto toNDC_y = [&](int py) { return -2.f * py / vpH + 1.f; };

    float x0 = toNDC_x(x),     y0 = toNDC_y(y);
    float x1 = toNDC_x(x + w), y1 = toNDC_y(y + h);

    // Two triangles (CCW), UV: (0,0)=top-left → (1,1)=bottom-right
    const float verts[6][4] = {
        {x0, y0,  0.f, 0.f},   // top-left
        {x0, y1,  0.f, 1.f},   // bottom-left
        {x1, y0,  1.f, 0.f},   // top-right
        {x1, y0,  1.f, 0.f},   // top-right
        {x0, y1,  0.f, 1.f},   // bottom-left
        {x1, y1,  1.f, 1.f},   // bottom-right
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Save/restore critical GL state
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blend     = glIsEnabled(GL_BLEND);
    GLboolean cull      = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    GLuint selectedProg = isDepth ? progD_ : prog_;
    GLint  selectedLoc  = isDepth ? uTexD_ : uTex_;

    glUseProgram(selectedProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    glUniform1i(selectedLoc, 0);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Restore state
    glUseProgram(prevProg);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend)     glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (cull)      glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
}
