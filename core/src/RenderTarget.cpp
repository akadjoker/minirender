#include "RenderTarget.hpp"
#include <SDL2/SDL.h>

// ── helpers ──────────────────────────────────────────────────────────────────

// Map an internal colour format to a compatible base format + type pair
// (used only when allocating empty textures, data pointer is null).
static void colorFormatHints(GLenum internal, GLenum &base, GLenum &type)
{
    switch (internal)
    {
    case GL_R8:
    case GL_R8_SNORM:   base = GL_RED;  type = GL_UNSIGNED_BYTE; return;
    case GL_R16F:
    case GL_R32F:       base = GL_RED;  type = GL_FLOAT;         return;
    case GL_RG8:        base = GL_RG;   type = GL_UNSIGNED_BYTE; return;
    case GL_RG16F:
    case GL_RG32F:      base = GL_RG;   type = GL_FLOAT;         return;
    case GL_RGB16F:
    case GL_RGB32F:
    case GL_R11F_G11F_B10F:
                        base = GL_RGB;  type = GL_FLOAT;         return;
    case GL_SRGB8:      base = GL_RGB;  type = GL_UNSIGNED_BYTE; return;
    case GL_RGBA16F:
    case GL_RGBA32F:    base = GL_RGBA; type = GL_FLOAT;         return;
    case GL_SRGB8_ALPHA8:
    case GL_RGBA8:
    default:            base = GL_RGBA; type = GL_UNSIGNED_BYTE; return;
    }
}

// ── RenderTarget ─────────────────────────────────────────────────────────────

RenderTarget &RenderTarget::create(int w, int h)
{
    w_ = w;
    h_ = h;
    colorTexs_.clear();
    colorFmts_.clear();
    hasDepthTex_ = false;
    hasDepthRB_  = false;
    return *this;
}

RenderTarget &RenderTarget::addColor(GLenum fmt)
{
    colorFmts_.push_back(fmt);
    return *this;
}

RenderTarget &RenderTarget::addDepth()
{
    hasDepthTex_ = true;
    return *this;
}

RenderTarget &RenderTarget::addDepthRB()
{
    hasDepthRB_ = true;
    return *this;
}

bool RenderTarget::finalize()
{
    if (fbo_) destroy();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // ── colour attachments ────────────────────────────────────────────────
    colorTexs_.resize(colorFmts_.size(), 0);
    std::vector<GLenum> drawBufs;

    for (int i = 0; i < (int)colorFmts_.size(); ++i)
    {
        GLenum base, type;
        colorFormatHints(colorFmts_[i], base, type);

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, colorFmts_[i],
                     w_, h_, 0, base, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0 + i,
                               GL_TEXTURE_2D, tex, 0);
        colorTexs_[i] = tex;
        drawBufs.push_back(GL_COLOR_ATTACHMENT0 + i);
    }

    if (colorFmts_.empty())
    {
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
    }
    else
    {
        glDrawBuffers((GLsizei)drawBufs.size(), drawBufs.data());
    }

    // ── depth attachment ──────────────────────────────────────────────────
    if (hasDepthTex_)
    {
        glGenTextures(1, &depthTex_);
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                     w_, h_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, depthTex_, 0);
    }
    else if (hasDepthRB_)
    {
        glGenRenderbuffers(1, &depthRB_);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRB_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w_, h_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, depthRB_);
    }

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
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    for (GLuint t : colorTexs_) if (t) glDeleteTextures(1, &t);
    colorTexs_.clear();
    colorFmts_.clear();
    if (depthTex_) { glDeleteTextures(1, &depthTex_); depthTex_ = 0; }
    if (depthRB_)  { glDeleteRenderbuffers(1, &depthRB_); depthRB_ = 0; }
    hasDepthTex_ = false;
    hasDepthRB_  = false;
}

bool RenderTarget::resize(int w, int h)
{
    // Save configuration before destroy() wipes it
    auto  savedFmts  = colorFmts_;
    bool  savedDepTx = hasDepthTex_;
    bool  savedDepRB = hasDepthRB_;

    destroy();

    create(w, h);
    colorFmts_   = savedFmts;
    hasDepthTex_ = savedDepTx;
    hasDepthRB_  = savedDepRB;
    return finalize();
}

void RenderTarget::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}

void RenderTarget::unbind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::clear(bool color, bool depth) const
{
    GLbitfield mask = 0;
    if (color && !colorTexs_.empty()) mask |= GL_COLOR_BUFFER_BIT;
    if (depth && (depthTex_ || depthRB_)) mask |= GL_DEPTH_BUFFER_BIT;
    if (mask) glClear(mask);
}

GLuint RenderTarget::colorTex(int index) const
{
    if (index < 0 || index >= (int)colorTexs_.size()) return 0;
    return colorTexs_[index];
}

// ── RenderTargetManager ──────────────────────────────────────────────────────

RenderTargetManager &RenderTargetManager::instance()
{
    static RenderTargetManager inst;
    return inst;
}

RenderTarget *RenderTargetManager::create(const std::string &name, int w, int h)
{
    auto it = rts_.find(name);
    if (it != rts_.end())
    {
        delete it->second;
        rts_.erase(it);
    }
    auto *rt = new RenderTarget();
    rt->create(w, h);
    rts_[name] = rt;
    return rt;
}

RenderTarget *RenderTargetManager::get(const std::string &name) const
{
    auto it = rts_.find(name);
    return it != rts_.end() ? it->second : nullptr;
}

void RenderTargetManager::destroy(const std::string &name)
{
    auto it = rts_.find(name);
    if (it != rts_.end())
    {
        delete it->second;
        rts_.erase(it);
    }
}

void RenderTargetManager::destroyAll()
{
    for (auto &[name, rt] : rts_) delete rt;
    rts_.clear();
}
