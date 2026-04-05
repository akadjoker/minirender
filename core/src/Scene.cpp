#include "Scene.hpp"
#include "Animator.hpp"
#include "Batch.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "RenderState.hpp"
#include "TerrainNode.hpp"
#include <algorithm>

namespace
{
void erase_node_ptr(std::vector<Node *> &nodes, Node *target)
{
    auto it = std::remove(nodes.begin(), nodes.end(), target);
    nodes.erase(it, nodes.end());
}
}

Scene::Scene()
{
    resetPassState();
}

Scene::~Scene()
{
    for (auto *cam : cameras_)
        delete cam;
    cameras_.clear();
    clear();
}

const std::vector<RenderableNode *> &Scene::renderables(RenderType type) const
{
    switch (type)
    {
    case RenderType::Solid:       return solidNodes_;
    case RenderType::Transparent: return transparentNodes_;
    case RenderType::Lightmap:    return lightmapNodes_;
    case RenderType::Skinning:    return skinningNodes_;
    case RenderType::Terrain:     return terrainNodes_;
    case RenderType::Skybox:      return skyboxNodes_;
    case RenderType::Special:     return specialNodes_;
    case RenderType::Overlay:     return overlayNodes_;
    }
    return renderables_;
}

void Scene::resetPassState()
{
    passActive_ = false;
    currentShader_ = nullptr;
}

void Scene::attachRecursive(Node *node)
{
    if (!node)
        return;
    node->scene_ = this;
    for (Node *child : node->getChildren())
        attachRecursive(child);
}

void Scene::detachRecursive(Node *node)
{
    if (!node)
        return;
    node->scene_ = nullptr;
    for (Node *child : node->getChildren())
        detachRecursive(child);
}

void Scene::onChildAttached(Node *node)
{
    if (!node)
        return;
    erase_node_ptr(roots_, node);
    attachRecursive(node);
}

void Scene::onChildDetached(Node *node)
{
    if (!node)
        return;
    attachRecursive(node);
    if (std::find(roots_.begin(), roots_.end(), node) == roots_.end())
        roots_.push_back(node);
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
    auto *node = new AnimatedMeshNode();
    node->name = name;
    node->mesh = mesh;
    node->animator = new Animator(mesh);
    if (mesh && !mesh->animations.empty())
    {
        AnimationLayer *layer = node->animator->addLayer();
        std::string startAnim = mesh->animations.front() ? mesh->animations.front()->name : std::string();
        for (Animation *animation : mesh->animations)
        {
            if (!animation)
                continue;
            layer->addAnimation(animation->name, animation, false);
            if (animation->name == "idle")
                startAnim = animation->name;
        }
        if (!startAnim.empty())
            layer->play(startAnim);
    }
    add(node);
    return node;
}

VertexAnimatedMeshNode *Scene::createVertexAnimatedMeshNode(const std::string &name, VertexAnimatedMesh *mesh)
{
    auto *node = new VertexAnimatedMeshNode();
    node->name = name;
    node->setMesh(mesh);
    add(node);
    return node;
}

 
TerrainLodNode *Scene::createTerrainLodNode(const std::string &name)
{
    auto *node = new TerrainLodNode(name);
    add(node);
    return node;
}

void Scene::add(Node *node)
{
    if (!node)
        return;

    if (node->scene_ && node->scene_ != this)
        node->scene_->remove(node);

    if (node->parent)
        node->parent->removeChild(node);

    attachRecursive(node);
    if (std::find(roots_.begin(), roots_.end(), node) == roots_.end())
        roots_.push_back(node);
}

void Scene::remove(Node *node)
{
    if (!node)
        return;

    if (node->parent)
        node->parent->removeChild(node);

    erase_node_ptr(roots_, node);
    erase_node_ptr(pendingRemoval_, node);
    detachRecursive(node);
}

void Scene::markForRemoval(Node *node)
{
    if (!node) return;
    if (std::find(pendingRemoval_.begin(), pendingRemoval_.end(), node) == pendingRemoval_.end())
        pendingRemoval_.push_back(node);
}

void Scene::clear()
{
    std::vector<Node *> roots = roots_;
    roots_.clear();
    pendingRemoval_.clear();
    for (Node *root : roots)
        delete root;

    renderables_.clear();
    solidNodes_.clear();
    transparentNodes_.clear();
    lightmapNodes_.clear();
    skinningNodes_.clear();
    terrainNodes_.clear();
    skyboxNodes_.clear();
    specialNodes_.clear();
    overlayNodes_.clear();
    skinningNodes_.clear();
    resetPassState();
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

Camera *Scene::createFreeCamera(const std::string &name,
                                int viewportWidth, int viewportHeight,
                                const glm::vec3 &position,
                                const glm::vec3 &target,
                                float moveSpeed,
                                float mouseSensitivity,
                                float sprintMultiplier)
{
    Camera *cam = createCamera(name);
    cam->setViewport(0, 0, viewportWidth, viewportHeight);
    cam->setPosition(position);
    cam->lookAt(target);

    auto *controller = new FreeCameraController();
    controller->moveSpeed = moveSpeed;
    controller->mouseSensitivity = mouseSensitivity;
    controller->sprintMultiplier = sprintMultiplier;
    cam->setController(controller);
    return cam;
}

void Scene::setCurrentCamera(Camera *cam)
{
    currentCamera_ = cam;
}

void Scene::setCamera(Camera *cam)
{
    setCurrentCamera(cam);
}

void Scene::beginPass()
{
    passActive_ = true;
 
    auto &rs = RenderState::instance();
    Material::applyDefaultStates();

    if (currentCamera_)
    {
        rs.setViewport(currentCamera_->viewport.x,
                           currentCamera_->viewport.y,
                           currentCamera_->viewport.z,
                           currentCamera_->viewport.w);
        rs.setClearColor(currentCamera_->clearColorVal.r,
                         currentCamera_->clearColorVal.g,
                         currentCamera_->clearColorVal.b,
                         currentCamera_->clearColorVal.a);
        rs.clear(currentCamera_->clearColor, currentCamera_->clearDepth);
    }
}

void Scene::endPass()
{
   
    passActive_ = false;
}

void Scene::setShader(Shader *shader)
{
    currentShader_ = shader;
    RenderState::instance().useProgram(shader ? shader->getId() : 0);
}

void Scene::render(RenderType type)
{
    if (!currentCamera_ || !currentShader_)
        return;

    auto &rs = RenderState::instance();

    rs.useProgram(currentShader_->getId());
    currentShader_->setMat4("u_view", currentCamera_->view);
    currentShader_->setMat4("u_projection", currentCamera_->projection);

    for (RenderableNode *renderable : renderables(type))
        renderable->render(currentShader_, currentCamera_);
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

void Scene::update(float dt)
{
    for (auto *node : pendingRemoval_)
    {
        remove(node);
        delete node;
    }
    pendingRemoval_.clear();

    for (auto *camera : cameras_)
    {
        if (!camera)
            continue;
        camera->update(dt);
        camera->updateMatrices();
    }

    for (auto *root : roots_)
        updateNode(root, dt);

    renderables_.clear();
    solidNodes_.clear();
    transparentNodes_.clear();
    lightmapNodes_.clear();
    skinningNodes_.clear();
    terrainNodes_.clear();
    skyboxNodes_.clear();
    specialNodes_.clear();
    overlayNodes_.clear();
    skinningNodes_.clear();

    for (auto *root : roots_)
        collectRenderables(root);
}

void Scene::collectRenderables(Node *node)
{
    if (!node || !node->visible)
        return;

    if (auto *renderable = node->asRenderableNode())
    {
        renderables_.push_back(renderable);
        switch (renderable->renderType)
        {
        case RenderType::Solid:       solidNodes_.push_back(renderable); break;
        case RenderType::Transparent: transparentNodes_.push_back(renderable); break;
        case RenderType::Lightmap:    lightmapNodes_.push_back(renderable); break;
        case RenderType::Skinning:    skinningNodes_.push_back(renderable); break;
        case RenderType::Terrain:     terrainNodes_.push_back(renderable); break;
        case RenderType::Skybox:      skyboxNodes_.push_back(renderable); break;
        case RenderType::Special:     specialNodes_.push_back(renderable); break;
        case RenderType::Overlay:     overlayNodes_.push_back(renderable); break;
        }
    }

    for (auto *child : node->getChildren())
        collectRenderables(child);
}

void Scene::debug(RenderBatch *batch)
{
    if (!batch)
        return;

    for (auto *root : roots_)
        debugNode(root, batch);
}

void Scene::updateNode(Node *node, float dt)
{
    if (!node || !node->visible) return;

    node->update(dt);

    if (auto *node3d = node->asNode3D())
        node3d->worldMatrix();

    if (auto *an = node->asAnimatedMeshNode())
        if (an->animator && an->animator->active)
        {
            an->animator->update(dt);
            an->updateSockets();
        }

    for (auto *child : node->getChildren())
        updateNode(child, dt);
}

void Scene::release()
{
    for (auto *cam : cameras_)
        delete cam;
    cameras_.clear();
    currentCamera_ = nullptr;
    resetPassState();
    clear();
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
        if (animatedNode->mesh)
        {
            const glm::mat4 world = animatedNode->worldMatrix();
            const BoundingBox posedAABB = animatedNode->mesh->computeSkinnedAABB();
            if (posedAABB.is_valid())
            {
                batch->SetColor(0, 200, 255, 255);
                batch->Box(posedAABB.transformed(world));
            }
        }
    }
    else if (auto *vertexAnimNode = node->asVertexAnimatedMeshNode())
    {
        if (vertexAnimNode->mesh && vertexAnimNode->mesh->aabb.is_valid())
        {
            const glm::mat4 world = vertexAnimNode->worldMatrix();
            bool drewSurfaceBoxes = false;

            for (const auto &surface : vertexAnimNode->mesh->surfaces)
            {
                if (!surface.aabb.is_valid())
                    continue;

                batch->SetColor(255, 180, 64, 255);
                batch->Box(surface.aabb.transformed(world));
                drewSurfaceBoxes = true;
            }

            if (!drewSurfaceBoxes)
            {
                batch->SetColor(255, 140, 0, 255);
                batch->Box(vertexAnimNode->mesh->aabb.transformed(world));
            }
        }
    }

    for (auto *child : node->getChildren())
        debugNode(child, batch);
}

static void pickNode(Node *node, const Ray &ray, ScenePickResult &best)
{
    if (!node || !node->visible) return;

    if (auto *mn = node->asMeshNode())
    {
        if (mn->mesh)
        {
            const glm::mat4 world = mn->worldMatrix();
            const BoundingBox worldAABB = mn->mesh->aabb.transformed(world);
            if (worldAABB.is_valid())
            {
                float aabbHit = worldAABB.intersects_ray(ray.origin, ray.direction);
                if (aabbHit < 0.f || aabbHit >= best.result.distance)
                    goto pickChildren;
            }

            PickResult r = mn->mesh->pick(ray, world);
            if (r.hit && r.distance < best.result.distance)
            {
                best.result = r;
                best.node = mn;
            }
        }
    }
    else if (auto *amn = node->asAnimatedMeshNode())
    {
        if (amn->mesh)
        {
            const glm::mat4 world = amn->worldMatrix();
            const BoundingBox worldAABB = amn->mesh->computeSkinnedAABB().transformed(world);
            if (worldAABB.is_valid())
            {
                float aabbHit = worldAABB.intersects_ray(ray.origin, ray.direction);
                if (aabbHit < 0.f || aabbHit >= best.result.distance)
                    goto pickChildren;
            }

            PickResult r = amn->mesh->pick(ray, world);
            if (r.hit && r.distance < best.result.distance)
            {
                best.result = r;
                best.node = amn;
            }
        }
    }
    else if (auto *van = node->asVertexAnimatedMeshNode())
    {
        if (van->mesh)
        {
            const glm::mat4 world = van->worldMatrix();
            const BoundingBox worldAABB = van->mesh->aabb.transformed(world);
            if (worldAABB.is_valid())
            {
                float aabbHit = worldAABB.intersects_ray(ray.origin, ray.direction);
                if (aabbHit < 0.f || aabbHit >= best.result.distance)
                    goto pickChildren;
            }

            PickResult r = van->mesh->pick(ray, world);
            if (r.hit && r.distance < best.result.distance)
            {
                best.result = r;
                best.node = van;
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
