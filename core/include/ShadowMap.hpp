#pragma once
#include "glad/glad.h"

// ============================================================
//  ShadowMap — depth-only FBO for directional / spot shadow maps.
//
//  Usage:
//    ShadowMap sm;
//    sm.create(2048);          // allocate depth FBO (recreates if size changes)
//    // per frame:
//    sm.bind();  ...draw depth pass...  sm.unbind();
//    // bind as sampler at texture unit N:
//    //   rs.bindTexture(N, GL_TEXTURE_2D, sm.depthTexId());
//    delete / let it go out of scope → calls destroy()
// ============================================================
class ShadowMap
{
public:
    ShadowMap()  = default;
    ~ShadowMap() { destroy(); }

    ShadowMap(const ShadowMap &)            = delete;
    ShadowMap &operator=(const ShadowMap &) = delete;

    // Allocate (or reallocate) a depth-only FBO of the given square size.
    // Returns true on success.  Safe to call multiple times — destroys old FBO first.
    bool create(int size);

    void destroy();

    // Bind / unbind the shadow FBO as current render target.
    void bind()   const;
    void unbind() const;

    GLuint depthTexId() const { return depthTex_; }
    int    size()       const { return size_;     }
    bool   valid()      const { return fbo_ != 0; }

private:
    GLuint fbo_      = 0;
    GLuint depthTex_ = 0;
    int    size_     = 0;
};
