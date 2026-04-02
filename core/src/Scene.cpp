#include "Scene.hpp"
#include "TerrainNode.hpp"
#include "Effects.hpp"
#include "Animator.hpp"
#include "Batch.hpp"
#include "RenderState.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

// ─── ctor/dtor ───────────────────────────────────────────────────────────────
Scene::Scene() {}

Scene::~Scene()
{
    for (auto *c : cameras_) delete c;
    cameras_.clear();
    clear();
}

void Scene::release()
{
    for (auto *c : cameras_) delete c;
    cameras_.clear();
    currentCamera_ = nullptr;
    clear();
}

// ─── Camera ──────────────────────────────────────────────────────────────────
Camera *Scene::createCamera(const std::string &name)
{
    auto *cam = new Camera();
    cam->name = name;
    cameras_.push_back(cam);
    if (!currentCamera_) currentCamera_ = cam;
    return cam;
}

void Scene::setCurrentCamera(Camera *cam)
{
    auto it = std::find(cameras_.begin(), cameras_.end(), cam);
    if (it != cameras_.end()) currentCamera_ = cam;
}

void Scene::removeCamera(Camera *cam)
{
    auto it = std::find(cameras_.begin(), cameras_.end(), cam);
    if (it == cameras_.end()) return;
    const bool wasCurrent = (*it == currentCamera_);
    delete *it;
    cameras_.erase(it);
    if (wasCurrent)
        currentCamera_ = cameras_.empty() ? nullptr : cameras_.front();
}

// ─── Node management ─────────────────────────────────────────────────────────
MeshNode *Scene::createMeshNode(const std::string &name, Mesh *mesh)
{
    auto *n = new MeshNode(); n->name = name; n->mesh = mesh; add(n); return n;
}

AnimatedMeshNode *Scene::createAnimatedMeshNode(const std::string &name, AnimatedMesh *mesh)
{
    auto *n = new AnimatedMeshNode();
    n->name = name; n->mesh = mesh;
    n->animator = mesh ? new Animator(mesh) : nullptr;
    add(n); return n;
}

ManualMeshNode *Scene::createManualMeshNode(const std::string &name)
{
    auto *n = new ManualMeshNode(); n->name = name; add(n); return n;
}
 

void Scene::add(Node *node)
{
    if (!node) return;
    if (node->parent) node->parent->removeChild(node);
    roots_.push_back(node);
}

void Scene::remove(Node *node)
{
    auto it = std::find(roots_.begin(), roots_.end(), node);
    if (it != roots_.end()) roots_.erase(it);
}

void Scene::markForRemoval(Node *node)
{
    if (!node) return;
    if (std::find(pendingRemoval_.begin(), pendingRemoval_.end(), node)
        == pendingRemoval_.end())
        pendingRemoval_.push_back(node);
}

void Scene::clear()
{
    for (auto *n : roots_) delete n;
    roots_.clear();
}

// ─── Update ──────────────────────────────────────────────────────────────────
void Scene::update(float dt)
{
    currentCamera_->update(dt);
    for (auto *n : pendingRemoval_)
    {
        remove(n);
        if (n->parent) n->parent->removeChild(n);
        delete n;
    }
    pendingRemoval_.clear();

    for (auto *r : roots_) updateNode(r, dt);
}

void Scene::updateNode(Node *node, float dt)
{
    if (!node || !node->visible) return;
    node->update(dt);
    if (auto *an = node->asAnimatedMeshNode())
        if (an->animator && an->animator->active)
        {
            an->animator->update(dt);
            an->updateSockets();
        }
    for (auto *c : node->getChildren()) updateNode(c, dt);
}

// ─── Render ──────────────────────────────────────────────────────────────────


// ─── Gather ──────────────────────────────────────────────────────────────────
void Scene::gather(const Frustum &camFrustum, const Frustum &shadowFrustum,
                   RenderQueue &queue)
{
    for (auto *r : roots_)
        gatherNode(r, camFrustum, shadowFrustum, queue);
}

void Scene::gatherNode(Node *node, const Frustum &camFrustum,
                       const Frustum &shadowFrustum, RenderQueue &queue)
{
    if (!node || !node->visible) return;

    // Collect lights
    if (auto *light = node->asLight())
        frameCtx_.lights.push_back(light);

    // Custom nodes (terrain, particles, water, manual meshes …)
    node->gatherRenderItems(queue, frameCtx_);

    // Static mesh
    if (auto *mn = node->asMeshNode())
    {
        if (mn->mesh)
        {
            const glm::mat4   world     = mn->worldMatrix();
            const BoundingBox worldAABB = mn->mesh->aabb.transformed(world);

            const bool nodeInCam    = !worldAABB.is_valid() || camFrustum.contains(worldAABB);
            const bool nodeInShadow = !worldAABB.is_valid() || shadowFrustum.contains(worldAABB);
            if (!nodeInCam && !nodeInShadow)
            {
                for (auto *c : node->getChildren())
                    gatherNode(c, camFrustum, shadowFrustum, queue);
                return;
            }

            for (const auto &surf : mn->mesh->surfaces)
            {
                const BoundingBox surfWorld = surf.aabb.is_valid() ? surf.aabb.transformed(world) : worldAABB;
                const bool surfInCam    = surf.aabb.is_valid() ? camFrustum.contains(surfWorld)    : nodeInCam;
                const bool surfInShadow = surf.aabb.is_valid() ? shadowFrustum.contains(surfWorld) : nodeInShadow;
                if (!surfInCam && !surfInShadow) continue;

                Material *mat = mn->getMaterial();
                if (!mat)
                {
                    const auto &mats = mn->mesh->materials;
                    if (surf.material_index >= 0 && surf.material_index < (int)mats.size())
                        mat = mats[surf.material_index];
                }
                if (!mat) continue;

                RenderItem item;
                item.drawable   = mn->mesh;
                item.material   = mat;
                item.model      = world;
                item.indexStart = surf.index_start;
                item.indexCount = surf.index_count;
                item.worldAABB  = surf.aabb.is_valid() ? surf.aabb.transformed(world) : worldAABB;

                item.passMask   = mn->passMask;
                if (surfInCam) queue.add(item);

                // Shadow casters (opaque only, visible in shadow frustum)
                if (surfInShadow && !(mn->passMask & (RenderPassMask::Transparent | RenderPassMask::Flat))) queue.shadow.push_back(item);

                if (surfInCam && frameCtx_.stats) frameCtx_.stats->objects++;
            }
        }
    }

    // Skinned mesh
    if (auto *amn = node->asAnimatedMeshNode())
    {
        if (amn->mesh)
        {
            const glm::mat4   world     = amn->worldMatrix();
            const BoundingBox worldAABB = amn->mesh->aabb.transformed(world);

            const bool nodeInCamA    = !worldAABB.is_valid() || camFrustum.contains(worldAABB);
            const bool nodeInShadowA = !worldAABB.is_valid() || shadowFrustum.contains(worldAABB);
            if (!nodeInCamA && !nodeInShadowA)
            {
                for (auto *c : node->getChildren())
                    gatherNode(c, camFrustum, shadowFrustum, queue);
                return;
            }

            for (const auto &surf : amn->mesh->surfaces)
            {
                const BoundingBox surfWorld = surf.aabb.is_valid() ? surf.aabb.transformed(world) : worldAABB;
                const bool surfInCam    = surf.aabb.is_valid() ? camFrustum.contains(surfWorld)    : nodeInCamA;
                const bool surfInShadow = surf.aabb.is_valid() ? shadowFrustum.contains(surfWorld) : nodeInShadowA;
                if (!surfInCam && !surfInShadow) continue;

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
                item.indexStart = surf.index_start;
                item.indexCount = surf.index_count;
                item.worldAABB  = surfWorld;

                item.passMask = amn->passMask;
                if (surfInCam) queue.add(item);

                // Shadow casters (opaque only)
                if (surfInShadow && !(amn->passMask & (RenderPassMask::Transparent | RenderPassMask::Flat)))
                    queue.shadow.push_back(item);

                if (surfInCam && frameCtx_.stats) frameCtx_.stats->objects++;
            }
        }
    }

    for (auto *c : node->getChildren())
        gatherNode(c, camFrustum, shadowFrustum, queue);
}

// ─── Draw ────────────────────────────────────────────────────────────────────
// ── Manual pipeline: gatherScene ─────────────────────────────────────────────
void Scene::gatherScene(Camera *cam)
{
    stats_.reset();
    queue_.clear();
    frameCtx_.lights.clear();
    frameCtx_.stats  = &stats_;
    cam->updateMatrices();
    frameCtx_.camera = cam;
    frameCtx_.frustum = Frustum::from_matrix(cam->viewProjection);

    const Frustum camFrustum = frameCtx_.frustum;
    gather(camFrustum, camFrustum, queue_);
}

// ── Manual pipeline: drawPass ─────────────────────────────────────────────────
// sh is already bound and has all uniforms set by caller.
// This call only sets u_model per item and binds material textures.
void Scene::drawPass(Shader *sh, uint32_t passMask, RenderSortMode sort)
{
    auto drawList = [&](std::vector<RenderItem> items)
    {
        if (items.empty()) return;

        if (sort == RenderSortMode::FrontToBack && frameCtx_.camera)
        {
            const glm::vec3 cp = frameCtx_.camera->position;
            for (auto &i : items)
                i.depth = glm::distance(glm::vec3(i.model[3]), cp);
            std::sort(items.begin(), items.end(),
                [](const RenderItem &a, const RenderItem &b){ return a.depth < b.depth; });
        }
        else if (sort == RenderSortMode::BackToFront && frameCtx_.camera)
        {
            const glm::vec3 cp = frameCtx_.camera->position;
            for (auto &i : items)
                i.depth = glm::distance(glm::vec3(i.model[3]), cp);
            std::sort(items.begin(), items.end(),
                [](const RenderItem &a, const RenderItem &b){ return a.depth > b.depth; });
        }

        const Material *lastMat = nullptr;
        for (const auto &item : items)
        {
            if (!item.material || !item.drawable) continue;

            if (item.material != lastMat)
            {
                item.material->bindTexturesTo(sh);
                item.material->applyUniformsTo(sh);
                lastMat = item.material;
            }

            item.drawable->applyBoneMatrices(sh);
            sh->setMat4("u_model", item.model);

            if (item.indexCount > 0)
            {
                item.drawable->drawRange(item.indexStart, item.indexCount);
                stats_.drawCalls++;
                stats_.triangles += item.indexCount / 3;
            }
            else
            {
                item.drawable->draw();
                stats_.drawCalls++;
                stats_.triangles += (uint32_t)item.drawable->indexCount() / 3;
            }
        }
    };

    if (passMask & RenderPassMask::Opaque)      drawList(queue_.opaque);
    if (passMask & RenderPassMask::Transparent) drawList(queue_.transparent);
    if (passMask & RenderPassMask::Flat)         drawList(queue_.flat);
}

// ── Manual pipeline: drawShadowDepth ─────────────────────────────────────────
// Renders the shadow-caster queue with depthSh, setting u_lightSpace.
void Scene::drawShadowDepth(Shader *depthSh, const glm::mat4 &lightSpace)
{
    if (queue_.shadow.empty() || !depthSh) return;

    auto &rs = RenderState::instance();
    rs.useProgram(depthSh->getId());
    depthSh->setMat4("u_lightSpace", lightSpace);

    for (const auto &item : queue_.shadow)
    {
        if (!item.drawable) continue;
        depthSh->setMat4("u_model", item.model);
        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }
}
  
// ─── drawSky ────────────────────────────────────────────────────────────
void Scene::drawSky()
{
    if (!skyShader || !frameCtx_.camera) return;

    auto &rs = RenderState::instance();
    rs.setDepthTest(true);
    rs.setDepthWrite(false);
    rs.setCull(false);
    rs.setBlend(false);

    const Camera *cam = frameCtx_.camera;
    rs.useProgram(skyShader->getId());
    skyShader->setMat4("u_view",       cam->view);
    skyShader->setMat4("u_proj",       cam->projection);
    skyShader->setMat4("u_viewProj",   cam->viewProjection);
    skyShader->setMat4("u_invViewProj",glm::inverse(cam->viewProjection));
    skyShader->setVec4("u_cameraPos",  glm::vec4(cam->position, 1.f));

    // Fullscreen triangle — no VAO geometry needed
    static GLuint dummyVAO = 0;
    if (!dummyVAO) glGenVertexArrays(1, &dummyVAO);
    glBindVertexArray(dummyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    rs.setDepthWrite(true);
    rs.setCull(true);
}

// ─── Debug ───────────────────────────────────────────────────────────────────
void Scene::debug(RenderBatch *batch)
{
    if (!batch) return;
    for (auto *r : roots_) debugNode(r, batch);
}

void Scene::debugNode(Node *node, RenderBatch *batch)
{
    if (!node || !node->visible || !batch) return;

    if (auto *mn = node->asMeshNode())
    {
        if (mn->mesh && mn->mesh->aabb.is_valid())
        {
            const glm::mat4 world = mn->worldMatrix();
            bool drew = false;
            for (const auto &s : mn->mesh->surfaces)
            {
                if (!s.aabb.is_valid()) continue;
                batch->SetColor(64, 255, 64, 255);
                batch->Box(s.aabb.transformed(world));
                drew = true;
            }
            if (!drew) { batch->SetColor(0,255,0,255); batch->Box(mn->mesh->aabb.transformed(world)); }
        }
    }
    else if (auto *amn = node->asAnimatedMeshNode())
    {
        if (amn->mesh && amn->mesh->aabb.is_valid())
        {
            const glm::mat4 world = amn->worldMatrix();
            bool drew = false;
            for (const auto &s : amn->mesh->surfaces)
            {
                if (!s.aabb.is_valid()) continue;
                batch->SetColor(64, 200, 255, 255);
                batch->Box(s.aabb.transformed(world));
                drew = true;
            }
            if (!drew) { batch->SetColor(0,200,255,255); batch->Box(amn->mesh->aabb.transformed(world)); }
        }
    }
    else if (auto *terrain = dynamic_cast<TerrainLodNode *>(node))
    {
        terrain->debug(batch);
    }

    for (auto *c : node->getChildren()) debugNode(c, batch);
}

// ─── Pick ─────────────────────────────────────────────────────────────────────
static void pickNode(Node *node, const Ray &ray, ScenePickResult &best)
{
    if (!node || !node->visible) return;
    if (auto *mn = node->asMeshNode())
        if (mn->mesh)
        {
            PickResult r = mn->mesh->pick(ray, mn->worldMatrix());
            if (r.hit && r.distance < best.result.distance) { best.result = r; best.node = mn; }
        }
    if (auto *amn = node->asAnimatedMeshNode())
        if (amn->mesh)
        {
            PickResult r = amn->mesh->pick(ray, amn->worldMatrix());
            if (r.hit && r.distance < best.result.distance) { best.result = r; best.node = amn; }
        }
    for (auto *c : node->getChildren()) pickNode(c, ray, best);
}

ScenePickResult Scene::pick(const Ray &ray) const
{
    ScenePickResult best;
    best.result.distance = std::numeric_limits<float>::max();
    for (auto *r : roots_) pickNode(r, ray, best);
    return best;
}
