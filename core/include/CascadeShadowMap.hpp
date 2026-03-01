#pragma once
#include "glad/glad.h"
#include "RenderPipeline.hpp"
#include "Camera.hpp"
#include "Node.hpp"
#include <glm/glm.hpp>
#include <array>

// ============================================================
//  CascadeShadowMap — 4-cascade directional shadow maps
//
//  Usage:
//    CascadeShadowMap csm;
//    csm.initialize(2048, 2048);
//    csm.setLightDirection(sunDir);
//
//  Each frame (before the lit pass):
//    csm.update(*camera);           // recomputes split planes + light matrices
//    csm.beginCascade(i);           // bind FBO for cascade i
//      renderScene(csm.getDepthShader());
//    csm.endCascade();
//
//  Then in the lit pass, bind csm to shader:
//    csm.bindToShader(litShader);
// ============================================================

static constexpr int CSM_NUM_CASCADES = 4;

class CascadeShadowMap
{
public:
    // ── Parameters ────────────────────────────────────────────
    glm::vec3 lightDirection = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));
    float     lambda         = 0.75f;  // blend uniform↔log split (0=uniform,1=log)
    float     nearPlane      = 0.1f;   // copied from camera at update()
    float     farPlane       = 300.f;  // only shadow this far

    // ── Public state (read-only after update()) ───────────────
    std::array<float,     CSM_NUM_CASCADES> cascadeSplits = {};
    std::array<glm::mat4, CSM_NUM_CASCADES> lightSpaceMatrices = {};

    // ── Setup ─────────────────────────────────────────────────
    bool initialize(unsigned int width = 2048, unsigned int height = 2048);
    void release();
    ~CascadeShadowMap() { release(); }

    void setLightDirection(const glm::vec3& dir) { lightDirection = glm::normalize(dir); }
    void setShadowFarPlane(float f)              { farPlane = f; }
    void setLambda(float l)                      { lambda = glm::clamp(l, 0.f, 1.f); }

    // ── Per-frame ─────────────────────────────────────────────
    /// Recompute split planes and per-cascade light-space matrices from camera.
    void update(const Camera& cam);

    /// Bind the FBO for cascade i — all draw calls go to that shadow map.
    void beginCascade(int cascade);

    /// Unbind the FBO.
    void endCascade();

    // ── Shader binding ────────────────────────────────────────
    /// Upload u_lightSpace[4], u_cascadeSplits[4], u_shadowMap[0..3]
    /// to an already-bound lit shader.
    void bindToShader(Shader* shader, int baseTextureUnit = 1) const;

    // ── Accessors ─────────────────────────────────────────────
    GLuint getDepthTexture(int cascade) const { return textures_[cascade]; }
    int    numCascades()                const { return CSM_NUM_CASCADES; }
    unsigned int width()                const { return width_; }
    unsigned int height()               const { return height_; }

private:
    unsigned int width_  = 2048;
    unsigned int height_ = 2048;

    std::array<GLuint, CSM_NUM_CASCADES> fbos_     = {};
    std::array<GLuint, CSM_NUM_CASCADES> textures_ = {};

    glm::mat4 computeLightSpaceMatrix(const Camera& cam,
                                       float nearSplit, float farSplit) const;

    void computeSplits(float camNear, float camFar);
};

// ============================================================
//  CsmDepthPass — renders the scene into one cascade
//  (one RenderPass per cascade)
// ============================================================
class CsmDepthPass : public RenderPass
{
public:
    CascadeShadowMap* csm      = nullptr;
    int               cascade  = 0;

    void execute(const FrameContext& ctx, RenderQueue& queue) const override;
};

// ============================================================
//  CsmTechnique — full CSM pipeline
//  Order: 4× CsmDepthPass → OpaquePass → TransparentPass
// ============================================================
class CsmTechnique : public Technique
{
public:
    CsmTechnique();
    ~CsmTechnique();

    bool initialize(unsigned int shadowRes = 2048);
    void release();

    void render(const FrameContext& ctx, RenderQueue& queue) const override;

    // Non-owning: set to choose which light-aware shader to upload CSM to.
    Shader *litShader = nullptr;

    CascadeShadowMap  *getCsm()           const { return csm_; }
    OpaquePass        *getOpaquePass()    const;
    TransparentPass   *getTransparentPass() const;

private:
    CascadeShadowMap *csm_ = nullptr;   // owned
};
