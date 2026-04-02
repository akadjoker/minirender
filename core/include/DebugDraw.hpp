#pragma once
#include "glad/glad.h"

// ============================================================
//  DebugDraw — lightweight 2D textured-quad overlay
//
//  Typical use (after main render, before SDL_GL_SwapWindow):
//
//    auto &dd = DebugDraw::instance();
//    dd.init();   // idempotent – safe to call every frame
//
//    dd.drawQuad(  0, 0, 256, 144, reflTex->id,  vpW, vpH);
//    dd.drawQuad(260, 0, 256, 144, refrTex->id,  vpW, vpH);
//    dd.drawQuad(520, 0, 256, 144, depthTex->id, vpW, vpH);
//
//  Coordinates: pixel-space, (0,0) = top-left corner.
//  vpW / vpH   = current GL drawable size (from camera->viewport or SDL).
// ============================================================
class DebugDraw
{
public:
    static DebugDraw &instance();

    // Build GL objects on first call.  Idempotent.
    bool init();

    // Release all GL resources.
    void release();

    // Draw a textured quad in pixel-space coordinates.
    // texId   : any GL_TEXTURE_2D name (colour or depth).
    // isDepth : if true, uses the R channel as greyscale (depth vis).
    void drawQuad(int x, int y, int w, int h, GLuint texId,
                  int vpW, int vpH, bool isDepth = false);

private:
    DebugDraw() = default;

    GLuint vao_      = 0;
    GLuint vbo_      = 0;
    GLuint prog_     = 0;    // colour shader
    GLuint progD_    = 0;    // depth greyscale shader
    GLint  uTex_     = -1;
    GLint  uTexD_    = -1;
    bool   ready_    = false;

    GLuint compileShader(GLenum type, const char *src);
    GLuint buildProgram (const char *vsSrc, const char *fsSrc);
};
