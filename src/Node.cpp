#include "Node.hpp"
#include "Animator.hpp"
#include "Manager.hpp"
#include <algorithm>

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
