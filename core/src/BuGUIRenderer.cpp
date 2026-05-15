#include "pch.h"
#include "BuGUIRenderer.hpp"
#include "Opengl.hpp"
#include "RenderState.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Shader helpers
// ─────────────────────────────────────────────────────────────────────────────
GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[BuGUIRenderer] shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuGUIRenderer
// ─────────────────────────────────────────────────────────────────────────────

bool BuGUIRenderer::init()
{
    return createDeviceObjects();
}

void BuGUIRenderer::shutdown()
{
    destroyDeviceObjects();
}

BuGUI::TextureHandle BuGUIRenderer::createTexture(int w, int h, const unsigned char* rgba)
{
    if (w <= 0 || h <= 0 || !rgba)
        return {};

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return BuGUI::TextureHandle{static_cast<uintptr_t>(tex)};
}

void BuGUIRenderer::destroyTexture(BuGUI::TextureHandle handle)
{
    if (!handle) return;
    GLuint id = static_cast<GLuint>(handle.value);
    glDeleteTextures(1, &id);
}

void BuGUIRenderer::render(const BuGUI::DrawData& drawData)
{
    if (drawData.passes.empty())                                          return;
    if (drawData.displayWidth <= 0.0f || drawData.displayHeight <= 0.0f) return;

    renderDrawData(drawData);
}

bool BuGUIRenderer::createDeviceObjects()
{
    // GLSL ES 3.0 — matches the GLES 3.x context created by Device.
    const char* vs = R"(#version 300 es
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aColor;
        layout(location = 2) in vec2 aUV;
        uniform vec2 uDisplaySize;
        uniform mat3 uCamera;
        out vec4 vColor;
        out vec2 vUV;
        void main()
        {
            vec2 pos = (uCamera * vec3(aPos, 1.0)).xy;
            vec2 p   = (pos / uDisplaySize) * 2.0 - 1.0;
            gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
            vColor = aColor;
            vUV    = aUV;
        }
    )";

    const char* fs = R"(#version 300 es
        precision mediump float;
        in vec4 vColor;
        in vec2 vUV;
        uniform sampler2D uTexture;
        uniform int       uUseTexture;
        out vec4 FragColor;
        void main()
        {
            vec4 texel = uUseTexture != 0 ? texture(uTexture, vUV) : vec4(1.0);
            FragColor  = vColor * texel;
        }
    )";

    GLuint vert = compileShader(GL_VERTEX_SHADER,   vs);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!vert || !frag) { glDeleteShader(vert); glDeleteShader(frag); return false; }

    shader_ = glCreateProgram();
    glAttachShader(shader_, vert);
    glAttachShader(shader_, frag);
    glLinkProgram(shader_);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(shader_, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetProgramInfoLog(shader_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[BuGUIRenderer] shader link error: %s\n", log);
        destroyDeviceObjects();
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER,         vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);

    // aPos  (vec2 float)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(BuGUI::DrawVertex),
                          reinterpret_cast<void*>(offsetof(BuGUI::DrawVertex, x)));
    // aColor (vec4 u8 normalised)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(BuGUI::DrawVertex),
                          reinterpret_cast<void*>(offsetof(BuGUI::DrawVertex, color)));
    // aUV (vec2 float)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(BuGUI::DrawVertex),
                          reinterpret_cast<void*>(offsetof(BuGUI::DrawVertex, u)));

    glBindVertexArray(0);
    return true;
}

void BuGUIRenderer::destroyDeviceObjects()
{
    if (ebo_)    { glDeleteBuffers(1,      &ebo_);    ebo_    = 0; }
    if (vbo_)    { glDeleteBuffers(1,      &vbo_);    vbo_    = 0; }
    if (vao_)    { glDeleteVertexArrays(1, &vao_);    vao_    = 0; }
    if (shader_) { glDeleteProgram(shader_);           shader_ = 0; }
}

void BuGUIRenderer::renderDrawData(const BuGUI::DrawData& drawData)
{
    auto& rs = RenderState::instance();

    // ── Set up render state via RenderState (cached, avoids redundant GL calls) ──
    rs.setBlend(true);
    rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    rs.setBlendEquation(GL_FUNC_ADD);
    rs.setDepthTest(false);
    rs.setCull(false);
    rs.setScissorTest(true);
    rs.useProgram(shader_);

    const GLint displayLoc    = glGetUniformLocation(shader_, "uDisplaySize");
    const GLint cameraLoc     = glGetUniformLocation(shader_, "uCamera");
    const GLint useTextureLoc = glGetUniformLocation(shader_, "uUseTexture");
    const GLint textureLoc    = glGetUniformLocation(shader_, "uTexture");
    glUniform2f(displayLoc, drawData.displayWidth, drawData.displayHeight);
    glUniform1i(textureLoc, 0);

    glBindVertexArray(vao_);

    for (const BuGUI::DrawPass& pass : drawData.passes)
    {
        if (!pass.list) continue;
        const auto& verts    = pass.list->vertices();
        const auto& idxs     = pass.list->indices();
        const auto& commands = pass.list->commands();
        if (verts.empty() || idxs.empty() || commands.empty()) continue;

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(BuGUI::DrawVertex)),
                     verts.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(idxs.size() * sizeof(uint32_t)),
                     idxs.data(), GL_STREAM_DRAW);

        const float pivX = pass.camera.pivotX * drawData.displayWidth;
        const float pivY = pass.camera.pivotY * drawData.displayHeight;
        const float c    = std::cos(pass.camera.angle) * pass.camera.scale;
        const float s    = std::sin(pass.camera.angle) * pass.camera.scale;
        const float ox   = pass.camera.x - pivX;
        const float oy   = pass.camera.y - pivY;
        const float tx   = c * ox - s * oy + pivX;
        const float ty   = s * ox + c * oy + pivY;
        const float mat[9] = { c, s, 0.0f, -s, c, 0.0f, tx, ty, 1.0f };
        glUniformMatrix3fv(cameraLoc, 1, GL_FALSE, mat);

        const bool noRot = (pass.camera.angle == 0.0f);

        for (const BuGUI::DrawCmd& cmd : commands)
        {
            const float scaleX = drawData.framebufferScaleX;
            const float scaleY = drawData.framebufferScaleY;
            const float fbW    = drawData.displayWidth  * scaleX;
            const float fbH    = drawData.displayHeight * scaleY;

            float cx0, cy0, cx1, cy1;
            if (noRot)
            {
                cx0 = c * cmd.clip.x                + tx;
                cy0 = c * cmd.clip.y                + ty;
                cx1 = c * (cmd.clip.x + cmd.clip.w) + tx;
                cy1 = c * (cmd.clip.y + cmd.clip.h) + ty;
                if (cx0 > cx1) std::swap(cx0, cx1);
                if (cy0 > cy1) std::swap(cy0, cy1);
            }
            else
            {
                cx0 = 0.0f; cy0 = 0.0f;
                cx1 = drawData.displayWidth;
                cy1 = drawData.displayHeight;
            }

            const float x0 = std::max(0.0f, cx0 * scaleX);
            const float y0 = std::max(0.0f, (drawData.displayHeight - cy1) * scaleY);
            const float x1 = std::min(fbW,  cx1 * scaleX);
            const float y1 = std::min(fbH,  (drawData.displayHeight - cy0) * scaleY);

            const int clipX = static_cast<int>(x0);
            const int clipY = static_cast<int>(y0);
            const int clipW = static_cast<int>(x1 - x0);
            const int clipH = static_cast<int>(y1 - y0);
            if (clipW <= 0 || clipH <= 0) continue;

            rs.setScissor(clipX, clipY, clipW, clipH);

            const GLuint texId = cmd.texture
                ? static_cast<GLuint>(cmd.texture.value)
                : 0u;
            rs.bindTexture(0, GL_TEXTURE_2D, texId);
            glUniform1i(useTextureLoc, cmd.texture ? 1 : 0);

            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(cmd.indexCount),
                           GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(
                               static_cast<uintptr_t>(cmd.indexOffset * sizeof(uint32_t))));
        }
    }

    // ── Clean up via RenderState ──────────────────────────────────────────
    // Disable scissor test so the next frame's glClear covers the full framebuffer.
    rs.setScissorTest(false);
    glBindVertexArray(0);
    rs.bindTexture(0, GL_TEXTURE_2D, 0);
    rs.useProgram(0);
}
