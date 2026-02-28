#pragma once
#include "glad/glad.h"
#include "Math.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>

class Material;
class Shader;

// ============================================================
//  Vertex — static geometry  (48 bytes)
// ============================================================
struct Vertex
{
    glm::vec3 position; // 12 bytes
    glm::vec3 normal;   // 12 bytes
    glm::vec4 tangent;  // 16 bytes  (w = handedness)
    glm::vec2 uv;       //  8 bytes
};

// ============================================================
//  AnimatedVertex — skinned geometry  (80 bytes)
// ============================================================
struct AnimatedVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::vec2 uv;
    glm::ivec4 boneIds = {0, 0, 0, 0};
    glm::vec4 boneWeights = {0.0f, 0.0f, 0.0f, 0.0f};
};

// ============================================================
//  MeshBuffer — static
// ============================================================
struct MeshBuffer
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    GLuint vao = 0, vbo = 0, ibo = 0;
    bool dynamic = false;
    GLenum mode = GL_TRIANGLES;

    void upload();
    void update();
    void draw() const;
    void drawRange(uint32_t start, uint32_t count) const;
    void free();

    Vertex &operator[](int i) { return vertices[i]; }
    int count() const { return (int)vertices.size(); }

    MeshBuffer() = default;
    MeshBuffer(const MeshBuffer &) = delete;
    MeshBuffer &operator=(const MeshBuffer &) = delete;
    ~MeshBuffer() { free(); }
};

// ============================================================
//  AnimatedMeshBuffer — skinned
// ============================================================
struct AnimatedMeshBuffer
{
    std::vector<AnimatedVertex> vertices;
    std::vector<uint32_t> indices;
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLenum mode = GL_TRIANGLES;

    void upload();
    void draw() const;
    void drawRange(uint32_t start, uint32_t count) const;
    void free();

    AnimatedVertex &operator[](int i) { return vertices[i]; }
    int count() const { return (int)vertices.size(); }

    AnimatedMeshBuffer() = default;
    AnimatedMeshBuffer(const AnimatedMeshBuffer &) = delete;
    AnimatedMeshBuffer &operator=(const AnimatedMeshBuffer &) = delete;
    ~AnimatedMeshBuffer() { free(); }
};

// ============================================================
//  Surface — geometry slice + material slot
// ============================================================
struct Surface
{
    uint32_t index_start = 0;
    uint32_t index_count = 0;
    int material_index = 0; // index into MeshBase::materials[]
    BoundingBox aabb;       // local-space; computed after upload
                            // if !aabb.is_valid() → no per-surface cull
};

// ============================================================
//  Bone
// ============================================================
struct Bone
{
    std::string name;
    glm::mat4 offset;    // inverse bind pose (model→bone space)
    glm::mat4 localPose; // rest pose in parent-local space (from file)
    int parent = -1;     // index into skeleton bone array (-1 = root)
};

// ============================================================
//  MeshBase<TBuffer> — shared data, buffer type varies
// ============================================================
template <typename TBuffer>
class MeshBase
{
public:
    std::string name;
    TBuffer buffer;
    BoundingBox aabb;
    std::vector<Surface> surfaces;
    std::vector<Material *> materials; // not owned — manager owns materials

    // ── Surfaces ──────────────────────────────────────────
    Surface &add_surface(uint32_t start, uint32_t count, int material_index = 0)
    {
        surfaces.push_back({start, count, material_index});
        return surfaces.back();
    }

    // ── Materials ─────────────────────────────────────────
    int add_material(Material *mat)
    {
        materials.push_back(mat);
        return (int)materials.size() - 1;
    }

    void set_material(int slot, Material *mat)
    {
        if (slot >= (int)materials.size())
            materials.resize(slot + 1, nullptr);
        materials[slot] = mat;
    }

    // ── AABB ──────────────────────────────────────────────
    void compute_aabb()
    {
        aabb = BoundingBox{};
        for (auto &v : buffer.vertices)
            aabb.expand(v.position);
    }

    void free() { buffer.free(); }
};

class IDrawable
{
public:
    virtual ~IDrawable() = default;
    virtual void draw() const = 0;
    virtual void drawRange(uint32_t start, uint32_t count) const = 0;
    virtual BoundingBox getAABB() const = 0;
    virtual int vertexCount() const = 0;
    virtual int indexCount() const = 0;
    // Sends bone matrices to shader (no-op for static meshes)
    virtual void applyBoneMatrices(Shader *sh) const {}
    virtual bool isSkinned() const { return false; }
};

// ============================================================
//  Mesh — static geometry
// ============================================================
class Mesh : public MeshBase<MeshBuffer>, public IDrawable
{
public:
    // Apply a matrix to all vertices in CPU memory (position, normal, tangent).
    // Call upload() afterwards to push changes to the GPU.
    void transform(const glm::mat4 &m);

    // Flip all normals (CPU side — requires upload() afterwards)
    void flip_normals();

    // Recalculate smooth normals from triangle geometry
    void compute_normals();

    // Compute tangents from UV layout (requires UVs and normals to be set first).
    // Fills Vertex::tangent.xyz; tangent.w = handedness (+1 or -1).
    void compute_tangents();

    // upload buffer + compute_aabb
    void upload();

    void draw() const override { buffer.draw(); }
    void drawRange(uint32_t s, uint32_t c) const override { buffer.drawRange(s, c); }
    BoundingBox getAABB() const override { return aabb; }
    int vertexCount() const override { return (int)buffer.vertices.size(); }
    int indexCount() const override { return (int)buffer.indices.size(); }
};

// ============================================================
//  AnimatedMesh — skinned geometry
// ============================================================
class AnimatedMesh : public MeshBase<AnimatedMeshBuffer>, public IDrawable
{
public:
    // skeleton
    std::vector<Bone> bones;              // indexed by boneIds in AnimatedVertex
    std::vector<glm::mat4> finalMatrices; // updated each frame, sent to shader

    // Compute tangents after loading (AnimatedVertex has same uv/normal/tangent layout)
    void compute_tangents();

    // upload buffer + compute_aabb
    void upload();

    void draw() const override { buffer.draw(); }
    void drawRange(uint32_t s, uint32_t c) const override { buffer.drawRange(s, c); }
    BoundingBox getAABB() const override { return aabb; }
    int vertexCount() const override { return (int)buffer.vertices.size(); }
    int indexCount() const override { return (int)buffer.indices.size(); }
    void applyBoneMatrices(Shader *sh) const override;
    bool isSkinned() const override { return true; }
};