#pragma once
#include "Node.hpp"
#include "RenderPipeline.hpp"
#include "RenderTarget.hpp"
#include "ShadowMap.hpp"
#include "Math.hpp"
#include <vector>

class RenderBatch;
class WaterNode3D;

// Result of a scene-level pick.
struct ScenePickResult
{
    PickResult  result;
    Node3D     *node = nullptr;
};

// ─── Scene ───────────────────────────────────────────────────────────────────
// Explicit render pipeline — no passes, no techniques.
//
// Render order each frame:
//   preRender (water RTs, animators)
//   → renderOpaque
//   → renderSky
//   → renderTransparent
//
// For reflection/refraction, WaterNode calls renderToTarget() from preRender().
class Scene
{
public:
    Scene();
    ~Scene();

    // ── Camera ───────────────────────────────────────────────────────────────
    Camera *createCamera(const std::string &name = "");
    void    removeCamera(Camera *cam);
    Camera *currentCamera() const { return currentCamera_; }
    void    setCurrentCamera(Camera *cam);
    const std::vector<Camera *> &cameras() const { return cameras_; }

    // ── Node management ───────────────────────────────────────────────────────
    MeshNode             *createMeshNode        (const std::string &name = "", Mesh *mesh = nullptr);
    AnimatedMeshNode     *createAnimatedMeshNode(const std::string &name = "", AnimatedMesh *mesh = nullptr);
    class ManualMeshNode *createManualMeshNode  (const std::string &name = "");
    WaterNode3D          *createWaterNode       (const std::string &name = "");

    template<typename T>
    T *createLight(const std::string &nodeName = "")
    {
        auto *light = new T();
        light->name = nodeName;
        add(light);
        return light;
    }

    void add(Node *node);
    void remove(Node *node);
    void clear();
    void markForRemoval(Node *node);

    // ── Per-frame ─────────────────────────────────────────────────────────────
    void update(float dt);
    void render();

    // ── Render to off-screen target ───────────────────────────────────────────
    // Used by WaterNode during preRender for reflection/refraction.
    // clipPlane: dot(worldPos, plane) < 0 → fragment discarded (vec4(0) = off).
    void renderToTarget(Camera *cam, RenderTarget *rt,
                        glm::vec4 clipPlane = glm::vec4(0.f));

    // ── Sky ───────────────────────────────────────────────────────────────────
    // Set a sky shader (procedural/gradient). Drawn after opaque, no depth write.
    // If nullptr, sky is skipped.
    Shader *skyShader = nullptr;

    // ── Directional shadow map ────────────────────────────────────────────────
    // Enable by setting shadow.enabled = true and shadow.depthShader.
    // Any material using lit_shadow.vert/frag will receive u_lightSpace +
    // u_shadowMap (unit 1) + u_lightDir/Color/Bias automatically.
    struct ShadowConfig
    {
        bool      enabled     = false;
        Shader   *depthShader = nullptr;
        glm::vec3 lightDir    = glm::normalize(glm::vec3(1.f, 3.f, 1.f));
        glm::vec3 lightColor  = {1.f, 1.f, 1.f};
        float     bias        = 0.005f;
        int       mapSize     = 2048;  // depth texture size
        int       numCascades = 1;     // 1=simple fixed ortho, 2-4=CSM
        // Simple shadow (numCascades==1)
        float     orthoSize   = 40.f;  // half-size of ortho frustum (world units)
        float     lightDist   = 100.f; // distance of light from world origin
        // CSM only
        float     lambda      = 0.75f; // 0=uniform, 1=logarithmic split
    } shadow;

    // ── Debug ─────────────────────────────────────────────────────────────────
    void debug(RenderBatch *batch);

    // ── Stats ─────────────────────────────────────────────────────────────────
    const RenderStats &stats() const { return stats_; }

    // ── Picking ───────────────────────────────────────────────────────────────
    ScenePickResult pick(const Ray &ray) const;

    void release();

    // ── Collected lights (valid after gather, before render) ─────────────────
    const std::vector<const Light *> &lights() const { return frameCtx_.lights; }

private:
    // Render one camera — called by render()
    void renderCamera(Camera *cam);

    // Gather visible items into queues
    void gather(const Frustum &camFrustum, const Frustum &shadowFrustum,
                RenderQueue &queue);
    void gatherNode(Node *node, const Frustum &camFrustum,
                    const Frustum &shadowFrustum, RenderQueue &queue);

    // Draw passes
    void drawItems(const std::vector<RenderItem> &items, const FrameContext &ctx,
                   RenderSortMode sort = RenderSortMode::None);
    void drawSky(const FrameContext &ctx);
    void drawShadowPass();     // numCascades==1 — single ortho shadow map
    void drawCsmShadowPass();  // numCascades >1 — PSSM cascade shadow maps

    // Node traversal helpers
    void preRenderNode(Node *node, Camera *cam);
    void updateNode   (Node *node, float dt);
    void debugNode    (Node *node, RenderBatch *batch);

    std::vector<Camera *> cameras_;
    Camera               *currentCamera_ = nullptr;
    std::vector<Node *>   roots_;
    std::vector<Node *>   pendingRemoval_;

    RenderQueue   queue_;
    FrameContext  frameCtx_;
    RenderStats   stats_;

    // CSM — up to 4 cascade depth maps (created lazily in drawShadowPass)
    static constexpr int MAX_CSM = 4;
    ShadowMap shadowMaps_[MAX_CSM];
    glm::mat4 lightSpaceMatrices_[MAX_CSM] = {};
    float     cascadeFarPlanes_[MAX_CSM]   = {};
};
