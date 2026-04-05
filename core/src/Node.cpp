#include "Node.hpp"
#include "Animator.hpp"
#include "Camera.hpp"
#include "Scene.hpp"
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>


unsigned long globalNodeID = 1; // start at 1 so 0 can be "invalid ID"

Node::Node()
{
     ID = globalNodeID++;
}

Node::~Node()
{
    for (Node *c : children)
        delete c;
    children.clear();
}

void Node::addChild(Node *child)
{
    if (!child || child == this || child->parent == this)
        return;

    if (child->parent)
        child->parent->removeChild(child);

    if (child->scene_ && child->scene_ != scene_)
        child->scene_->remove(child);

    child->parent = this;
    children.push_back(child);

    if (auto *child3D = child->asNode3D())
        child3D->transformParent_ = asNode3D();

    if (scene_)
        scene_->onChildAttached(child);
}

void Node::removeChild(Node *child)
{
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
    {
        Node *detached = *it;
        detached->parent = nullptr;
        children.erase(it);

        if (auto *child3D = detached->asNode3D())
        {
            if (child3D->transformParent_ == asNode3D())
                child3D->transformParent_ = nullptr;
            child3D->markDirty();
        }

        if (scene_)
            scene_->onChildDetached(detached);
    }
}

Node *Node::getChild(const std::string &name) const
{
    for (Node *c : children)
        if (c->name == name)
            return c;
    return nullptr;
}

void MeshNode::setMaterial(const std::string &name)
{
    materialName_ = name;
    materialOverrides_.clear();
}

void MeshNode::setMaterial(Material *material)
{
    setMaterial(0, material);
    materialName_.clear();
}

void MeshNode::setMaterial(int slot, Material *material)
{
    if (slot < 0)
        return;
    if (slot >= (int)materialOverrides_.size())
        materialOverrides_.resize(slot + 1, nullptr);
    materialOverrides_[slot] = material;
}

Material *MeshNode::getMaterial(int slot) const
{
    if (slot < 0)
        return nullptr;
    if (slot < (int)materialOverrides_.size() && materialOverrides_[slot])
        return materialOverrides_[slot];
    if (mesh && slot < (int)mesh->materials.size())
        return mesh->materials[slot];
    return nullptr;
}

namespace
{
void applyNodeMaterial(Material *material, Shader *shader)
{
    if (material)
    {
        material->applyStates();
        material->applyUniformsTo(shader);
        material->bindTexturesTo(shader);
    }
    else
    {
        Material::applyDefaultStates();
    }
}
}

RenderableNode::RenderableNode() : Node3D()
{
}

void RenderableNode::render(Shader *shader, Camera *camera)
{
    (void)shader;
    (void)camera;
}

MeshNode::MeshNode():RenderableNode(), mesh(nullptr)
{
    type = NodeType::MeshNode;
}

void MeshNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !mesh)
        return;

    const glm::mat4 model = worldMatrix();
    const BoundingBox worldBounds = mesh->aabb.transformed(model);
    if (worldBounds.is_valid() && !camera->frustum.contains(worldBounds))
        return;

    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));

    for (const Surface &surface : mesh->surfaces)
    {
        applyNodeMaterial(getMaterial(surface.material_index), shader);
        mesh->drawRange(surface.index_start, surface.index_count);
    }
}


Light::Light():Node3D(), lightType(LightType::Point), color(1.f, 1.f, 1.f), intensity(1.0f), castShadow(false)
{
    type = NodeType::Light;
}

// ─── AnimatedMeshNode ────────────────────────────────────────────────────────

AnimatedMeshNode::AnimatedMeshNode() : RenderableNode()
{
    type = NodeType::MeshNode; // reuse MeshNode type
}

AnimatedMeshNode::~AnimatedMeshNode()
{
    delete animator;
}

void AnimatedMeshNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !mesh)
        return;

    const glm::mat4 model = worldMatrix();
    const BoundingBox worldBounds = mesh->computeSkinnedAABB().transformed(model);
    if (worldBounds.is_valid() && !camera->frustum.contains(worldBounds))
        return;

    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));
    mesh->applyBoneMatrices(shader);

    for (const Surface &surface : mesh->surfaces)
    {
        applyNodeMaterial(getMaterial(surface.material_index), shader);
        mesh->drawRange(surface.index_start, surface.index_count);
    }
}

 

void AnimatedMeshNode::setMaterial(const std::string &name)
{
    materialName_ = name;
    materialOverrides_.clear();
}

void AnimatedMeshNode::setMaterial(Material *material)
{
    setMaterial(0, material);
    materialName_.clear();
}

void AnimatedMeshNode::setMaterial(int slot, Material *material)
{
    if (slot < 0)
        return;
    if (slot >= (int)materialOverrides_.size())
        materialOverrides_.resize(slot + 1, nullptr);
    materialOverrides_[slot] = material;
}

Material *AnimatedMeshNode::getMaterial(int slot) const
{
    if (slot < 0)
        return nullptr;
    if (slot < (int)materialOverrides_.size() && materialOverrides_[slot])
        return materialOverrides_[slot];
    if (mesh && slot < (int)mesh->materials.size())
        return mesh->materials[slot];
    return nullptr;
}

VertexAnimatedMeshNode::VertexAnimatedMeshNode() : RenderableNode()
{
    type = NodeType::MeshNode;
}

VertexAnimatedMeshNode::~VertexAnimatedMeshNode()
{
   
}

void VertexAnimatedMeshNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !mesh)
        return;

    const glm::mat4 model = worldMatrix();
    const BoundingBox worldBounds = mesh->aabb.transformed(model);
    if (worldBounds.is_valid() && !camera->frustum.contains(worldBounds))
        return;

    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));

    for (const Surface &surface : mesh->surfaces)
    {
        applyNodeMaterial(getMaterial(surface.material_index), shader);
        mesh->drawRange(surface.index_start, surface.index_count);
    }
}

void VertexAnimatedMeshNode::setFrame(float value)
{
    frameAnimator.setFrame(value);
}

void VertexAnimatedMeshNode::setMaterial(const std::string &name)
{
    materialName_ = name;
    materialOverrides_.clear();
}

void VertexAnimatedMeshNode::setMaterial(Material *material)
{
    setMaterial(0, material);
    materialName_.clear();
}

void VertexAnimatedMeshNode::setMaterial(int slot, Material *material)
{
    if (slot < 0)
        return;
    if (slot >= (int)materialOverrides_.size())
        materialOverrides_.resize(slot + 1, nullptr);
    materialOverrides_[slot] = material;
}

Material *VertexAnimatedMeshNode::getMaterial(int slot) const
{
    if (slot < 0)
        return nullptr;
    if (slot < (int)materialOverrides_.size() && materialOverrides_[slot])
        return materialOverrides_[slot];
    if (mesh && slot < (int)mesh->materials.size())
        return mesh->materials[slot];
    return nullptr;
}

 

void VertexAnimatedMeshNode::setMesh(VertexAnimatedMesh *mesh)
{
    this->mesh = mesh;
    frameAnimator.setMesh(mesh);
    frameAnimator.clearAnimations();
    if (!mesh)
        return;

    if (mesh->tagsPerFrame == 0)
    {
        const int frames = mesh->frameCount();
        struct Md2AnimDef
        {
            const char *name;
            int start;
            int end;
            float fps;
            bool loop;
        };

        const Md2AnimDef md2Anims[] = {
            {"idle", 0, 38, 9.0f, true},
            {"run", 40, 45, 10.0f, true},
            {"attack", 46, 53, 10.0f, false},
            {"pain_a", 54, 57, 7.0f, false},
            {"pain_b", 58, 61, 7.0f, false},
            {"pain_c", 62, 65, 7.0f, false},
            {"jump", 66, 71, 7.0f, false},
            {"flip", 72, 83, 7.0f, false},
            {"salute", 84, 94, 7.0f, false},
            {"taunt", 95, 111, 10.0f, false},
            {"wave", 112, 122, 7.0f, false},
            {"point", 123, 134, 6.0f, false},
            {"crouch_idle", 135, 153, 10.0f, true},
            {"crouch_walk", 154, 159, 7.0f, true},
            {"crouch_attack", 160, 168, 10.0f, false},
            {"crouch_pain", 169, 172, 7.0f, false},
            {"crouch_death", 173, 177, 5.0f, false},
            {"death_fallback", 178, 183, 7.0f, false},
            {"death_fallback_forward", 184, 189, 7.0f, false},
            {"death_fallback_slow", 190, 197, 7.0f, false},
            {"boom", 198, 198, 5.0f, false},
        };

        for (const Md2AnimDef &anim : md2Anims)
        {
            if (anim.start >= frames)
                continue;
            frameAnimator.addAnimation(
                anim.name,
                anim.start,
                std::min(anim.end, frames - 1),
                anim.fps,
                anim.loop);
        }

        frameAnimator.play("idle");
    }

    for (size_t i = 0; i < mesh->tagsPerFrame; i++)
    {
        Node3D *back = new Node3D();
        back->name = mesh->tags[i].tag;
        addChild(back);
    }
}

Node3D *VertexAnimatedMeshNode::getTag(int index)
{
     return static_cast<Node3D *>(children[index]);
}

void VertexAnimatedMeshNode::update(float dt)
{
    if (!mesh)
        return;

    frameAnimator.update(dt);

    if (children.empty())
        return;

    for (int i = 0; i < mesh->tagsPerFrame; ++i)
    {
        glm::mat4 tagLocal;
        if (frameAnimator.blending())
        {
            const FrameAnimation *previous = frameAnimator.previousAnimation();
            const FrameAnimation *current = frameAnimator.currentAnimation();
            if (!previous || !current)
                continue;
            if (!mesh->sampleTagBlended(i,
                                        frameAnimator.previousFrame(), previous->startFrame, previous->endFrame,
                                        frameAnimator.currentFrame(), current->startFrame, current->endFrame,
                                        frameAnimator.blendAlpha(), tagLocal))
                continue;
        }
        else if (const FrameAnimation *animation = frameAnimator.currentAnimation())
        {
            if (!mesh->sampleTag(i, frameAnimator.currentFrame(),
                                 animation->startFrame, animation->endFrame, tagLocal))
                continue;
        }
        else
        {
            if (!mesh->sampleTag(i, frameAnimator.currentFrame(), tagLocal))
                continue;
        }

        static_cast<Node3D *>(children[i])->setLocal(tagLocal);
 
    }
}

// ── BoneSocket ───────────────────────────────────────────────

BoneSocket *AnimatedMeshNode::addSocket(const std::string &boneName, Node3D *node,
                                        const glm::mat4 &localOffset)
{
    // Avoid duplicates on same bone
    for (auto &s : sockets_)
        if (s.boneName == boneName) { s.node = node; s.localOffset = localOffset; return &s; }

    BoneSocket s;
    s.boneName    = boneName;
    s.boneIndex   = animator ? animator->findBoneIndex(boneName) : -1;
    s.node        = node;
    s.localOffset = localOffset;
    sockets_.push_back(s);

    // Add as child so it participates in scene traversal / rendering
    addChild(node);
    return &sockets_.back();
}

BoneSocket *AnimatedMeshNode::getSocket(const std::string &boneName)
{
    for (auto &s : sockets_)
        if (s.boneName == boneName) return &s;
    return nullptr;
}

void AnimatedMeshNode::removeSocket(const std::string &boneName)
{
    for (auto it = sockets_.begin(); it != sockets_.end(); ++it)
    {
        if (it->boneName == boneName)
        {
            removeChild(it->node);
            sockets_.erase(it);
            return;
        }
    }
}

void AnimatedMeshNode::updateSockets()
{
    if (sockets_.empty() || !animator) return;

    for (auto &s : sockets_)
    {
        if (!s.node) continue;

        // Resolve bone index lazily
        if (s.boneIndex < 0)
            s.boneIndex = animator->findBoneIndex(s.boneName);
        if (s.boneIndex < 0) continue;

        // boneMat = mesh-local transform of the bone (no inverse bind pose applied)
        // The socket node is a child of this AnimatedMeshNode, so its local transform
        // should be relative to this node's space — which is exactly boneMat * localOffset.
        glm::mat4 boneMat = animator->getBoneTransform(s.boneIndex) * s.localOffset;

        // Decompose into TRS for the Node3D
        glm::vec3 scl, pos, skew;
        glm::vec4 persp;
        glm::quat rot;
        if (glm::decompose(boneMat, scl, rot, pos, skew, persp))
        {
            s.node->position = pos;
            s.node->rotation = glm::conjugate(rot); // glm::decompose returns conjugate
            s.node->scale    = scl;
        }
    }
} 
