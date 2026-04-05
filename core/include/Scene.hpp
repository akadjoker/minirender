#pragma once
#include "Node.hpp"
#include "RenderScene.hpp"
#include "Math.hpp"
#include <vector>

class RenderBatch;
class TerrainLodNode;
class WaterNode3D;

// Result of a scene-level pick: closest hit across all pickable nodes.
struct ScenePickResult
{
    PickResult  result;           // geometry hit details (hit, distance, point, normal ...)
    Node3D     *node    = nullptr; // the node that was hit (nullptr = no hit)
};

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
    VertexAnimMeshNode   *createVertexAnimMeshNode(const std::string &name = "", Mesh *mesh = nullptr);
    TerrainLodNode       *createTerrainLodNode  (const std::string &name = "");
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

    // --- Per-frame API ---
    void update(float dt);
    void buildRenderScene(Camera *camera, RenderScene &outScene, const Node *ignoredNode = nullptr);
    void setSkyEnabled(bool enabled) { sky_.enabled = enabled; }
    void setSkyColors(const glm::vec3 &top, const glm::vec3 &horizon, const glm::vec3 &ground)
    {
        sky_.top = top;
        sky_.horizon = horizon;
        sky_.ground = ground;
    }
    const SkySettings &sky() const { return sky_; }

    void debug(RenderBatch *batch);

    // --- Picking ---
    // Cast a world-space ray against all MeshNode / AnimatedMeshNode in the tree.
    // Returns the closest hit across all nodes (result.node == nullptr = no hit).
    // Use Ray::from_screen() to build the ray from a mouse position.
    ScenePickResult pick(const Ray &ray) const;

    void release();

private:
    void debugNode(Node *node, RenderBatch *batch);
    // Animator update traverse
    void updateNode(Node *node, float dt);

    std::vector<Camera *>    cameras_;    // owned
    Camera                  *currentCamera_ = nullptr;
    std::vector<Node *>      roots_;
    std::vector<Node *>      pendingRemoval_; // flushed at the start of update()
    SkySettings              sky_;
};
