#include "Scene.hpp"
#include "Animator.hpp"
#include "Manager.hpp"
#include <algorithm>

Scene::Scene()
{
}

Scene::~Scene()
{
    for (auto *cam : cameras_)
        delete cam;
    cameras_.clear();
    clear();
}

MeshNode *Scene::createMeshNode(const std::string &name, Mesh *mesh)
{
    auto *node = new MeshNode();
    node->name = name;
    node->mesh = mesh;

    add(node);
    return node;
}

AnimatedMeshNode *Scene::createAnimatedMeshNode(const std::string &name, AnimatedMesh *mesh)
{
    auto *node     = new AnimatedMeshNode();
    node->name     = name;
    node->mesh     = mesh;
    node->animator = mesh ? new Animator(mesh) : nullptr;
    add(node);
    return node;
}

void Scene::add(Node *node)
{
    if (!node)
        return;
    if (node->parent)
        node->parent->removeChild(node);
    roots_.push_back(node);
}

void Scene::remove(Node *node)
{
    auto it = std::find(roots_.begin(), roots_.end(), node);
    if (it != roots_.end())
        roots_.erase(it);
}

void Scene::clear()
{
    for (auto *node : roots_)
        delete node;
    roots_.clear();
}

Camera *Scene::createCamera(const std::string &name)
{
    auto *cam = new Camera();
    cam->name = name;
    cameras_.push_back(cam);
    return cam;
}

void Scene::removeCamera(Camera *cam)
{
    auto it = std::find(cameras_.begin(), cameras_.end(), cam);
    if (it != cameras_.end())
    {
        delete *it;
        cameras_.erase(it);
    }
}

void Scene::render()
{
    for (auto *cam : cameras_)
        renderCamera(cam);
}

void Scene::update(float dt)
{
    for (auto *root : roots_)
        updateNode(root, dt);
}

void Scene::updateNode(Node *node, float dt)
{
    if (!node || !node->visible) return;
    if (auto *an = node->asAnimatedMeshNode())
        if (an->animator && an->animator->active)
            an->animator->update(dt);
    for (auto *child : node->getChildren())
        updateNode(child, dt);
}

void Scene::release()
{
    for (auto *cam : cameras_)
        delete cam;
    cameras_.clear();  // prevent double-delete if destructor runs after release()

    clear();           // deletes owned nodes, clears roots_
}

void Scene::renderCamera(Camera *cam)
{
    // Bake matrices once — view, projection, viewProjection all cached on cam
    cam->updateMatrices();

    stats_.reset();
    frameCtx_.camera   = cam;
    frameCtx_.viewport = cam->viewport;
    frameCtx_.lights.clear();              // refilled by gatherNode below
    frameCtx_.frustum  = Frustum::from_matrix(cam->viewProjection);
    frameCtx_.stats    = &stats_;

    renderQueue_.clear();
    for (auto *node : roots_)
        gatherNode(node, frameCtx_.frustum);

    for (auto *t : techniques_)
        t->render(frameCtx_, renderQueue_);
}

void Scene::gatherNode(Node *node, const Frustum &frustum)
{
    if (!node || !node->visible)
        return;

    // Lights are nodes too — collect into frame context
    if (auto *light = node->asLight())
        frameCtx_.lights.push_back(light);

    // ── Static mesh ─────────────────────────────────────────────────
    if (auto *meshNode = node->asMeshNode())
    {
        if (meshNode->mesh)
        {
            const glm::mat4 world    = meshNode->worldMatrix();
            const BoundingBox worldAABB = meshNode->mesh->aabb.transformed(world);

            if (worldAABB.is_valid() && !frustum.contains(worldAABB))
            {
                for (auto *child : node->getChildren())
                    gatherNode(child, frustum);
                return;
            }

            for (const auto &surf : meshNode->mesh->surfaces)
            {
                if (surf.aabb.is_valid())
                {
                    const BoundingBox surfWorld = surf.aabb.transformed(world);
                    if (!frustum.contains(surfWorld))
                        continue;
                }

                Material *mat = meshNode->getMaterial();
                if (!mat)
                {
                    const auto &mats = meshNode->mesh->materials;
                    if (surf.material_index >= 0 && surf.material_index < (int)mats.size())
                        mat = mats[surf.material_index];
                }
                if (!mat) continue;

                RenderItem item;
                item.drawable   = meshNode->mesh;
                item.material   = mat;
                item.model      = world;
                item.passMask   = meshNode->passMask;
                item.indexStart = surf.index_start;
                item.indexCount = surf.index_count;
                item.worldAABB  = surf.aabb.is_valid() ? surf.aabb.transformed(world) : worldAABB;
                renderQueue_.add(item);
            }
        }
    }

    // ── Skinned mesh ───────────────────────────────────────────────
    if (auto *amn = node->asAnimatedMeshNode())
    {
        if (amn->mesh)
        {
            const glm::mat4 world     = amn->worldMatrix();
            const BoundingBox worldAABB = amn->mesh->aabb.transformed(world);

            if (worldAABB.is_valid() && !frustum.contains(worldAABB))
            {
                for (auto *child : node->getChildren())
                    gatherNode(child, frustum);
                return;
            }

            for (const auto &surf : amn->mesh->surfaces)
            {
                Material *mat = amn->getMaterial();
                if (!mat)
                {
                    const auto &mats = amn->mesh->materials;
                    if (surf.material_index >= 0 && surf.material_index < (int)mats.size())
                        mat = mats[surf.material_index];
                }
                if (!mat) continue;

                RenderItem item;
                item.drawable   = amn->mesh;
                item.material   = mat;
                item.model      = world;
                item.passMask   = amn->passMask;
                item.indexStart = surf.index_start;
                item.indexCount = surf.index_count;
                item.worldAABB  = surf.aabb.is_valid() ? surf.aabb.transformed(world) : worldAABB;
                renderQueue_.add(item);
            }
        }
    }

    for (auto *child : node->getChildren())
        gatherNode(child, frustum);
}
