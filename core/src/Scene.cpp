#include "Scene.hpp"
#include "Animator.hpp"
#include "Batch.hpp"
#include "Manager.hpp"
#include "Effects.hpp"
#include <algorithm>

Scene::Scene()
{
    sceneUBO_.create();
}

Scene::~Scene()
{
    for (auto *cam : cameras_)
        delete cam;
    cameras_.clear();
    clearTechniques();
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

ManualMeshNode *Scene::createManualMeshNode(const std::string &name)
{
    auto *node = new ManualMeshNode();
    node->name = name;
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

void Scene::markForRemoval(Node *node)
{
    if (!node) return;
    // Only enqueue once
    if (std::find(pendingRemoval_.begin(), pendingRemoval_.end(), node)
        == pendingRemoval_.end())
        pendingRemoval_.push_back(node);
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
    if (!currentCamera_)
        currentCamera_ = cam;
    return cam;
}

void Scene::setCurrentCamera(Camera *cam)
{
    if (!cam)
    {
        currentCamera_ = nullptr;
        return;
    }

    auto it = std::find(cameras_.begin(), cameras_.end(), cam);
    if (it != cameras_.end())
        currentCamera_ = cam;
}

void Scene::removeCamera(Camera *cam)
{
    auto it = std::find(cameras_.begin(), cameras_.end(), cam);
    if (it != cameras_.end())
    {
        const bool removingCurrent = (*it == currentCamera_);
        delete *it;
        cameras_.erase(it);
        if (removingCurrent)
            currentCamera_ = cameras_.empty() ? nullptr : cameras_.front();
    }
}

void Scene::render()
{
    if (currentCamera_)
    {
        renderCamera(currentCamera_);
        return;
    }

    for (auto *cam : cameras_)
        renderCamera(cam);
}

void Scene::debug(RenderBatch *batch)
{
    if (!batch)
        return;

    for (auto *root : roots_)
        debugNode(root, batch);
}

void Scene::update(float dt)
{
    // Flush deferred removals before updating
    for (auto *node : pendingRemoval_)
    {
        remove(node);
        // Also remove from any parent's children list
        if (node->parent)
            node->parent->removeChild(node);
        delete node;
    }
    pendingRemoval_.clear();

    for (auto *root : roots_)
        updateNode(root, dt);
}

void Scene::updateNode(Node *node, float dt)
{
    if (!node || !node->visible) return;
    // Generic per-node simulation (particles, etc.)
    node->update(dt);
    if (auto *an = node->asAnimatedMeshNode())
        if (an->animator && an->animator->active)
        {
            an->animator->update(dt);
            an->updateSockets(); // atualizar attachments de bones
        }
    for (auto *child : node->getChildren())
        updateNode(child, dt);
}

void Scene::release()
{
    for (auto *cam : cameras_)
        delete cam;
    cameras_.clear();  // prevent double-delete if destructor runs after release()
    currentCamera_ = nullptr;

    clearTechniques(); // deletes owned technique objects
    clear();           // deletes owned nodes, clears roots_
    sceneUBO_.destroy();
}

void Scene::renderCamera(Camera *cam)
{
    // Bake matrices once — view, projection, viewProjection all cached on cam
    cam->updateMatrices();

    // Upload scene-wide uniforms to UBO binding 0 once per frame.
    // Demo fills sceneData.lightDir etc.; we fill the camera-derived fields here.
    sceneData.view        = cam->view;
    sceneData.proj        = cam->projection;
    sceneData.viewProj    = cam->viewProjection;
    sceneData.invViewProj = glm::inverse(cam->viewProjection);
    sceneData.cameraPos   = glm::vec4(cam->position, 1.0f);
    sceneUBO_.upload(sceneData);

    stats_.reset();
    frameCtx_.camera   = cam;
    frameCtx_.viewport = cam->viewport;
    frameCtx_.lights.clear();              // refilled by gatherNode below
    frameCtx_.frustum  = Frustum::from_matrix(cam->viewProjection);
    frameCtx_.stats    = &stats_;

    renderQueue_.clear();
    frameCtx_.shadowQueue.clear();
    for (auto *node : roots_)
        gatherNode(node, frameCtx_.frustum, renderQueue_);

    // Shadow casters: gather ALL opaque nodes ignoring camera frustum
    {
        Frustum inf = Frustum::infinite();
        for (auto *node : roots_)
            gatherNode(node, inf, frameCtx_.shadowQueue);
    }

    for (auto *t : techniques_)
        t->render(frameCtx_, renderQueue_);
}

void Scene::debugNode(Node *node, RenderBatch *batch)
{
    if (!node || !node->visible || !batch)
        return;

    if (auto *meshNode = node->asMeshNode())
    {
        if (meshNode->mesh && meshNode->mesh->aabb.is_valid())
        {
            const glm::mat4 world = meshNode->worldMatrix();
            bool drewSurfaceBoxes = false;

            for (const auto &surface : meshNode->mesh->surfaces)
            {
                 if (!surface.aabb.is_valid())
                     continue;

                batch->SetColor(64, 255, 64, 255);
                batch->Box(surface.aabb.transformed(world));
                drewSurfaceBoxes = true;
            }

            if (!drewSurfaceBoxes)
            {
                batch->SetColor(0, 255, 0, 255);
                batch->Box(meshNode->mesh->aabb.transformed(world));
            }
        }
    }
    else if (auto *animatedNode = node->asAnimatedMeshNode())
    {
        if (animatedNode->mesh && animatedNode->mesh->aabb.is_valid())
        {
            const glm::mat4 world = animatedNode->worldMatrix();
            bool drewSurfaceBoxes = false;

            for (const auto &surface : animatedNode->mesh->surfaces)
            {
                if (!surface.aabb.is_valid())
                    continue;

                batch->SetColor(64, 200, 255, 255);
                batch->Box(surface.aabb.transformed(world));
                drewSurfaceBoxes = true;
            }

            if (!drewSurfaceBoxes)
            {
                batch->SetColor(0, 200, 255, 255);
                batch->Box(animatedNode->mesh->aabb.transformed(world));
            }
        }
    }

    for (auto *child : node->getChildren())
        debugNode(child, batch);
}

void Scene::gatherNode(Node *node, const Frustum &frustum, RenderQueue &queue)
{
    if (!node || !node->visible)
        return;

    // Lights are nodes too — collect into frame context
    if (auto *light = node->asLight())
        frameCtx_.lights.push_back(light);

    // Custom nodes (terrains, particles, etc.) gather their own items
    node->gatherRenderItems(queue, frameCtx_);

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
                    gatherNode(child, frustum, queue);
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
                queue.add(item);
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
                    gatherNode(child, frustum, queue);
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
                queue.add(item);
            }
        }
    }

    for (auto *child : node->getChildren())
        gatherNode(child, frustum, queue);
}

// ============================================================
//  Scene::pick
// ============================================================
static void pickNode(Node *node, const Ray &ray, ScenePickResult &best)
{
    if (!node || !node->visible) return;

    if (auto *mn = node->asMeshNode())
    {
        if (mn->mesh)
        {
            PickResult r = mn->mesh->pick(ray, mn->worldMatrix());
            if (r.hit && r.distance < best.result.distance)
            {
                best.result = r;
                best.node   = mn;
            }
        }
    }
    else if (auto *amn = node->asAnimatedMeshNode())
    {
        if (amn->mesh)
        {
            PickResult r = amn->mesh->pick(ray, amn->worldMatrix());
            if (r.hit && r.distance < best.result.distance)
            {
                best.result = r;
                best.node   = amn;
            }
        }
    }

    for (auto *child : node->getChildren())
        pickNode(child, ray, best);
}

ScenePickResult Scene::pick(const Ray &ray) const
{
    ScenePickResult best;
    best.result.distance = std::numeric_limits<float>::max();
    for (auto *root : roots_)
        pickNode(root, ray, best);
    return best;
}
