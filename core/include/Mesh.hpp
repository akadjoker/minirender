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
//  IDrawable — interface for anything that can be rendered
// ============================================================
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
//  MeshBuffer — static
// ============================================================
struct MeshBuffer : public IDrawable
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    GLuint vao = 0, vbo = 0, ibo = 0;
    bool dynamic = false;
    GLenum mode = GL_TRIANGLES;
    BoundingBox aabb = {};     // optional — terrain/custom nodes fill this

    void upload();
    void update();
    void draw() const override;
    void drawRange(uint32_t start, uint32_t count) const override;
    void free();

    // IDrawable
    BoundingBox getAABB() const override { return aabb; }
    int vertexCount() const override { return (int)vertices.size(); }
    int indexCount() const override { return (int)indices.size(); }

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
struct AnimatedMeshBuffer : public IDrawable
{
    std::vector<AnimatedVertex> vertices;
    std::vector<uint32_t> indices;
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLenum mode = GL_TRIANGLES;

    void upload();
    void draw() const override;
    void drawRange(uint32_t start, uint32_t count) const override;
    void free();

    // IDrawable
    BoundingBox getAABB() const override { return BoundingBox{}; }
    int vertexCount() const override { return (int)vertices.size(); }
    int indexCount() const override { return (int)indices.size(); }

    AnimatedVertex &operator[](int i) { return vertices[i]; }
    int count() const { return (int)vertices.size(); }

    AnimatedMeshBuffer() = default;
    AnimatedMeshBuffer(const AnimatedMeshBuffer &) = delete;
    AnimatedMeshBuffer &operator=(const AnimatedMeshBuffer &) = delete;
    ~AnimatedMeshBuffer() { free(); }
};

// ============================================================
//  TerrainVertex — 40 bytes, no tangent, two UV sets
//
//  loc 0  position  vec3   — world-space XYZ
//  loc 1  normal    vec3   — smooth shading normal
//  loc 2  uv        vec2   — base texture (scales with terrain)
//  loc 3  uv2       vec2   — detail texture (tiles rapidly)
//
//  Tangent is intentionally omitted: terrain normal-maps are
//  usually in object-space, and when tangent-space NM is needed
//  a TBN can be reconstructed in the vertex shader cheaply
//  (T = cross(normal, vec3(0,0,1)), B = cross(T, normal)).
// ============================================================
struct TerrainVertex
{
    glm::vec3 position; // 12 bytes
    glm::vec3 normal;   // 12 bytes
    glm::vec2 uv;       //  8 bytes — base tex
    glm::vec2 uv2;      //  8 bytes — detail tex
};

// ============================================================
//  TerrainBuffer
// ============================================================
struct TerrainBuffer : public IDrawable
{
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t>      indices;
    GLuint vao   = 0, vbo = 0, ibo = 0;
    bool   dynamic = false;
    GLenum mode    = GL_TRIANGLES;
    BoundingBox aabb = {};

    void upload();                              // static VBO + static IBO
    void allocateDynamicIndices(size_t maxCount);// static VBO + pre-alloc dynamic IBO
    void updateIndices(size_t count);            // glBufferSubData first `count` indices
    void update();
    void draw()          const override;
    void drawRange(uint32_t start, uint32_t count) const override;
    void free();

    // IDrawable
    BoundingBox getAABB()    const override { return aabb; }
    int vertexCount()        const override { return (int)vertices.size(); }
    int indexCount()         const override { return (int)indices.size(); }

    TerrainVertex &operator[](int i) { return vertices[i]; }
    int  count() const { return (int)vertices.size(); }

    TerrainBuffer() = default;
    TerrainBuffer(const TerrainBuffer &) = delete;
    TerrainBuffer &operator=(const TerrainBuffer &) = delete;
    ~TerrainBuffer() { free(); }
};

// ============================================================
//  ParticleVertex — 36 bytes, billboard quads
//
//  loc 0  position  vec3  — world-space XYZ  (built each frame)
//  loc 1  texCoord  vec2  — atlas UV
//  loc 2  color     vec4  — rgba tint + alpha
// ============================================================
struct ParticleVertex
{
    glm::vec3 position; // 12
    glm::vec2 texCoord; //  8
    glm::vec4 color;    // 16
};                      // 36 bytes total

// ============================================================
//  ParticleBuffer — dynamic VBO, pre-baked static IBO
// ============================================================
struct ParticleBuffer : public IDrawable
{
    std::vector<ParticleVertex> vertices;   // CPU-side, rebuilt each frame
    GLuint vao      = 0, vbo = 0, ibo = 0;
    int    capacity = 0;                    // max particles allocated on GPU

    // Call once: allocates VAO, dynamic VBO and static quad-index IBO.
    void allocate(int maxParticles);

    // Partial upload: only the first activeParticles*4 vertices.
    void uploadVertices(int activeParticles);

    void draw()      const override;
    void drawRange(uint32_t start, uint32_t count) const override;
    void free();

    // IDrawable
    BoundingBox getAABB()     const override { return BoundingBox{}; }
    int         vertexCount() const override { return (int)vertices.size(); }
    int         indexCount()  const override { return capacity * 6; }

    ParticleBuffer() = default;
    ParticleBuffer(const ParticleBuffer &) = delete;
    ParticleBuffer &operator=(const ParticleBuffer &) = delete;
    ~ParticleBuffer() { free(); }
};

// ============================================================
//  Surface — geometry slice + material slot
// ============================================================
struct Surface
{
    uint32_t index_start = 0;
    uint32_t index_count = 0;
    int material_index = 0; // index into MeshBase::materials[]
    BoundingBox aabb = {};
    Surface() = default;
    Surface(uint32_t start, uint32_t count, int mat)
        : index_start(start), index_count(count), material_index(mat) {}
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
    BoundingBox aabb = {};
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

    void compute_surface_aabbs()
    {
        for (auto &surface : surfaces)
        {
            surface.aabb = BoundingBox{};

            const uint32_t start = surface.index_start;
            const uint32_t end = std::min<uint32_t>(surface.index_start + surface.index_count,
                                                    static_cast<uint32_t>(buffer.indices.size()));

            for (uint32_t i = start; i < end; ++i)
            {
                const uint32_t vertexIndex = buffer.indices[i];
                if (vertexIndex < static_cast<uint32_t>(buffer.vertices.size()))
                    surface.aabb.expand(buffer.vertices[vertexIndex].position);
            }
        }
    }


    void free() { buffer.free(); }
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

    // ── Picking ───────────────────────────────────────────────
    // worldRay in world space; model = world matrix of the owning node.
    // Phase 1: AABB reject.  Phase 2: Möller–Trumbore per triangle.
    // Returns best hit or hit==false if no intersection.
    PickResult pick(const Ray &worldRay, const glm::mat4 &model = glm::mat4(1.f)) const;
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

    // Same two-phase pick as Mesh::pick — uses rest-pose vertex positions.
    PickResult pick(const Ray &worldRay, const glm::mat4 &model = glm::mat4(1.f)) const;
};
