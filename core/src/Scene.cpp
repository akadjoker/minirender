#include "Scene.hpp"
#include "Animator.hpp"
#include "Batch.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "TerrainNode.hpp"
#include "WaterNode.hpp"
#include <algorithm>
#include <limits>

Scene::Scene()
{
}

static Material *resolveMeshMaterial(MeshNode *node, Mesh *mesh, const Surface &surface)
{
    if (!node || !mesh) return nullptr;

    Material *mat = node->getMaterial();
    if (mat) return mat;

    const auto &mats = mesh->materials;
    if (surface.material_index >= 0 && surface.material_index < static_cast<int>(mats.size()))
        return mats[surface.material_index];
    return nullptr;
}

static Material *resolveAnimatedMaterial(AnimatedMeshNode *node, AnimatedMesh *mesh, const Surface &surface)
{
    if (!node || !mesh) return nullptr;

    Material *mat = node->getMaterial();
    if (mat) return mat;

    const auto &mats = mesh->materials;
    if (surface.material_index >= 0 && surface.material_index < static_cast<int>(mats.size()))
        return mats[surface.material_index];
    return nullptr;
}

static void addRenderObject(RenderScene &outScene, const RenderObject &object)
{
    if (!object.material) return;

    if (object.material->getType() == MaterialType::Water)
        outScene.water.push_back(object);
    else if (object.material->isTransparent())
        outScene.transparent.push_back(object);
    else
        outScene.opaque.push_back(object);
}

static void gatherRenderNode(Node *node, const Frustum &frustum, RenderScene &outScene, const Node *ignoredNode)
{
    if (!node || !node->visible || node == ignoredNode)
        return;

    if (auto *light = node->asLight())
        outScene.lights.push_back(light);

    if (auto *meshNode = node->asMeshNode())
    {
        if (meshNode->mesh)
        {
            const glm::mat4 world = meshNode->worldMatrix();
            const BoundingBox worldBounds = meshNode->mesh->aabb.transformed(world);

            if (!worldBounds.is_valid() || frustum.contains(worldBounds))
            {
                for (const auto &surface : meshNode->mesh->surfaces)
                {
                    BoundingBox surfaceBounds = surface.aabb.is_valid()
                        ? surface.aabb.transformed(world)
                        : worldBounds;
                    if (surfaceBounds.is_valid() && !frustum.contains(surfaceBounds))
                        continue;

                    Material *mat = resolveMeshMaterial(meshNode, meshNode->mesh, surface);
                    if (!mat) continue;

                    RenderObject object;
                    object.owner = meshNode;
                    object.drawable = meshNode->mesh;
                    object.material = mat;
                    object.model = world;
                    object.worldBounds = surfaceBounds;
                    object.indexStart = surface.index_start;
                    object.indexCount = surface.index_count;
                    object.castShadow = meshNode->castShadow;
                    object.receiveShadow = meshNode->receiveShadow;
                    addRenderObject(outScene, object);
                }
            }
        }
    }

    if (auto *animatedNode = node->asAnimatedMeshNode())
    {
        if (animatedNode->mesh)
        {
            const glm::mat4 world = animatedNode->worldMatrix();
            const BoundingBox worldBounds = animatedNode->mesh->aabb.transformed(world);

            if (!worldBounds.is_valid() || frustum.contains(worldBounds))
            {
                for (const auto &surface : animatedNode->mesh->surfaces)
                {
                    BoundingBox surfaceBounds = surface.aabb.is_valid()
                        ? surface.aabb.transformed(world)
                        : worldBounds;
                    if (surfaceBounds.is_valid() && !frustum.contains(surfaceBounds))
                        continue;

                    Material *mat = resolveAnimatedMaterial(animatedNode, animatedNode->mesh, surface);
                    if (!mat) continue;

                    RenderObject object;
                    object.owner = animatedNode;
                    object.drawable = animatedNode->mesh;
                    object.material = mat;
                    object.model = world;
                    object.worldBounds = surfaceBounds;
                    object.indexStart = surface.index_start;
                    object.indexCount = surface.index_count;
                    object.castShadow = animatedNode->castShadow;
                    object.receiveShadow = animatedNode->receiveShadow;
                    addRenderObject(outScene, object);
                }
            }
        }
    }

    if (auto *terrain = dynamic_cast<TerrainLodNode *>(node))
    {
        if (terrain->prepareForRender(outScene.camera, frustum))
        {
            RenderObject object;
            object.owner = terrain;
            object.drawable = terrain->getRenderBuffer();
            object.material = terrain->getMaterial();
            object.model = glm::mat4(1.0f);
            object.worldBounds = terrain->getAABB();
            object.indexStart = 0;
            object.indexCount = terrain->getVisibleIndexCount();
            object.castShadow = terrain->castShadow;
            object.receiveShadow = terrain->receiveShadow;
            addRenderObject(outScene, object);
        }
    }

    if (auto *water = dynamic_cast<WaterNode3D *>(node))
    {
        if (water->getMaterial())
        {
            RenderObject object;
            object.owner = water;
            object.drawable = water->getRenderBuffer();
            object.material = water->getMaterial();
            object.model = water->worldMatrix();
            object.worldBounds = water->getAABB();
            object.castShadow = water->castShadow;
            object.receiveShadow = water->receiveShadow;
            addRenderObject(outScene, object);
        }
    }

    for (auto *child : node->getChildren())
        gatherRenderNode(child, frustum, outScene, ignoredNode);
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
    auto *node = new AnimatedMeshNode();
    node->name = name;
    node->mesh = mesh;
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

TerrainLodNode *Scene::createTerrainLodNode(const std::string &name)
{
    auto *node = new TerrainLodNode(name);
    add(node);
    return node;
}

WaterNode3D *Scene::createWaterNode(const std::string &name)
{
    auto *node = new WaterNode3D(name);
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
        *it = roots_.back();
        roots_.pop_back();
    }
}

void Scene::markForRemoval(Node *node)
{
    if (!node) return;
    if (std::find(pendingRemoval_.begin(), pendingRemoval_.end(), node) == pendingRemoval_.end())
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

void Scene::update(float dt)
{
    for (auto *node : pendingRemoval_)
    {
        remove(node);
        if (node->parent)
            node->parent->removeChild(node);
        delete node;
    }
    pendingRemoval_.clear();

    for (auto *root : roots_)
        updateNode(root, dt);
}

void Scene::buildRenderScene(Camera *camera, RenderScene &outScene, const Node *ignoredNode)
{
    outScene.clear();
    if (!camera)
        return;

    camera->updateMatrices();

    outScene.camera = camera;
    outScene.viewport = camera->viewport;
    outScene.clearColor = camera->clearColor;
    outScene.clearColorValue = camera->clearColorVal;
    outScene.clearDepth = camera->clearDepth;
    outScene.sky = sky_;

    const Frustum frustum = Frustum::from_matrix(camera->viewProjection);
    for (auto *root : roots_)
        gatherRenderNode(root, frustum, outScene, ignoredNode);
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

    if (auto *vn = node->asVertexAnimMeshNode())
    {
        vn->controller.update(dt);
        vn->updateTagSockets();
    }

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
            const BoundingBox worldAABB = amn->mesh->aabb.transformed(world);
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
