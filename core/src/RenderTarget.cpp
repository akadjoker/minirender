#include "RenderTarget.hpp"
#include "Material.hpp"
#include <SDL2/SDL.h>

bool RenderTarget::create(int w, int h)
{
    if (fbo_) destroy();
    width_  = w;
    height_ = h;
    glGenFramebuffers(1, &fbo_);
    return fbo_ != 0;
}

bool RenderTarget::addColorAttachment()
{
    if (!fbo_ || colorTex_) return false;

    colorTex_ = new Texture();
    colorTex_->width  = width_;
    colorTex_->height = height_;
    colorTex_->target = GL_TEXTURE_2D;
    colorTex_->type   = PixelType::RGBA;

    glGenTextures(1, &colorTex_->id);
    glBindTexture(GL_TEXTURE_2D, colorTex_->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex_->id, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool RenderTarget::addDepthAttachment()
{
    if (!fbo_ || depthRbo_ || depthTex_) return false;

    glGenRenderbuffers(1, &depthRbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depthRbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool RenderTarget::addDepthTexture()
{
    if (!fbo_ || depthTex_ || depthRbo_) return false;

    depthTex_ = new Texture();
    depthTex_->width  = width_;
    depthTex_->height = height_;
    depthTex_->target = GL_TEXTURE_2D;
    depthTex_->type   = PixelType::RGBA;

    glGenTextures(1, &depthTex_->id);
    glBindTexture(GL_TEXTURE_2D, depthTex_->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width_, height_, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTex_->id, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool RenderTarget::finalize()
{
    if (!fbo_) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("[RenderTarget] FBO incomplete: 0x%x", status);
        destroy();
        return false;
    }
    return true;
}

void RenderTarget::destroy()
{
    if (fbo_)      { glDeleteFramebuffers(1,  &fbo_);      fbo_      = 0; }
    if (depthRbo_) { glDeleteRenderbuffers(1, &depthRbo_); depthRbo_ = 0; }
    delete colorTex_; colorTex_ = nullptr;
    delete depthTex_; depthTex_ = nullptr;
    width_ = height_ = 0;
}

void RenderTarget::bind()   const { glBindFramebuffer(GL_FRAMEBUFFER, fbo_); }
void RenderTarget::unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0);   }
