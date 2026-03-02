#include "Scene.hpp"
#include "TerrainNode.hpp"
#include "Animator.hpp"
#include "Batch.hpp"
#include "Effects.hpp"
#include "WaterNode.hpp"
#include "RenderState.hpp"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#define MSF_GIF_IMPL
#include "msf_gif.h"

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
    shadowMap_.destroy();
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

WaterNode3D *Scene::createWaterNode(const std::string &name)
{
    auto *n = new WaterNode3D(); n->name = name; add(n); return n;
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
void Scene::render()
{
    if (currentCamera_)  { renderCamera(currentCamera_); return; }
    for (auto *c : cameras_) renderCamera(c);
}

void Scene::renderCamera(Camera *cam)
{
    cam->updateMatrices();

    auto &rs = RenderState::instance();

    // Build context
    frameCtx_.camera    = cam;
    frameCtx_.viewport  = cam->viewport;
    frameCtx_.secondary = false;
    frameCtx_.frustum   = Frustum::from_matrix(cam->viewProjection);
    frameCtx_.clipPlane = glm::vec4(0.f);
    frameCtx_.stats     = &stats_;
    stats_.reset();

    // 1. preRender (WaterNode builds its reflection/refraction RTs here)
    for (auto *r : roots_) preRenderNode(r, cam);

    // 2. Gather — fills queue_.opaque, queue_.transparent, queue_.shadow
    frameCtx_.lights.clear();
    queue_.clear();
    gather(frameCtx_.frustum, Frustum::infinite(), queue_);

    // 3. Shadow depth pass (must run before main draw, uses queue_.shadow)
    frameCtx_.shadowTex = 0;
    if (shadow.enabled && shadow.depthShader && !queue_.shadow.empty())
    {
        drawShadowPass();
        frameCtx_.lightSpaceMatrix = lightSpaceMatrix_;
        frameCtx_.shadowTex        = shadowMap_.depthTexId();
        frameCtx_.lightDir         = glm::normalize(shadow.lightDir);
        frameCtx_.lightColor       = shadow.lightColor;
        frameCtx_.shadowBias       = shadow.bias;
        // Restore main framebuffer and viewport
        rs.setViewport(frameCtx_.viewport.x, frameCtx_.viewport.y,
                       frameCtx_.viewport.z, frameCtx_.viewport.w);
    }

    // 4. Draw passes in order
    rs.setViewport(frameCtx_.viewport.x, frameCtx_.viewport.y,
                   frameCtx_.viewport.z, frameCtx_.viewport.w);

    // Opaque — front-to-back
    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setCull(true);
    rs.setBlend(false);
    drawItems(queue_.opaque, frameCtx_, RenderSortMode::FrontToBack);

    // Sky — after opaque so early-Z kills hidden sky pixels
    drawSky(frameCtx_);

    // Transparent — back-to-front, no depth write
    rs.setDepthTest(true);
    rs.setDepthWrite(false);
    rs.setBlend(true);
    rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawItems(queue_.transparent, frameCtx_, RenderSortMode::BackToFront);

    rs.setDepthWrite(true);
    rs.setBlend(false);
}

// ─── renderToTarget ──────────────────────────────────────────────────────────
// Used by WaterNode for reflection / refraction.
void Scene::renderToTarget(Camera *cam, RenderTarget *rt, glm::vec4 clipPlane)
{
    if (!cam || !rt || !rt->valid()) return;

    cam->updateMatrices();

    rt->bind();
    auto &rs = RenderState::instance();
    rs.setViewport(0, 0, rt->width(), rt->height());
    rs.setClearColor(cam->clearColorVal.r, cam->clearColorVal.g,
                     cam->clearColorVal.b, cam->clearColorVal.a);
    rs.clear(cam->clearColor, cam->clearDepth);

    FrameContext ctx;
    ctx.camera    = cam;
    ctx.viewport  = {0, 0, rt->width(), rt->height()};
    ctx.secondary = true;
    ctx.lights    = frameCtx_.lights;
    ctx.frustum   = Frustum::from_matrix(cam->viewProjection);
    ctx.stats     = nullptr;
    ctx.clipPlane = clipPlane;

    // Gather with secondary flag so WaterNode skips itself
    const Frustum savedFrustum  = frameCtx_.frustum;
    const bool    savedSecondary = frameCtx_.secondary;
    frameCtx_.frustum   = ctx.frustum;
    frameCtx_.secondary = true;

    RenderQueue q;
    gather(ctx.frustum, Frustum::infinite(), q);

    frameCtx_.frustum   = savedFrustum;
    frameCtx_.secondary = savedSecondary;

    // Draw opaque + sky + transparent into RT
    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setCull(true);
    rs.setBlend(false);
    drawItems(q.opaque, ctx, RenderSortMode::FrontToBack);

    drawSky(ctx);

    rs.setDepthTest(true);
    rs.setDepthWrite(false);
    rs.setBlend(true);
    rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawItems(q.transparent, ctx, RenderSortMode::BackToFront);
    rs.setDepthWrite(true);
    rs.setBlend(false);

    rt->unbind();

    // Restore viewport
    rs.setViewport(frameCtx_.viewport.x, frameCtx_.viewport.y,
                   frameCtx_.viewport.z, frameCtx_.viewport.w);
}

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

            if (worldAABB.is_valid() && !camFrustum.contains(worldAABB))
            {
                for (auto *c : node->getChildren())
                    gatherNode(c, camFrustum, shadowFrustum, queue);
                return;
            }

            for (const auto &surf : mn->mesh->surfaces)
            {
                if (surf.aabb.is_valid() && !camFrustum.contains(surf.aabb.transformed(world)))
                    continue;

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
                queue.add(item);

                // Shadow casters (opaque only)
                if (!(mn->passMask & RenderPassMask::Transparent)) queue.shadow.push_back(item);

                if (frameCtx_.stats) frameCtx_.stats->objects++;
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

            if (worldAABB.is_valid() && !camFrustum.contains(worldAABB))
            {
                for (auto *c : node->getChildren())
                    gatherNode(c, camFrustum, shadowFrustum, queue);
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
                item.indexStart = surf.index_start;
                item.indexCount = surf.index_count;
                item.worldAABB  = surf.aabb.is_valid() ? surf.aabb.transformed(world) : worldAABB;

                item.passMask   = amn->passMask;
                queue.add(item);
                if (frameCtx_.stats) frameCtx_.stats->objects++;
            }
        }
    }

    for (auto *c : node->getChildren())
        gatherNode(c, camFrustum, shadowFrustum, queue);
}

// ─── Draw ────────────────────────────────────────────────────────────────────
void Scene::drawItems(const std::vector<RenderItem> &items,
                      const FrameContext &ctx, RenderSortMode sort)
{
    if (items.empty()) return;

    auto &rs = RenderState::instance();
    const glm::vec3 camPos = ctx.camera->position;

    // Sort (in-place copy for sorting)
    std::vector<RenderItem> sorted = items;
    if (sort == RenderSortMode::FrontToBack)
    {
        for (auto &i : sorted)
            i.depth = glm::distance(glm::vec3(i.model[3]), camPos);
        std::sort(sorted.begin(), sorted.end(), [](const RenderItem &a, const RenderItem &b)
        {
            if (a.material != b.material) return a.material < b.material;
            return a.depth < b.depth;
        });
    }
    else if (sort == RenderSortMode::BackToFront)
    {
        for (auto &i : sorted)
            i.depth = glm::distance(glm::vec3(i.model[3]), camPos);
        std::sort(sorted.begin(), sorted.end(), [](const RenderItem &a, const RenderItem &b)
        {
            return a.depth > b.depth;
        });
    }

    // Draw with state dedup
    Shader         *lastShader   = nullptr;
    const Material *lastMaterial = nullptr;

    const glm::mat4 &view      = ctx.camera->view;
    const glm::mat4 &proj      = ctx.camera->projection;
    const glm::mat4 &viewProj  = ctx.camera->viewProjection;
    const glm::vec4  camPosV   = glm::vec4(ctx.camera->position, 1.f);

    for (const auto &item : sorted)
    {
        if (!item.material || !item.drawable) continue;

        Shader *sh = item.material->getShader();
        if (!sh) continue;

        if (sh != lastShader)
        {
            rs.useProgram(sh->getId());
            sh->setMat4("u_view",      view);
            sh->setMat4("u_proj",      proj);
            sh->setMat4("u_viewProj",  viewProj);
            sh->setVec4("u_cameraPos", camPosV);
            sh->setVec4("u_clipPlane", ctx.clipPlane);
            // Shadow — only set if shadow is active; harmless on shaders without these uniforms
            if (ctx.shadowTex)
            {
                sh->setMat4 ("u_lightSpace",  ctx.lightSpaceMatrix);
                sh->setInt  ("u_shadowMap",   1);
                sh->setVec3 ("u_lightDir",    ctx.lightDir);
                sh->setVec3 ("u_lightColor",  ctx.lightColor);
                sh->setFloat("u_shadowBias",  ctx.shadowBias);
                rs.bindTexture(1, GL_TEXTURE_2D, ctx.shadowTex);
            }
            lastShader   = sh;
            lastMaterial = nullptr;
            if (ctx.stats) ctx.stats->shaderChanges++;
        }

        if (item.material != lastMaterial)
        {
            item.material->applyStates();
            int tx = item.material->bindTexturesTo(sh);
            item.material->applyUniformsTo(sh);
            lastMaterial = item.material;
            if (ctx.stats)
            {
                ctx.stats->materialChanges++;
                ctx.stats->textureBinds += (uint32_t)tx;
            }
        }

        item.drawable->applyBoneMatrices(sh);
        sh->setMat4("u_model", item.model);

        const uint32_t idxCount = item.indexCount > 0
            ? item.indexCount
            : (uint32_t)item.drawable->indexCount();

        if (ctx.stats)
        {
            ctx.stats->drawCalls++;
            ctx.stats->triangles += idxCount / 3;
            ctx.stats->vertices  += (uint32_t)item.drawable->vertexCount();
        }

        if (item.indexCount > 0) item.drawable->drawRange(item.indexStart, item.indexCount);
        else                     item.drawable->draw();
    }
}

// ─── Shadow depth pass ────────────────────────────────────────────────────────────
void Scene::drawShadowPass()
{
    // (Re)create depth FBO only when size changes or not yet allocated
    if (!shadowMap_.valid() || shadowMap_.size() != shadow.mapSize)
        shadowMap_.create(shadow.mapSize);

    if (!shadowMap_.valid()) return;

    // Build light view+proj
    const glm::vec3 dir = glm::normalize(shadow.lightDir);
    const glm::vec3 up  = (std::abs(dir.y) > 0.99f)
                        ? glm::vec3(0.f, 0.f, -1.f)
                        : glm::vec3(0.f, 1.f,  0.f);
    const glm::mat4 lightView = glm::lookAt(-dir * shadow.lightDist,
                                            glm::vec3(0.f), up);
    const glm::mat4 lightProj = glm::ortho(
        -shadow.orthoSize,  shadow.orthoSize,
        -shadow.orthoSize,  shadow.orthoSize,
         0.1f, shadow.lightDist * 2.f);
    lightSpaceMatrix_ = lightProj * lightView;

    shadowMap_.bind();

    auto &rs = RenderState::instance();
    rs.setViewport(0, 0, shadow.mapSize, shadow.mapSize);
    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setCull(true);
    rs.setCullFace(GL_FRONT); // reverse-cull reduces shadow acne
    glClear(GL_DEPTH_BUFFER_BIT);

    rs.useProgram(shadow.depthShader->getId());
    shadow.depthShader->setMat4("u_lightSpace", lightSpaceMatrix_);

    for (const auto &item : queue_.shadow)
    {
        if (!item.drawable) continue;
        shadow.depthShader->setMat4("u_model", item.model);
        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }

    rs.setCullFace(GL_BACK);
    shadowMap_.unbind();
}

// ─── drawSky ────────────────────────────────────────────────────────────
void Scene::drawSky(const FrameContext &ctx)
{
    if (!skyShader || !ctx.camera) return;

    auto &rs = RenderState::instance();
    rs.setDepthTest(true);
    rs.setDepthWrite(false);
    rs.setCull(false);
    rs.setBlend(false);

    rs.useProgram(skyShader->getId());
    skyShader->setMat4("u_view",       ctx.camera->view);
    skyShader->setMat4("u_proj",       ctx.camera->projection);
    skyShader->setMat4("u_viewProj",   ctx.camera->viewProjection);
    skyShader->setMat4("u_invViewProj",glm::inverse(ctx.camera->viewProjection));
    skyShader->setVec4("u_cameraPos",  glm::vec4(ctx.camera->position, 1.f));

    // Fullscreen triangle — no VAO geometry needed
    static GLuint dummyVAO = 0;
    if (!dummyVAO) glGenVertexArrays(1, &dummyVAO);
    glBindVertexArray(dummyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    rs.setDepthWrite(true);
    rs.setCull(true);
}

// ─── preRender ───────────────────────────────────────────────────────────────
void Scene::preRenderNode(Node *node, Camera *cam)
{
    if (!node || !node->visible) return;
    node->preRender(this, cam);
    for (auto *c : node->getChildren()) preRenderNode(c, cam);
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
