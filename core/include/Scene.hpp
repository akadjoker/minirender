#pragma once
#include "Node.hpp"
#include "Math.hpp"
#include <vector>

class RenderBatch;
class TerrainLodNode;
class Shader;

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
    Camera *createFreeCamera(const std::string &name,
                             int viewportWidth, int viewportHeight,
                             const glm::vec3 &position,
                             const glm::vec3 &target,
                             float moveSpeed = 8.0f,
                             float mouseSensitivity = 0.15f,
                             float sprintMultiplier = 2.5f);
    void    removeCamera(Camera *cam);
    const std::vector<Camera *> &cameras() const { return cameras_; }
    Camera *currentCamera() const { return currentCamera_; }
    void    setCurrentCamera(Camera *cam);
    void    setCamera(Camera *cam);

    // --- Pass state (prepared for script/manual pass control) ---
    void    beginPass();
    void    endPass();
    bool    passActive() const { return passActive_; }
    void    setShader(Shader *shader);
    Shader *currentShader() const { return currentShader_; }
    void    render(RenderType type);

    // --- Node management ---
    MeshNode             *createMeshNode        (const std::string &name = "", Mesh *mesh = nullptr);
    AnimatedMeshNode     *createAnimatedMeshNode(const std::string &name = "", AnimatedMesh *mesh = nullptr);
    VertexAnimatedMeshNode *createVertexAnimatedMeshNode(const std::string &name = "", VertexAnimatedMesh *mesh = nullptr);
 
    TerrainLodNode       *createTerrainLodNode  (const std::string &name = "");

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
    const std::vector<RenderableNode *> &renderables() const { return renderables_; }
    const std::vector<RenderableNode *> &renderables(RenderType type) const;

    void debug(RenderBatch *batch);

    // --- Picking ---
    // Cast a world-space ray against all MeshNode / AnimatedMeshNode in the tree.
    // Returns the closest hit across all nodes (result.node == nullptr = no hit).
    // Use Ray::from_screen() to build the ray from a mouse position.
    ScenePickResult pick(const Ray &ray) const;

    void release();

private:
    friend class Node;
    void debugNode(Node *node, RenderBatch *batch);
    // Animator update traverse
    void updateNode(Node *node, float dt);
    void collectRenderables(Node *node);
    void attachRecursive(Node *node);
    void detachRecursive(Node *node);
    void onChildAttached(Node *node);
    void onChildDetached(Node *node);
 
    void resetPassState();

    std::vector<Camera *>    cameras_;    // owned
    Camera                  *currentCamera_ = nullptr;
    std::vector<Node *>      roots_;
    std::vector<Node *>      pendingRemoval_; // flushed at the start of update()
    bool                     passActive_ = false;
    Shader                  *currentShader_ = nullptr;
    std::vector<RenderableNode *> renderables_;
    std::vector<RenderableNode *> solidNodes_;
    std::vector<RenderableNode *> transparentNodes_;
    std::vector<RenderableNode *> lightmapNodes_;
    std::vector<RenderableNode *> terrainNodes_;
    std::vector<RenderableNode *> skyboxNodes_;
    std::vector<RenderableNode *> specialNodes_;
    std::vector<RenderableNode *> overlayNodes_;
    std::vector<RenderableNode *> skinningNodes_;
};
