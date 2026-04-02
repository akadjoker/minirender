#include "ShadowMap.hpp"
#include <SDL2/SDL.h>

bool ShadowMap::create(int size)
{
    if (size <= 0) return false;
    if (valid()) destroy();

    size_ = size;

    // Depth texture
    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1.f, 1.f, 1.f, 1.f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth-only FBO (GLES 3.x — complete without glDrawBuffer/glReadBuffer)
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTex_, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("[ShadowMap] FBO incomplete: 0x%x", status);
        destroy();
        return false;
    }
    return true;
}

void ShadowMap::destroy()
{
    if (fbo_)      { glDeleteFramebuffers(1, &fbo_);      fbo_      = 0; }
    if (depthTex_) { glDeleteTextures(1,    &depthTex_);  depthTex_ = 0; }
    size_ = 0;
}

void ShadowMap::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void ShadowMap::unbind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
