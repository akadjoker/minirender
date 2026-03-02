#pragma once
#include "Node.hpp"
#include "RenderPipeline.hpp"
#include "Math.hpp"
 
#include "RenderTarget.hpp"
#include "WaterNode.hpp"
#include <vector>

class RenderBatch;

// Result of a scene-level pick: closest hit across all pickable nodes.
struct ScenePickResult
{
    PickResult  result;           // geometry hit details (hit, distance, point, normal ...)
    Node3D     *node    = nullptr; // the node that was hit (nullptr = no hit)
};

// Scene manages the node tree and dispatches per-frame rendering
// through a configurable list of Technique* (non-owning).
class Scene
{
public:
    Scene()  ;
    ~Scene();

 

    // --- Camera management (scene owns cameras) ---
    Camera *createCamera(const std::string &name = "");
    void    removeCamera(Camera *cam);
    const std::vector<Camera *> &cameras() const { return cameras_; }
    Camera *currentCamera() const { return currentCamera_; }
    void    setCurrentCamera(Camera *cam);

    // --- Node management ---
    MeshNode             *createMeshNode        (const std::string &name = "", Mesh *mesh = nullptr);
    AnimatedMeshNode     *createAnimatedMeshNode(const std::string &name = "", AnimatedMesh *mesh = nullptr);
    class ManualMeshNode *createManualMeshNode  (const std::string &name = "");
    WaterNode3D          *createWaterNode       (const std::string &name = "");

    // Creates a light node, adds it to the root of the scene tree.
    // Example: auto *sun = scene.createLight<DirectionalLight>("sun");
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
    void clear(); // deletes owned nodes

    /// Mark a node to be removed (and deleted) at the start of the next update().
    /// Safe to call from update() callbacks (e.g. on collision, on lifetime expiry).
    void markForRemoval(Node *node);

    // --- Technique list (owned, executed in order) ---
    void addTechnique(Technique *t) { techniques_.push_back(t); }
    void clearTechniques()          { for (auto *t : techniques_) delete t; techniques_.clear(); }

    // --- Per-frame API ---
    // Update animators — call BEFORE render()
    void update(float dt);

    // Gather visible nodes (frustum cull) + render all cameras
    void render();
 
    // Render the scene into an off-screen RenderTarget using a custom camera.
    // secondary=true: expensive pre-passes (e.g. CSM depth) are skipped.
    // Light/clip data are taken from sceneData at the time of the call.
    // Debug: print which nodes are gathered into the next renderToTarget call.
    // Resets automatically after one frame.
    bool debugRTGather = false;

    // Clip plane in world space (xyz=normal, w=offset).
    // Active only during renderToTarget — auto-reset to {0,0,0,0} afterwards.
    void setClipPlane(const glm::vec4 &plane) { clipPlane_ = plane; }
    glm::vec4 getClipPlane() const { return clipPlane_; }

    void renderToTarget(Camera *cam, RenderTarget *rt);

    void debug(RenderBatch *batch);

    // Debug stats accumulated during the last render() call
    const RenderStats &stats() const { return stats_; }

    // --- Picking ---
    // Cast a world-space ray against all MeshNode / AnimatedMeshNode in the tree.
    // Returns the closest hit across all nodes (result.node == nullptr = no hit).
    // Use Ray::from_screen() to build the ray from a mouse position.
    ScenePickResult pick(const Ray &ray) const;

    void release();

private:
    void renderCamera(Camera *cam);
    void preRenderNodes(Camera *cam);
    void preRenderNode(Node *node, Camera *cam);
    void debugNode(Node *node, RenderBatch *batch);
    // Frustum-cull traverse: only adds nodes whose world AABB is inside frustum
    void gatherNode(Node *node, const Frustum &frustum, RenderQueue &queue);
    // Animator update traverse
    void updateNode(Node *node, float dt);

    std::vector<Camera *>    cameras_;    // owned
    Camera                  *currentCamera_ = nullptr;
    std::vector<Node *>      roots_;
    std::vector<Technique *> techniques_; // owned
    RenderQueue              renderQueue_;
    FrameContext             frameCtx_;
    RenderStats              stats_;
    glm::vec4                clipPlane_ = {0.f, 0.f, 0.f, 0.f};
   

    std::vector<Node *>      pendingRemoval_; // flushed at the start of update()
};
