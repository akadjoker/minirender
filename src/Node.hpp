#pragma once
#include "Mesh.hpp"
#include "Material.hpp"
#include "Types.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cstdint>

enum class NodeType
{
    Node,
    Node3D,
    MeshNode,
    Light,
    Camera,
    ParticleSystem,

};

enum class TransformSpace
{
    Local,
    Parent,
    World
};

class Camera;
struct RenderList;

class Node
{

    protected:

    NodeType type = NodeType::Node;

    public:

    std::string name;
    bool visible = true;
    Node *parent = nullptr;

    Node() = default;
    virtual ~Node();

    // Fast type access — no dynamic_cast needed
    virtual class Node3D   *asNode3D()   { return nullptr; }
    virtual class MeshNode *asMeshNode() { return nullptr; }
    virtual class Light    *asLight()    { return nullptr; }

    void addChild(Node *child);
    void removeChild(Node *child);
    Node *getChild(const std::string &name) const;
    const std::vector<Node *> &getChildren() const { return children; }
    int childCount() const { return (int)children.size(); }

protected:
    std::vector<Node *> children;
};

class Node3D : public Node
{
public:
    Node3D();
    virtual ~Node3D() = default;

     Node3D *asNode3D() override { return this; }

    // ── Transform (raw access — use setters for dirty propagation) ──
    glm::vec3 position = {0.f, 0.f, 0.f};
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 scale    = {1.f, 1.f, 1.f};

    // ── Matrices ────────────────────────────────────────────
    glm::mat4 localMatrix() const;
    glm::mat4 worldMatrix() const;

    // ── World space queries ──────────────────────────────────
    glm::vec3 worldPosition() const;
    glm::quat worldRotation()  const;
    glm::vec3 worldScale()     const;

    // Local axes in world space
    glm::vec3 forward() const;  // -Z
    glm::vec3 back()    const;  // +Z
    glm::vec3 right()   const;  // +X
    glm::vec3 left()    const;  // -X
    glm::vec3 up()      const;  // +Y
    glm::vec3 down()    const;  // -Y

    // ── Setters (mark dirty) ─────────────────────────────────
    void setPosition(const glm::vec3 &p);
    void setPosition(float x, float y, float z);
    void setRotation(const glm::quat &q);
    void setScale(const glm::vec3 &s);
    void setScale(float s);

    // ── Translation ──────────────────────────────────────────
    void translate(const glm::vec3 &delta, TransformSpace space = TransformSpace::Local);

    // Move along a world-space direction (ignores node orientation)
    void move(const glm::vec3 &worldDelta);

    // ── Rotation ────────────────────────────────────────────
    void rotate(const glm::quat &rot, TransformSpace space = TransformSpace::Local);
    void rotate(float angle, const glm::vec3 &axis, TransformSpace space = TransformSpace::Local);

    // Rotate around world Y (yaw), local X (pitch), local Z (roll) — degrees
    void yaw(float degrees);
    void pitch(float degrees);
    void roll(float degrees);

    // ── Look ────────────────────────────────────────────────
    // Point node toward target (world pos)
    void lookAt(const glm::vec3 &target, const glm::vec3 &up = {0, 1, 0});

    // Point toward a direction vector (world space)
    void lookDirection(const glm::vec3 &dir, const glm::vec3 &up = {0, 1, 0});

    // Smoothly rotate toward target — t in [0,1] per frame (use t = speed * dt)
    void lookAtSmooth(const glm::vec3 &target, float t, const glm::vec3 &up = {0, 1, 0});

    // ── Utility ─────────────────────────────────────────────
    // Distance to another node (world space)
    float distanceTo(const Node3D *other) const;
    float distanceTo(const glm::vec3 &worldPos) const;

    // Is target within range?
    bool inRange(const Node3D *other, float range) const;

    // Direction to target (world space, normalised)
    glm::vec3 directionTo(const Node3D *other) const;
    glm::vec3 directionTo(const glm::vec3 &worldPos) const;

    // Reset transform to identity
    void resetTransform();

    // ── Dirty flag ───────────────────────────────────────────
    void markDirty();
    bool isDirty() const { return dirty_; }

private:
    mutable glm::mat4 worldCache_ = glm::mat4(1.f);
    mutable bool      dirty_      = true;
};

class MeshNode : public Node3D
{
public:
    Mesh *mesh    = nullptr;

    MeshNode *asMeshNode() override { return this; }

    // Set by name → resolves + caches the pointer once.
    // Leave empty to use mesh->materials[] (loader materials).
    void        setMaterial(const std::string &name);
    Material   *getMaterial() const { return material_; }
    const std::string &getMaterialName() const { return materialName_; }

    bool castShadow    = true;
    bool receiveShadow = true;
    uint32_t passMask  = RenderPassMask::Opaque;

    MeshNode() ;
    virtual ~MeshNode() = default;

private:
    std::string  materialName_;
    Material    *material_ = nullptr;  // cached, non-owning
};

// ─── Lights ──────────────────────────────────────────────────────────────────

enum class LightType { Point, Directional, Spot };

class Light : public Node3D
{
public:
    LightType lightType  = LightType::Point;
    glm::vec3 color      = {1.f, 1.f, 1.f};
    float     intensity  = 1.0f;
    bool      castShadow = false;

    Light();

    Light *asLight() override { return this; }
    virtual ~Light() = default;
};

class PointLight : public Light
{
public:
    float range = 500.0f;
    PointLight() { lightType = LightType::Point; }
};

class DirectionalLight : public Light
{
public:
    // Direction is Node3D::forward() in world space (-Z local)
    DirectionalLight() { lightType = LightType::Directional; }
};

class SpotLight : public Light
{
public:
    float range      = 500.0f;
    float innerAngle = 20.0f; // degrees
    float outerAngle = 35.0f;
    SpotLight() { lightType = LightType::Spot; }
};
