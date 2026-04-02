#include "Scene.hpp"
#include "TerrainNode.hpp"
#include "Animator.hpp"
#include "Batch.hpp"
#include "Manager.hpp"
#include "Effects.hpp"
#include "WaterNode.hpp"
#include "RenderState.hpp"
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#define MSF_GIF_IMPL
#include "msf_gif.h"

Scene::Scene()
{
   
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

VertexAnimMeshNode *Scene::createVertexAnimMeshNode(const std::string &name, Mesh *mesh)
{
    auto *node = new VertexAnimMeshNode();
    node->name = name;
    node->mesh = mesh;
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

WaterNode3D *Scene::createWaterNode(const std::string &name)
{
    auto *node = new WaterNode3D();
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
    {
        // Swap-and-pop: O(1) instead of O(n) erase
        *it = roots_.back();
        roots_.pop_back();
    }
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

    //glDepthFunc(GL_GREATER);      // invertido — passa se MAIOR (longe)
   // glDepthFunc(GL_LESS);


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

    if (auto *vn = node->asVertexAnimMeshNode())
    {
        vn->controller.update(dt);
        vn->updateTagSockets();
    }

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
 
}

// ============================================================
//  Light-only pre-gather
// ============================================================
void Scene::gatherLightsOnly(Node *node)
{
    if (!node || !node->visible) return;
    if (auto *l = node->asLight())
        frameCtx_.lights.push_back(l);
    for (auto *c : node->getChildren())
        gatherLightsOnly(c);
}

void Scene::renderCamera(Camera *cam)
{
    cam->updateMatrices();

    // Set up base context (frustum etc.) so preRenderNodes can use it
    frameCtx_.camera   = cam;
    frameCtx_.viewport = cam->viewport;
    frameCtx_.secondary = false;
    frameCtx_.frustum  = Frustum::from_matrix(cam->viewProjection);
    frameCtx_.stats    = &stats_;
    stats_.reset();

    // Pre-gather lights so water's preRender has correct lighting this frame
    frameCtx_.lights.clear();
    for (auto *root : roots_)
        gatherLightsOnly(root);

    preRenderNodes(cam);

    // Main gather — re-clears lights (gatherNode re-adds them) to avoid duplicates
    frameCtx_.lights.clear();
    renderQueue_.clear();
    frameCtx_.shadowQueue.clear();
    for (auto *node : roots_)
        gatherNode(node, frameCtx_.frustum, renderQueue_);

    // Shadow queue: cull against the light frustum (NOT camera frustum).
    // A caster behind the camera can still cast shadows into the view.
    // CsmTechnique returns the widest cascade frustum; others return infinite.
    {
        const Frustum savedFrustum = frameCtx_.frustum;
        Frustum shadowFrustum      = Frustum::infinite();
        for (auto *t : techniques_)
        {
            Frustum f = t->getShadowCasterFrustum();
            // Use first technique that provides a non-infinite frustum
            if (!f.isInfinite()) { shadowFrustum = f; break; }
        }
        frameCtx_.frustum = shadowFrustum;
        for (auto *node : roots_)
            gatherNode(node, shadowFrustum, frameCtx_.shadowQueue);
        frameCtx_.frustum = savedFrustum;
    }

    for (auto *t : techniques_)
        t->render(frameCtx_, renderQueue_);
}

void Scene::preRenderNodes(Camera *cam)
{
    for (auto *node : roots_)
        preRenderNode(node, cam);
}

void Scene::preRenderNode(Node *node, Camera *cam)
{
    if (!node || !node->visible) return;
    node->preRender(this, cam);
    for (auto *child : node->getChildren())
        preRenderNode(child, cam);
}

void Scene::renderToTarget(Camera *tmpCam, RenderTarget *rt)
{
    if (!tmpCam || !rt || !rt->valid()) return;
 

    // Bind FBO and clear
    rt->bind();
    auto &rs = RenderState::instance();
    rs.setViewport(0, 0, rt->width(), rt->height());
    const glm::vec4 &cc2 = tmpCam->clearColorVal;
    rs.setClearColor(cc2.r, cc2.g, cc2.b, cc2.a);
    rs.clear(tmpCam->clearColor, tmpCam->clearDepth);

    // Build secondary FrameContext
    FrameContext ctx;
    ctx.camera      = tmpCam;
    ctx.secondary   = true;   // skip CSM depth passes
    ctx.lights      = frameCtx_.lights;
    ctx.viewport    = {0, 0, rt->width(), rt->height()};
    ctx.frustum     = Frustum::from_matrix(tmpCam->viewProjection);
    ctx.stats       = nullptr;
    ctx.shadowQueue = frameCtx_.shadowQueue;
    ctx.clipPlanes     = clipPlanes_;
    ctx.clipPlaneCount = clipPlaneCount_;
    clipPlaneCount_ = 0; // auto-reset after use


    // Gather + render (no recursive preRender calls).
    // Temporarily propagate secondary flag into frameCtx_ so that
    // gatherNode → gatherRenderItems (which reads frameCtx_) will
    // see secondary=true and skip nodes like WaterNode3D.
    const bool prevSecondary = frameCtx_.secondary;
    frameCtx_.secondary = true;

    rtQueue_.clear();
    for (auto *node : roots_)
    {
         
            gatherNode(node, ctx.frustum, rtQueue_);
        
    }
    

    frameCtx_.secondary = prevSecondary;

    for (auto *t : techniques_)
        t->render(ctx, rtQueue_);

    rt->unbind();
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

    // TerrainLodNode: draw patch AABBs coloured by LOD
    else if (auto *terrain = dynamic_cast<TerrainLodNode *>(node))
    {
        terrain->debug(batch);
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
            // Broad phase: ray-AABB test before expensive triangle intersection
            const glm::mat4 world = mn->worldMatrix();
            const BoundingBox worldAABB = mn->mesh->aabb.transformed(world);
            if (worldAABB.is_valid())
            {
                float aabbHit = worldAABB.intersects_ray(ray.origin, ray.direction);
                if (aabbHit < 0.f || aabbHit >= best.result.distance)
                    goto pickChildren;
            }
            {
                PickResult r = mn->mesh->pick(ray, world);
                if (r.hit && r.distance < best.result.distance)
                {
                    best.result = r;
                    best.node   = mn;
                }
            }
        }
    }
    else if (auto *amn = node->asAnimatedMeshNode())
    {
        if (amn->mesh)
        {
            const glm::mat4 world = amn->worldMatrix();
            const BoundingBox worldAABB = amn->mesh->aabb.transformed(world);
            if (worldAABB.is_valid())
            {
                float aabbHit = worldAABB.intersects_ray(ray.origin, ray.direction);
                if (aabbHit < 0.f || aabbHit >= best.result.distance)
                    goto pickChildren;
            }
            {
                PickResult r = amn->mesh->pick(ray, world);
                if (r.hit && r.distance < best.result.distance)
                {
                    best.result = r;
                    best.node   = amn;
                }
            }
        }
    }

pickChildren:
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
