#pragma once
#include "Node.hpp"        // Light* in FrameContext
#include "Camera.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Math.hpp"
#include "Types.hpp"
#include "GBuffer.hpp"
#include <cstdint>
#include <string>
#include <vector>

enum class RenderSortMode { None, FrontToBack, BackToFront };

struct RenderItem
{
    IDrawable   *drawable   = nullptr; 
    Material    *material   = nullptr;
    glm::mat4    model      = glm::mat4(1.0f);
    BoundingBox  worldAABB;          // precomputed in gatherNode — never recompute
    uint32_t     indexStart = 0;
    uint32_t     indexCount = 0;
    float        depth      = 0.0f;
    uint32_t     passMask   = RenderPassMask::Opaque;
};

class RenderQueue
{
public:
    void clear()
    {
        opaque_.clear();
        transparent_.clear();
    }

    void add(const RenderItem &item)
    {
        if (item.passMask & RenderPassMask::Opaque) opaque_.push_back(item);
        if (item.passMask & RenderPassMask::Transparent) transparent_.push_back(item);
    }

    std::vector<RenderItem>       &getOpaque()       { return opaque_; }
    const std::vector<RenderItem> &getOpaque() const  { return opaque_; }
    std::vector<RenderItem>       &getTransparent()       { return transparent_; }
    const std::vector<RenderItem> &getTransparent() const { return transparent_; }

private:
    std::vector<RenderItem> opaque_;
    std::vector<RenderItem> transparent_;
};

// ─── RenderStats ─────────────────────────────────────────────────────────────────────
struct RenderStats
{
    uint32_t drawCalls      = 0;
    uint32_t triangles      = 0;
    uint32_t vertices       = 0;
    uint32_t objects        = 0;   // render items submitted (post-cull)
    uint32_t shaderChanges  = 0;   // useProgram calls
    uint32_t materialChanges= 0;   // bindTextures/applyStates calls
    uint32_t textureBinds   = 0;   // individual glBindTexture calls

    void reset() { drawCalls = triangles = vertices = objects =
                   shaderChanges = materialChanges = textureBinds = 0; }
};

// ─── FrameContext ───────────────────────────────────────────────────────────────────
struct FrameContext
{
    const Camera             *camera  = nullptr;
    std::vector<const Light *> lights;
    glm::ivec4                 viewport = glm::ivec4(0);
    Frustum                    frustum;
    RenderStats               *stats       = nullptr;
    RenderQueue                shadowQueue;
    // When true: secondary render (reflection/refraction).
    // Expensive pre-passes (CSM depth) are skipped.
    bool                       secondary   = false;
    // Clip plane in world space: dot(worldPos, clipPlane) < 0 → discard fragment.
    // Set to {0,0,0,0} to disable.
    glm::vec4                  clipPlane   = {0.f, 0.f, 0.f, 0.f};
};

// ─── RenderPass ───────────────────────────────────────────────────────────────────
 
class RenderPass
{
public:
    Shader        *shader     = nullptr;   // nullptr → defer to material's shader
    bool           clearColor = false;
    bool           clearDepth = false;
    glm::vec4      clearValue = {0.f, 0.f, 0.f, 1.f};
    bool           depthTest  = true;
    bool           depthWrite = true;
    bool           cull       = true;
    bool           blend      = false;
    GLenum         blendSrc   = GL_SRC_ALPHA;
    GLenum         blendDst   = GL_ONE_MINUS_SRC_ALPHA;
    // When scissorTest = true the scissor rect is set to scissorRect if its area
    // is non-zero, otherwise it falls back to the current viewport.
    bool           scissorTest = false;
    glm::ivec4     scissorRect = {0, 0, 0, 0}; // {0,0,0,0} → use viewport
    uint32_t       passMask   = RenderPassMask::Opaque;
    RenderSortMode sortMode   = RenderSortMode::FrontToBack;

    virtual ~RenderPass() = default;
    virtual void execute(const FrameContext &ctx, RenderQueue &queue) const;

protected:
    virtual void drawItem          (const FrameContext &ctx, const RenderItem &item, Shader *sh) const;
    virtual void drawItemNoMaterial(const FrameContext &ctx, const RenderItem &item, Shader *sh) const;
};

class OpaquePass : public RenderPass { public: OpaquePass(); };
class TransparentPass : public RenderPass { public: TransparentPass(); };


class SkyPass : public RenderPass
{
public:
    glm::vec3 skyTop      = {0.18f, 0.36f, 0.72f}; // zenith
    glm::vec3 skyHorizon  = {0.62f, 0.78f, 0.90f}; // horizon
    glm::vec3 groundColor = {0.20f, 0.18f, 0.14f}; // below horizon

    void execute(const FrameContext &ctx, RenderQueue &queue) const override;

private:
    mutable GLuint dummyVao_ = 0;
};

// ─── Technique ───────────────────────────────────────────────────────────────────
// Owns and sequences passes. Scene calls render() once per camera per frame.
class Technique
{
public:
    std::string               name;
    std::vector<RenderPass *> passes;  // owned

    virtual ~Technique() { for (auto *p : passes) delete p; }
    virtual void render(const FrameContext &ctx, RenderQueue &queue) const;

    // Override to provide a tighter frustum for shadow-caster culling.
    // Default: infinite (no culling). CsmTechnique returns the light frustum.
    virtual Frustum getShadowCasterFrustum() const { return Frustum::infinite(); }

    template<typename T, typename... Args>
    T *addPass(Args &&...args)
    {
        auto *p = new T(std::forward<Args>(args)...);
        passes.push_back(p);
        return p;
    }
};

// ─── ForwardTechnique ──────────────────────────────────────────────────────────────────
class ForwardTechnique : public Technique
{
public:
    ForwardTechnique();  // creates OpaquePass + TransparentPass, stores typed ptrs
    OpaquePass      *opaque()      const { return opaque_; }
    TransparentPass *transparent() const { return transparent_; }

private:
    OpaquePass      *opaque_      = nullptr;  // owned via passes[]
    TransparentPass *transparent_ = nullptr;  // owned via passes[]
};

// ─── GBufferPass ─────────────────────────────────────────────────────────────
// Geometry pass: renders all opaque items into the GBuffer.
// Requires pass->shader to be set to the gbuffer program.
class GBufferPass : public RenderPass
{
public:
    GBufferPass();

    GBuffer *gbuffer = nullptr;  // non-owning

    void execute(const FrameContext &ctx, RenderQueue &queue) const override;
};

// ─── DeferredLightingPass ─────────────────────────────────────────────────────
// Fullscreen lighting pass: samples the GBuffer and computes shading.
// Requires pass->shader to be set to the deferred_lighting program.
class DeferredLightingPass : public RenderPass
{
public:
    DeferredLightingPass();
    ~DeferredLightingPass();

    GBuffer  *gbuffer = nullptr;  // non-owning

    // Directional light — set once or each frame
    glm::vec3 dirLightDir   = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));
    glm::vec3 dirLightColor = glm::vec3(1.f, 1.f, 1.f);

    void execute(const FrameContext &ctx, RenderQueue &queue) const override;

private:
    GLuint dummyVAO_ = 0;  // bound for the fullscreen triangle draw
};

// ─── DeferredTechnique ────────────────────────────────────────────────────────
// GBufferPass → DeferredLightingPass → TransparentPass (forward)
class DeferredTechnique : public Technique
{
public:
    DeferredTechnique();
    ~DeferredTechnique();

    void render(const FrameContext &ctx, RenderQueue &queue) const override;

    // Call on window resize so GBuffer matches the backbuffer
    void onResize(unsigned int w, unsigned int h);

    GBufferPass          *geometryPass()    const;
    DeferredLightingPass *lightingPass()    const;
    TransparentPass      *transparentPass() const;

    GBuffer *getGBuffer() const { return gbuffer_; }

private:
    GBuffer *gbuffer_ = nullptr; // owned
};
