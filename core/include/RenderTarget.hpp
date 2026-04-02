#pragma once
#include "glad/glad.h"
#include <string>
#include <vector>
#include <unordered_map>

// Builder-style FBO wrapper.
//
// Single colour attachment (reflection RT):
//   auto *rt = new RenderTarget();
//   rt->create(512, 512).addColor().addDepthRB();
//   rt->finalize();
//
// GBuffer (multiple colour attachments + depth tex):
//   rt->create(w, h).addColor(GL_RGBA8)      // diffuse
//                   .addColor(GL_RGB16F)      // normals
//                   .addColor(GL_R11F_G11F_B10F) // specular
//                   .addDepth();
//   rt->finalize();
//   rt->colorTex(0); rt->colorTex(1); rt->depthTex();
//
// Depth-only shadow map:
//   rt->create(1024,1024).addDepth();
//   rt->finalize();

class RenderTarget
{
public:
    RenderTarget()  = default;
    ~RenderTarget() { destroy(); }
    RenderTarget(const RenderTarget &)            = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;

    // ── Builder ───────────────────────────────────────────────────────────
    RenderTarget &create   (int w, int h);
    RenderTarget &addColor (GLenum fmt = GL_RGBA8);
    RenderTarget &addDepth ();      // samplable depth texture (use for water refraction, GBuffer, etc.)
    RenderTarget &addDepthRB();     // faster depth renderbuffer  (non-samplable; use for reflection, shadow pass)
    bool          finalize ();

    // ── Lifecycle ──────────────────────────────────────────────────────────
    void destroy();
    bool resize(int w, int h);     // destroys and recreates with same configuration

    // ── Per-frame ──────────────────────────────────────────────────────────
    void bind  () const;           // glBindFramebuffer + glViewport(0,0,w,h)
    void unbind() const;           // glBindFramebuffer(0)
    void clear (bool color = true, bool depth = true) const;

    // ── Accessors ──────────────────────────────────────────────────────────
    GLuint colorTex  (int index = 0) const;
    GLuint depthTex  ()              const { return depthTex_; }
    GLuint fbo       ()              const { return fbo_; }
    int    colorCount()              const { return (int)colorTexs_.size(); }
    int    width     ()              const { return w_; }
    int    height    ()              const { return h_; }
    bool   valid     ()              const { return fbo_ != 0; }

private:
    GLuint              fbo_         = 0;
    std::vector<GLuint> colorTexs_;
    std::vector<GLenum> colorFmts_;   // saved for resize()
    GLuint              depthTex_    = 0;
    GLuint              depthRB_     = 0;
    int                 w_           = 0;
    int                 h_           = 0;
    bool                hasDepthTex_ = false;
    bool                hasDepthRB_  = false;
};

// ── RenderTargetManager ───────────────────────────────────────────────────────
// Owns render-targets by name.  Caller still configures attachments after create().
//   auto *rt = RenderTargetManager::instance().create("gbuf", 1280, 720);
//   rt->addColor(GL_RGBA8).addColor(GL_RGB16F).addDepth();
//   rt->finalize();
class RenderTargetManager
{
public:
    static RenderTargetManager &instance();

    // Creates (or replaces) a named render-target;
    // caller must configure attachments and call finalize().
    RenderTarget *create    (const std::string &name, int w, int h);
    RenderTarget *get       (const std::string &name) const;
    void          destroy   (const std::string &name);
    void          destroyAll();

private:
    RenderTargetManager()  = default;
    ~RenderTargetManager() { destroyAll(); }
    RenderTargetManager(const RenderTargetManager &)            = delete;
    RenderTargetManager &operator=(const RenderTargetManager &) = delete;

    std::unordered_map<std::string, RenderTarget *> rts_;
};
