#pragma once
#include "Camera.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Math.hpp"
#include "Types.hpp"
#include <cstdint>
#include <vector>

// ─── RenderSortMode ──────────────────────────────────────────────────────────
enum class RenderSortMode { None, FrontToBack, BackToFront };

// ─── RenderItem ──────────────────────────────────────────────────────────────
struct RenderItem
{
    IDrawable   *drawable   = nullptr;
    Material    *material   = nullptr;
    glm::mat4    model      = glm::mat4(1.0f);
    BoundingBox  worldAABB;
    uint32_t     indexStart = 0;
    uint32_t     indexCount = 0;
    float        depth      = 0.0f;
    uint32_t     passMask   = RenderPassMask::Opaque;  // used by nodes
};

// ─── RenderQueue ─────────────────────────────────────────────────────────────
// Named buckets drawn in order: shadow → opaque → sky → transparent
struct RenderQueue
{
    std::vector<RenderItem> shadow;       // depth-only, no camera frustum cull
    std::vector<RenderItem> opaque;       // sorted front-to-back
    std::vector<RenderItem> transparent;  // sorted back-to-front

    void clear()
    {
        shadow.clear();
        opaque.clear();
        transparent.clear();
    }

    bool empty() const
    {
        return shadow.empty() && opaque.empty() && transparent.empty();
    }

    // Nodes call add() — routes to opaque or transparent via passMask.
    void add(const RenderItem &item)
    {
        if (item.passMask & RenderPassMask::Transparent) transparent.push_back(item);
        else                                              opaque.push_back(item);
    }
};

// ─── RenderStats ─────────────────────────────────────────────────────────────
struct RenderStats
{
    uint32_t drawCalls       = 0;
    uint32_t triangles       = 0;
    uint32_t vertices        = 0;
    uint32_t objects         = 0;
    uint32_t shaderChanges   = 0;
    uint32_t materialChanges = 0;
    uint32_t textureBinds    = 0;

    void reset()
    {
        drawCalls = triangles = vertices = objects =
        shaderChanges = materialChanges = textureBinds = 0;
    }
};

// ─── FrameContext ─────────────────────────────────────────────────────────────
// Internal: passed between Scene render methods.
struct FrameContext
{
    const Camera             *camera    = nullptr;
    std::vector<const Light *> lights;
    glm::ivec4                 viewport  = glm::ivec4(0);
    Frustum                    frustum;
    RenderStats               *stats     = nullptr;
    glm::vec4                  clipPlane = {0.f, 0.f, 0.f, 0.f};
    bool                       secondary = false;

    // Directional shadow (filled by Scene::drawShadowPass, zero = disabled)
    glm::mat4  lightSpaceMatrix = glm::mat4(1.f);
    GLuint     shadowTex        = 0;
    glm::vec3  lightDir         = {0.f, -1.f, 0.f};
    glm::vec3  lightColor       = {1.f,  1.f, 1.f};
    float      shadowBias       = 0.005f;
};
