#pragma once
#include "glad/glad.h"

struct Texture; // defined in Material.hpp

// ============================================================
//  RenderTarget — off-screen FBO with colour + optional depth.
//
//  Usage:
//    auto *rt = new RenderTarget();
//    rt->create(512, 512);          // allocate FBO
//    rt->addColorAttachment();      // RGBA8 texture
//    rt->addDepthAttachment();      // depth24 RBO   (faster, not sampleable)
//    // OR:
//    rt->addDepthTexture();         // depth24 texture  (sampleable)
//    rt->finalize();                // check completeness
//    // per frame:
//    rt->bind(); ...draw... rt->unbind();
//    // as sampler: rt->colorTex()->id,  rt->depthTex()->id
//    delete rt;  // frees all GL resources
// ============================================================
class RenderTarget
{
public:
    RenderTarget()  = default;
    ~RenderTarget() { destroy(); }

    RenderTarget(const RenderTarget &)            = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;

    // Step 1 — allocate FBO
    bool create(int w, int h);

    // Step 2 — add attachments (before finalize)
    bool addColorAttachment();  // RGBA8 texture
    bool addDepthAttachment();  // DEPTH24 renderbuffer  (not sampleable)
    bool addDepthTexture();     // DEPTH24 texture       (sampleable)

    // Step 3 — check FBO completeness
    bool finalize();

    void destroy();
    void bind()   const;
    void unbind() const;

    Texture *colorTex() const { return colorTex_; }
    Texture *depthTex() const { return depthTex_; } // null if addDepthAttachment used
    GLuint   fbo()      const { return fbo_;      }
    int      width()    const { return width_;    }
    int      height()   const { return height_;   }
    bool     valid()    const { return fbo_ != 0; }

private:
    GLuint   fbo_      = 0;
    GLuint   depthRbo_ = 0;       // depth renderbuffer (if addDepthAttachment)
    int      width_    = 0;
    int      height_   = 0;
    Texture *colorTex_ = nullptr; // owned
    Texture *depthTex_ = nullptr; // owned (only if addDepthTexture was called)
};
