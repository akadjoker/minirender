#pragma once
#include "glad/glad.h"
#include <SDL2/SDL.h>

// ============================================================
//  GBuffer — FBO com 3 colour attachments + depth
//
//  Layout:
//    attachment 0 (gPosition)   RGB16F  — world-space position
//    attachment 1 (gNormal)     RGB16F  — world-space normal
//    attachment 2 (gAlbedoSpec) RGBA8   — albedo.rgb + specular
//    depth renderbuffer         DEPTH24_STENCIL8
// ============================================================
class GBuffer
{
public:
    unsigned int width      = 0;
    unsigned int height     = 0;

    GLuint gPosition        = 0;
    GLuint gNormal          = 0;
    GLuint gAlbedoSpec      = 0;
    GLuint depthRBO         = 0;

    bool initialize(unsigned int w, unsigned int h);
    void resize(unsigned int w, unsigned int h);

    // Bind para o geometry pass — activa MRT e viewport
    void bind() const;

    // Volta ao FBO default (screen)
    void unbind() const;

    void release();

    GLuint fbo() const { return fbo_; }
    bool   valid() const { return fbo_ != 0; }

private:
    GLuint fbo_ = 0;

    bool   setupAttachments();
    void   releaseAttachments();
};
