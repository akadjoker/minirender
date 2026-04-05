#include "Node.hpp"
#include "Animator.hpp"
#include "Scene.hpp"
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

RenderableNode::RenderableNode() : Node3D()
{
}

MeshNode::MeshNode():RenderableNode(), mesh(nullptr)
{
    type = NodeType::MeshNode;
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

void VertexAnimatedMeshNode::setFrame(float value)
{
    frame = value; 
    if (mesh) mesh->setFrame(frame); 
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

TagSocket *VertexAnimatedMeshNode::addSocket(const std::string &boneName, Node3D *node, const glm::mat4 &localOffset)
{
   return nullptr;
}

void VertexAnimatedMeshNode::setMesh(VertexAnimatedMesh *mesh)
{
    this->mesh = mesh;
    if (!mesh)
        return;

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
    if (!mesh || children.empty())
        return;

    for (int i = 0; i < mesh->tagsPerFrame; ++i)
    {
        glm::mat4 tagLocal;
        if (!mesh->sampleTag(i, frame, tagLocal))
            continue;

        static_cast<Node3D *>(children[i])->setLocal(tagLocal);
    //    static_cast<Node3D *>(children[i])->update(dt);
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
