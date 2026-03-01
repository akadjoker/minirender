#include "GBuffer.hpp"
#include "RenderState.hpp"
#include <SDL2/SDL.h>

// ─── helpers ─────────────────────────────────────────────────────────────────

static GLuint makeTex16F(unsigned int w, unsigned int h)
{
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, (GLsizei)w, (GLsizei)h,
                 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

static GLuint makeTex8(unsigned int w, unsigned int h)
{
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

// ─── GBuffer ─────────────────────────────────────────────────────────────────

bool GBuffer::setupAttachments()
{
    gPosition   = makeTex16F(width, height);
    gNormal     = makeTex16F(width, height);
    gAlbedoSpec = makeTex8  (width, height);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition,   0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal,     0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoSpec, 0);

    // GLES 3.0 — seleccionar MRT
    GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, bufs);

    // Depth + stencil renderbuffer
    glGenRenderbuffers(1, &depthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)width, (GLsizei)height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRBO);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[GBuffer] FBO incomplete: 0x%x", status);
        return false;
    }
    return true;
}

void GBuffer::releaseAttachments()
{
    if (gPosition)   { glDeleteTextures(1, &gPosition);       gPosition   = 0; }
    if (gNormal)     { glDeleteTextures(1, &gNormal);         gNormal     = 0; }
    if (gAlbedoSpec) { glDeleteTextures(1, &gAlbedoSpec);     gAlbedoSpec = 0; }
    if (depthRBO)    { glDeleteRenderbuffers(1, &depthRBO);   depthRBO    = 0; }
}

bool GBuffer::initialize(unsigned int w, unsigned int h)
{
    width  = w;
    height = h;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    bool ok = setupAttachments();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (ok)
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[GBuffer] Created %ux%u GBuffer", w, h);
    return ok;
}

void GBuffer::resize(unsigned int w, unsigned int h)
{
    if (w == width && h == height)
        return;

    width  = w;
    height = h;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    releaseAttachments();
    setupAttachments();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBuffer::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    RenderState::instance().setViewport(0, 0, (GLsizei)width, (GLsizei)height);
}

void GBuffer::unbind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBuffer::release()
{
    if (fbo_)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        releaseAttachments();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}
