#pragma once
#include "Node.hpp"
#include "RenderPipeline.hpp"
#include "Math.hpp"
#include <vector>

// Scene manages the node tree and dispatches per-frame rendering
// through a configurable list of Technique* (non-owning).
class Scene
{
public:
    Scene()  ;
    ~Scene();

    // --- Environment ---
    // Lights are nodes in the tree — add via createLight<PointLight>("name") etc.

    // --- Camera management (scene owns cameras) ---
    Camera *createCamera(const std::string &name = "");
    void    removeCamera(Camera *cam);
    const std::vector<Camera *> &cameras() const { return cameras_; }

    // --- Node management ---
    MeshNode *createMeshNode(const std::string &name = "", Mesh *mesh = nullptr);

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

    // --- Technique list (non-owning, executed in order) ---
    void addTechnique(Technique *t) { techniques_.push_back(t); }
    void clearTechniques()          { techniques_.clear(); }

    // --- Per-frame API ---
    // Gather visible nodes (frustum cull) + render all cameras
    void render();

    // Debug stats accumulated during the last render() call
    const RenderStats &stats() const { return stats_; }

    void release();

private:
    void renderCamera(Camera *cam);
    // Frustum-cull traverse: only adds nodes whose world AABB is inside frustum
    void gatherNode(Node *node, const Frustum &frustum);

    std::vector<Camera *>    cameras_;    // owned
    std::vector<Node *>      roots_;
    std::vector<Technique *> techniques_; // non-owning
    RenderQueue              renderQueue_;
    FrameContext             frameCtx_;
    RenderStats              stats_;
};
