#include "Node.hpp"
#include "Animator.hpp"
#include "Manager.hpp"
#include <algorithm>


unsigned long globalNodeID = 1; // start at 1 so 0 can be "invalid ID"

Node::Node()
{
     ID = globalNodeID++;
}

Node::~Node()
{
    for (Node *c : children)
    {
        c->parent = nullptr;
        delete c;
    }
    children.clear();
}

void Node::addChild(Node *child)
{
    if (!child || child->parent == this)
        return;
    if (child->parent)
        child->parent->removeChild(child);
    child->parent = this;
    children.push_back(child);
}

void Node::removeChild(Node *child)
{
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
    {
        (*it)->parent = nullptr;
        children.erase(it);
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
    material_     = MaterialManager::instance().get(name); // resolved once
}

MeshNode::MeshNode():Node3D(), mesh(nullptr), castShadow(true), receiveShadow(true), passMask(RenderPassMask::Opaque)
{
    type = NodeType::MeshNode;
}


Light::Light():Node3D(), lightType(LightType::Point), color(1.f, 1.f, 1.f), intensity(1.0f), castShadow(false)
{
    type = NodeType::Light;
}

// ─── AnimatedMeshNode ────────────────────────────────────────────────────────

AnimatedMeshNode::AnimatedMeshNode() : Node3D()
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
    material_     = MaterialManager::instance().get(name);
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
