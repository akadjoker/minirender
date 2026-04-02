#pragma once
#include "Node.hpp"
#include "RenderPipeline.hpp"
#include "RenderTarget.hpp"
#include "Math.hpp"
#include <vector>

class RenderBatch;

// Result of a scene-level pick.
struct ScenePickResult
{
    PickResult  result;
    Node3D     *node = nullptr;
};

// ─── Scene ───────────────────────────────────────────────────────────────────
// Manual render pipeline — caller drives every pass explicitly.
//
//   scene.gatherScene(cam);
//   scene.drawShadowDepth(depthShader, lightSpace);
//   scene.drawPass(litShader, RenderPassMask::Opaque);
//   scene.drawPass(litShader, RenderPassMask::Flat);
//
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
    ManualMeshNode       *createManualMeshNode  (const std::string &name = "");
 

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

    // ── Sky ───────────────────────────────────────────────────────────────────
    // Optional: set a sky shader and call drawSky() manually after opaque pass.
    Shader *skyShader = nullptr;
    void drawSky();

    // ── Debug ─────────────────────────────────────────────────────────────────
    void debug(RenderBatch *batch);

    // ── Stats ─────────────────────────────────────────────────────────────────
    const RenderStats &stats() const { return stats_; }

    // ── Picking ───────────────────────────────────────────────────────────────
    ScenePickResult pick(const Ray &ray) const;

    void release();

    // ── Manual pipeline API ──────────────────────────────────────────────────
    // Drive rendering explicitly from the demo / game loop:
    //
    //   scene.gatherScene(cam);
    //   myShader->bind();
    //   myShader->setMat4("u_view", view); // caller sets ALL uniforms
    //   scene.drawPass(myShader, RenderPassMask::Opaque);
    //
    // GL state, RTs, shadow maps, clip planes — fully managed by the caller.

    // Cull scene with cam and fill internal render queues.
    // Call once per frame before drawPass / drawShadowDepth.
    void gatherScene(Camera *cam);

    // Draw queued items matching passMask using sh.
    // sh must already be bound; only u_model and material textures are set here.
    void drawPass(Shader *sh, uint32_t passMask,
                  RenderSortMode sort = RenderSortMode::None);

    // Draw shadow-caster queue with depthSh, injecting u_lightSpace.
    void drawShadowDepth(Shader *depthSh, const glm::mat4 &lightSpace);

    // Direct access to render queues (read-only).
    const RenderQueue &renderQueue() const { return queue_; }


    // ── Collected lights (valid after gather, before render) ─────────────────
    const std::vector<const Light *> &lights() const { return frameCtx_.lights; }

private:
    // Gather visible items into queues
    void gather(const Frustum &camFrustum, const Frustum &shadowFrustum,
                RenderQueue &queue);
    void gatherNode(Node *node, const Frustum &camFrustum,
                    const Frustum &shadowFrustum, RenderQueue &queue);

    // Node traversal helpers
    void updateNode(Node *node, float dt);
    void debugNode (Node *node, RenderBatch *batch);

    std::vector<Camera *> cameras_;
    Camera               *currentCamera_ = nullptr;
    std::vector<Node *>   roots_;
    std::vector<Node *>   pendingRemoval_;

    RenderQueue   queue_;
    FrameContext  frameCtx_;
    RenderStats   stats_;
};
