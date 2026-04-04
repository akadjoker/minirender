#pragma once
#include "Mesh.hpp"
#include "Material.hpp"
#include "Md2Loader.hpp"
#include "Md3Loader.hpp"
#include "VertexAnimation.hpp"
#include "Types.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations for render integration
class RenderQueue;
struct FrameContext;

enum class NodeType
{
    Node,
    Node3D,
    MeshNode,
    Md2Node,
    Md3Node,
    Light,
    Camera,
    ParticleSystem,
    Decal,
    LensFlare,
    Grass,
    ManualMesh,
};

enum class TransformSpace
{
    Local,
    Parent,
    World
};

class Camera;
class Shader;
struct RenderList;

class Node
{

    protected:

    NodeType type = NodeType::Node;

    public:

    std::string name;
    bool visible = true;
    Node *parent = nullptr;
    unsigned long ID = 0; // unique ID assigned by scene (for picking, etc.)

    Node();
    virtual ~Node();

    // Fast type access — no dynamic_cast needed
    virtual class Node3D   *asNode3D()            { return nullptr; }
    virtual class MeshNode *asMeshNode()          { return nullptr; }
    virtual class AnimatedMeshNode *asAnimatedMeshNode() { return nullptr; }
    virtual class VertexAnimMeshNode *asVertexAnimMeshNode() { return nullptr; }
    virtual class Md2Node *asMd2Node()            { return nullptr; }
    virtual class Md3Node *asMd3Node()            { return nullptr; }
    virtual class Light    *asLight()             { return nullptr; }

    // Override to submit custom draw calls (terrains, particles, etc.)
    virtual void gatherRenderItems(RenderQueue& /*queue*/, const FrameContext& /*ctx*/) {}

    // Override to update simulation logic each frame (particles, etc.)
    virtual void update(float /*dt*/) {}

    // Called once per frame BEFORE the main render, allowing the node to
    // render into off-screen targets (e.g. WaterNode3D reflection/refraction).
    // scene is non-const to allow renderToTarget calls.
    virtual void preRender(class Scene * /*scene*/, const Camera * /*mainCam*/) {}

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

    // Euler angles in DEGREES: x=pitch, y=yaw, z=roll
    glm::vec3 getEulerAngles() const;
    void      setEulerAngles(const glm::vec3 &degreesPitchYawRoll);

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

    // ── RotateTowards / MoveTowards ──────────────────────────
    // Rotate toward targetRot by at most maxDegrees — returns true when reached
    bool rotateTo(const glm::quat &targetRot, float maxDegrees);

    // Move toward worldTarget by at most maxDelta units — returns true when reached
    bool moveTo(const glm::vec3 &worldTarget, float maxDelta);

    // ── Space conversion ─────────────────────────────────────
    // Transform a world-space point into this node's local space
    glm::vec3 worldToLocalPoint(const glm::vec3 &worldPoint) const;

    // Transform a local-space point into world space
    glm::vec3 localToWorldPoint(const glm::vec3 &localPoint) const;

    // ── Re-parent ────────────────────────────────────────────
    // Attach to newParent. If keepWorldTransform=true the node stays
    // visually in place (position/rotation/scale are recalculated in
    // the new parent's space).
    void setParent(Node3D *newParent, bool keepWorldTransform = true);

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
    Material    *material_ = nullptr;
};

// ─── BoneSocket ──────────────────────────────────────────────────────────────
// Liga um Node3D a um bone do AnimatedMeshNode.
// O node filho é atualizado automaticamente em Scene::update() a cada frame.
struct BoneSocket
{
    std::string  boneName;
    int          boneIndex   = -1;           // resolvido na primeira atualização
    Node3D      *node        = nullptr;      // nó filho — não owned
    glm::mat4    localOffset = glm::mat4(1.f); // offset em bone-local space
};

// ─── AnimatedMeshNode ────────────────────────────────────────────────────────
class AnimatedMeshNode : public Node3D
{
public:
    AnimatedMesh *mesh     = nullptr;
    class Animator *animator = nullptr; // owned
    uint32_t passMask      = RenderPassMask::Opaque;

    AnimatedMeshNode();
    virtual ~AnimatedMeshNode();

    AnimatedMeshNode *asAnimatedMeshNode() override { return this; }

    void       setMaterial(const std::string &name);
    Material  *getMaterial() const { return material_; }
    const std::string &getMaterialName() const { return materialName_; }

    // ── Bone Sockets (attachments) ──────────────────────────
    // Adiciona um node filho que segue um bone.
    // O node é adicionado como filho deste AnimatedMeshNode.
    // localOffset: transform adicional em bone-local space (ex: rotação/offset de uma arma)
    BoneSocket *addSocket(const std::string &boneName, Node3D *node,
                          const glm::mat4 &localOffset = glm::mat4(1.f));
    BoneSocket *getSocket(const std::string &boneName);
    void        removeSocket(const std::string &boneName);

    // Atualiza todos os sockets — chamado por Scene::updateNode() após animator->update()
    void updateSockets();

private:
    std::string  materialName_;
    Material    *material_ = nullptr;
    std::vector<BoneSocket> sockets_; // bone attachments
};

// ─── VertexAnimMeshNode ────────────────────────────────────────────────────
// Reuses MeshNode render path, but updates frame/clip state through
// VertexAnimController and drives attach nodes from MD3-like tags.
class VertexAnimMeshNode : public MeshNode
{
public:
    VertexAnimMeshNode();
    ~VertexAnimMeshNode() override;

    VertexAnimMeshNode *asVertexAnimMeshNode() override { return this; }

    // Per-instance animator state (independent per node clone/instance)
    VertexAnimController controller;

    // Shared animation asset (optional). If set, updateAnimation() will
    // update controller, deform this node's mesh and update tag sockets.
    void setAnimAsset(const VertexAnimAsset *asset, bool cloneTemplateMesh = true);
    const VertexAnimAsset *animAsset() const { return animAsset_; }
    bool hasAnimAsset() const { return animAsset_ != nullptr; }

    void setAutoApplySample(bool enabled) { autoApplySample_ = enabled; }
    bool autoApplySample() const { return autoApplySample_; }

    // Called by Scene::updateNode().
    virtual void updateAnimation(float dt);

    // Create a new node instance with independent controller state.
    // If this node has an anim asset, clone inherits it and gets its own mesh clone.
    VertexAnimMeshNode *cloneInstance(const std::string &newName = "") const;

    void setTagTracks(const std::vector<VertexTagTrack> &tracks) { tagTracks_ = tracks; }
    std::vector<VertexTagTrack> &tagTracks() { return tagTracks_; }
    const std::vector<VertexTagTrack> &tagTracks() const { return tagTracks_; }

    VertexTagSocket *addTagSocket(const std::string &tagName, Node3D *node,
                                  const glm::mat4 &localOffset = glm::mat4(1.f));
    VertexTagSocket *getTagSocket(const std::string &tagName);
    void             removeTagSocket(const std::string &tagName);
    void             clearTagSockets();

    void updateTagSockets();

private:
    Mesh                        *ownedMesh_ = nullptr; // optional per-instance mesh
    const VertexAnimAsset       *animAsset_ = nullptr; // non-owning shared asset
    bool                         autoApplySample_ = true;
    VertexTagBinder              tagBinder_;
    std::vector<VertexTagSocket> tagSockets_;
    std::vector<VertexTagTrack>  tagTracks_;
};

// ─── Md2Node ────────────────────────────────────────────────────────────────
// Dedicated MD2 instance node:
// - owns per-instance controller state
// - can clone shared template mesh via setMd2Asset()
class Md2Node : public VertexAnimMeshNode
{
public:
    Md2Node();

    Md2Node *asMd2Node() override { return this; }

    // Preferred API: loader provides mesh + runtime, node owns animation behavior.
    bool setMd2Data(Mesh *templateMesh,
                    const Md2RuntimeData &runtime,
                    bool cloneTemplateMesh = true,
                    const std::string &defaultClip = "Stand");

    bool setMd2Asset(const VertexAnimAsset *asset,
                     bool cloneTemplateMesh = true,
                     const std::string &defaultClip = "Stand");
    bool loadMd2(const std::string &modelPath,
                 const std::string &texturePath,
                 Shader *shader = nullptr,
                 bool cloneTemplateMesh = true,
                 const std::string &defaultClip = "Stand");
    bool playMd2(const std::string &clipName, float blendTime = 0.12f);

private:
    VertexAnimAsset md2Asset_;
    Md2RuntimeData  md2Runtime_;
    static void applyMd2Sample(Mesh *mesh,
                               const VertexAnimSample &sample,
                               const void *userData);
};

// ─── Md3Node ────────────────────────────────────────────────────────────────
// Dedicated MD3 instance node:
// - same per-instance animator behavior as Md2Node
// - helper attach API for tag-based hierarchy (weapon/head/etc.)
class Md3Node : public VertexAnimMeshNode
{
public:
    Md3Node();

    Md3Node *asMd3Node() override { return this; }

    bool setMd3Data(Mesh *templateMesh,
                    const Md3PartRuntime &runtime,
                    const std::vector<VertexAnimClip> &clips = {},
                    bool cloneTemplateMesh = true,
                    const std::string &defaultClip = "");

    bool setMd3Asset(const VertexAnimAsset *asset,
                     bool cloneTemplateMesh = true,
                     const std::string &defaultClip = "");
    bool loadMd3Part(const std::string &modelPath,
                     const std::string &skinPath = "",
                     const std::string &partId = "",
                     const std::string &animationCfgPath = "",
                     const std::string &clipPrefix = "",
                     bool includeBoth = false,
                     Shader *shader = nullptr,
                     bool cloneTemplateMesh = true,
                     const std::string &defaultClip = "");
    bool playMd3(const std::string &clipName, float blendTime = 0.12f);

    VertexTagSocket *attachTagNode(const std::string &tagName,
                                   Node3D *child,
                                   const glm::mat4 &localOffset = glm::mat4(1.f));

private:
    VertexAnimAsset md3Asset_;
    Md3PartRuntime  md3Runtime_;
    static void applyMd3Sample(Mesh *mesh,
                               const VertexAnimSample &sample,
                               const void *userData);
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
    // u_lightDir sent to shaders = -forward() (pointing TOWARD the light)
    glm::vec3 ambient = {0.08f, 0.08f, 0.08f}; // scene ambient contribution
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
