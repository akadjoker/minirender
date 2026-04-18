#include "Mesh.hpp"
#include "Material.hpp"
#include "Animation.hpp"
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

// ============================================================
//  InstanceBuffer
// ============================================================
void InstanceBuffer::upload()
{
    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    if (vbo == 0)
        glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 matrices.size() * sizeof(glm::mat4),
                 matrices.data(), usage);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void InstanceBuffer::update()
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    matrices.size() * sizeof(glm::mat4),
                    matrices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void InstanceBuffer::free()
{
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
}

// ============================================================
//  MeshBuffer
// ============================================================
void MeshBuffer::upload()
{
    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), usage);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, tangent));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, uv));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

void MeshBuffer::update()
{
    assert(vbo != 0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex), vertices.data());
}

void MeshBuffer::draw() const
{
    glBindVertexArray(vao);
    glDrawElements(mode, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void MeshBuffer::drawRange(uint32_t start, uint32_t count) const
{
    glBindVertexArray(vao);
    const void *offset = reinterpret_cast<const void *>(static_cast<uintptr_t>(start * sizeof(uint32_t)));
    glDrawElements(mode, (GLsizei)count, GL_UNSIGNED_INT, offset);
    glBindVertexArray(0);
}

void MeshBuffer::free()
{
    if (vbo)
    {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (ibo)
    {
        glDeleteBuffers(1, &ibo);
        ibo = 0;
    }
    if (vao)
    {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

void MeshBuffer::attachInstances(InstanceBuffer *ib)
{
    // Bind the instance VBO into this mesh's VAO.
    // A mat4 occupies 4 consecutive attribute locations (one vec4 each).
    // We use locations 6-9, leaving 0-5 for vertex data and skinning.
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, ib->vbo);

    for (int col = 0; col < 4; ++col)
    {
        GLuint loc = 6 + (GLuint)col;
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE,
                              sizeof(glm::mat4),
                              (void *)(col * sizeof(glm::vec4)));
        // divisor=1: advance one instance per draw, not per vertex
        glVertexAttribDivisor(loc, 1);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void MeshBuffer::drawInstanced(int instanceCount) const
{
    glBindVertexArray(vao);
    glDrawElementsInstanced(mode, (GLsizei)indices.size(),
                            GL_UNSIGNED_INT, nullptr, instanceCount);
    glBindVertexArray(0);
}

void MeshBuffer::drawRangeInstanced(uint32_t start, uint32_t count, int instanceCount) const
{
    glBindVertexArray(vao);
    const void *offset = reinterpret_cast<const void *>(static_cast<uintptr_t>(start * sizeof(uint32_t)));
    glDrawElementsInstanced(mode, (GLsizei)count,
                            GL_UNSIGNED_INT, offset, instanceCount);
    glBindVertexArray(0);
}

// ============================================================
//  AnimatedMeshBuffer
// ============================================================
void AnimatedMeshBuffer::upload()
{
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(AnimatedVertex), vertices.data(), GL_STATIC_DRAW);

    // attrib 0-3 same layout as Vertex (position/normal/tangent/uv)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex), (void *)offsetof(AnimatedVertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex), (void *)offsetof(AnimatedVertex, normal));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex), (void *)offsetof(AnimatedVertex, tangent));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex), (void *)offsetof(AnimatedVertex, uv));
    // attrib 4-5: skinning
    glVertexAttribIPointer(4, 4, GL_INT, sizeof(AnimatedVertex), (void *)offsetof(AnimatedVertex, boneIds));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex), (void *)offsetof(AnimatedVertex, boneWeights));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

void AnimatedMeshBuffer::draw() const
{
    glBindVertexArray(vao);
    glDrawElements(mode, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void AnimatedMeshBuffer::drawRange(uint32_t start, uint32_t count) const
{
    glBindVertexArray(vao);
    const void *offset = reinterpret_cast<const void *>(static_cast<uintptr_t>(start * sizeof(uint32_t)));
    glDrawElements(mode, (GLsizei)count, GL_UNSIGNED_INT, offset);
    glBindVertexArray(0);
}

void AnimatedMeshBuffer::free()
{
    if (vbo)
    {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (ibo)
    {
        glDeleteBuffers(1, &ibo);
        ibo = 0;
    }
    if (vao)
    {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

// ============================================================
//  AnimatedVertexMeshBuffer
// ============================================================
void AnimatedVertexMeshBuffer::upload()
{
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(VertexAnimVertex), vertices.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexAnimVertex), (void *)offsetof(VertexAnimVertex, position));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAnimVertex), (void *)offsetof(VertexAnimVertex, uv));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(VertexAnimVertex), (void *)offsetof(VertexAnimVertex, normal));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

void AnimatedVertexMeshBuffer::update()
{
    if (!vbo || vertices.empty())
        return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(VertexAnimVertex), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void AnimatedVertexMeshBuffer::draw() const
{
    glBindVertexArray(vao);
    glDrawElements(mode, (GLsizei)indices.size(), GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
}

void AnimatedVertexMeshBuffer::drawRange(uint32_t start, uint32_t count) const
{
    glBindVertexArray(vao);
    const void *offset = reinterpret_cast<const void *>(static_cast<uintptr_t>(start * sizeof(uint16_t)));
    glDrawElements(mode, (GLsizei)count, GL_UNSIGNED_SHORT, offset);
    glBindVertexArray(0);
}

void AnimatedVertexMeshBuffer::free()
{
    if (vbo)
    {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (ibo)
    {
        glDeleteBuffers(1, &ibo);
        ibo = 0;
    }
    if (vao)
    {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

// ============================================================
//  TerrainBuffer
//  loc 0 = position (vec3)
//  loc 1 = normal   (vec3)
//  loc 2 = uv       (vec2) base texture
//  loc 3 = uv2      (vec2) detail texture
// ============================================================
void TerrainBuffer::upload()
{
    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(TerrainVertex), vertices.data(), usage);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, normal));
    // loc 2 = uv2 (detail), loc 3 = uv (base) — matches standard shader convention
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, uv2));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, uv));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

void TerrainBuffer::allocateDynamicIndices(size_t maxCount)
{
    // Upload VBO as STATIC (vertices never change after load)
    GLenum usage = GL_STATIC_DRAW;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(TerrainVertex), vertices.data(), usage);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, uv2));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, uv));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);

    // Pre-allocate DYNAMIC IBO at max size
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, maxCount * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

    glBindVertexArray(0);
}

void TerrainBuffer::updateIndices(size_t count)
{
    assert(ibo != 0 && count <= indices.size());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, count * sizeof(uint32_t), indices.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void TerrainBuffer::update()
{
    assert(vbo != 0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(TerrainVertex), vertices.data());
}

void TerrainBuffer::draw() const
{
    glBindVertexArray(vao);
    glDrawElements(mode, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void TerrainBuffer::drawRange(uint32_t start, uint32_t count) const
{
    glBindVertexArray(vao);
    const void *offset = reinterpret_cast<const void *>(static_cast<uintptr_t>(start * sizeof(uint32_t)));
    glDrawElements(mode, (GLsizei)count, GL_UNSIGNED_INT, offset);
    glBindVertexArray(0);
}

void TerrainBuffer::free()
{
    if (vbo) { glDeleteBuffers(1, &vbo);       vbo = 0; }
    if (ibo) { glDeleteBuffers(1, &ibo);       ibo = 0; }
    if (vao) { glDeleteVertexArrays(1, &vao);  vao = 0; }
}

// ============================================================
//  ParticleBuffer
// ============================================================
void ParticleBuffer::allocate(int maxParticles)
{
    free();
    capacity = maxParticles;
    vertices.resize(maxParticles * 4);

    // Pre-build static quad indices (never changes)
    std::vector<uint32_t> idx;
    idx.reserve(maxParticles * 6);
    for (int i = 0; i < maxParticles; ++i)
    {
        uint32_t b = i * 4;
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(maxParticles * 4 * sizeof(ParticleVertex)),
                 nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(idx.size() * sizeof(uint32_t)),
                 idx.data(), GL_STATIC_DRAW);

    // loc 0 — position vec3
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex),
                          (void *)offsetof(ParticleVertex, position));
    // loc 1 — texCoord vec2
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex),
                          (void *)offsetof(ParticleVertex, texCoord));
    // loc 2 — color vec4
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex),
                          (void *)offsetof(ParticleVertex, color));

    glBindVertexArray(0);
}

void ParticleBuffer::uploadVertices(int activeParticles)
{
    if (!vbo || activeParticles <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(activeParticles * 4 * sizeof(ParticleVertex)),
                    vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ParticleBuffer::draw() const
{
    if (!vao || capacity <= 0) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, capacity * 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void ParticleBuffer::drawRange(uint32_t start, uint32_t count) const
{
    if (!vao || count == 0) return;
    glBindVertexArray(vao);
    const void *offset = reinterpret_cast<const void *>(
        static_cast<uintptr_t>(start * sizeof(uint32_t)));
    glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, offset);
    glBindVertexArray(0);
}

void ParticleBuffer::free()
{
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo);      vbo = 0; }
    if (ibo) { glDeleteBuffers(1, &ibo);      ibo = 0; }
    capacity = 0;
    vertices.clear();
}
void Mesh::transform(const glm::mat4 &m)
{
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(m)));
    for (auto &v : buffer.vertices)
    {
        v.position = glm::vec3(m * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(normalMat * v.normal);
        glm::vec3 t = glm::normalize(normalMat * glm::vec3(v.tangent));
        v.tangent = glm::vec4(t, v.tangent.w);
    }
}

void Mesh::flip_normals()
{
    for (auto &v : buffer.vertices)
        v.normal = -v.normal;
}

void Mesh::compute_normals()
{
    for (auto &v : buffer.vertices)
        v.normal = glm::vec3(0.0f);

    const auto &idx = buffer.indices;
    auto &verts = buffer.vertices;

    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        auto &v0 = verts[idx[i]];
        auto &v1 = verts[idx[i + 1]];
        auto &v2 = verts[idx[i + 2]];

        glm::vec3 e1 = v1.position - v0.position;
        glm::vec3 e2 = v2.position - v0.position;
        glm::vec3 n = glm::cross(e1, e2); // unnormalized — weights by area

        v0.normal += n;
        v1.normal += n;
        v2.normal += n;
    }

    for (auto &v : verts)
        if (glm::length(v.normal) > 1e-6f)
            v.normal = glm::normalize(v.normal);
}

void Mesh::compute_tangents()
{
    auto &verts = buffer.vertices;
    const auto &idx = buffer.indices;
    const size_t n = verts.size();

    std::vector<glm::vec3> tan1(n, glm::vec3(0.f));
    std::vector<glm::vec3> tan2(n, glm::vec3(0.f));

    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];

        const glm::vec3 &p0 = verts[i0].position;
        const glm::vec3 &p1 = verts[i1].position;
        const glm::vec3 &p2 = verts[i2].position;
        const glm::vec2 &uv0 = verts[i0].uv;
        const glm::vec2 &uv1 = verts[i1].uv;
        const glm::vec2 &uv2 = verts[i2].uv;

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;
        glm::vec2 d1 = uv1 - uv0;
        glm::vec2 d2 = uv2 - uv0;

        float denom = d1.x * d2.y - d2.x * d1.y;
        float r = (glm::abs(denom) > 1e-8f) ? (1.f / denom) : 0.f;

        glm::vec3 sdir = r * (d2.y * e1 - d1.y * e2);
        glm::vec3 tdir = r * (-d2.x * e1 + d1.x * e2);

        tan1[i0] += sdir;
        tan1[i1] += sdir;
        tan1[i2] += sdir;
        tan2[i0] += tdir;
        tan2[i1] += tdir;
        tan2[i2] += tdir;
    }

    for (size_t i = 0; i < n; ++i)
    {
        const glm::vec3 &n = verts[i].normal;
        const glm::vec3 &t = tan1[i];
        glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));
        float hand = (glm::dot(glm::cross(n, t), tan2[i]) < 0.f) ? -1.f : 1.f;
        verts[i].tangent = glm::vec4(tangent, hand);
    }
}

void Mesh::upload()
{
    compute_aabb();
    compute_surface_aabbs();
    buffer.upload();
}

// ============================================================
//  AnimatedMesh
// ============================================================
AnimatedMesh::~AnimatedMesh()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Destroying AnimatedMesh '%s'\n", name.c_str());
    releaseAnimations();
}

void AnimatedMesh::releaseAnimations()
{
    for (Animation *animation : animations)
        delete animation;
    animations.clear();
}

void AnimatedMesh::compute_tangents()
{
    auto &verts = buffer.vertices;
    const auto &idx = buffer.indices;
    const size_t n = verts.size();

    std::vector<glm::vec3> tan1(n, glm::vec3(0.f));
    std::vector<glm::vec3> tan2(n, glm::vec3(0.f));

    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];

        const glm::vec3 &p0 = verts[i0].position;
        const glm::vec3 &p1 = verts[i1].position;
        const glm::vec3 &p2 = verts[i2].position;
        const glm::vec2 &uv0 = verts[i0].uv;
        const glm::vec2 &uv1 = verts[i1].uv;
        const glm::vec2 &uv2 = verts[i2].uv;

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;
        glm::vec2 d1 = uv1 - uv0;
        glm::vec2 d2 = uv2 - uv0;

        float denom = d1.x * d2.y - d2.x * d1.y;
        float r = (glm::abs(denom) > 1e-8f) ? (1.f / denom) : 0.f;

        glm::vec3 sdir = r * (d2.y * e1 - d1.y * e2);
        glm::vec3 tdir = r * (-d2.x * e1 + d1.x * e2);

        tan1[i0] += sdir;
        tan1[i1] += sdir;
        tan1[i2] += sdir;
        tan2[i0] += tdir;
        tan2[i1] += tdir;
        tan2[i2] += tdir;
    }

    for (size_t i = 0; i < n; ++i)
    {
        const glm::vec3 &n = verts[i].normal;
        const glm::vec3 &t = tan1[i];
        glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));
        float hand = (glm::dot(glm::cross(n, t), tan2[i]) < 0.f) ? -1.f : 1.f;
        verts[i].tangent = glm::vec4(tangent, hand);
    }
}

void AnimatedMesh::upload()
{
    compute_aabb();
    compute_surface_aabbs();
    buffer.upload();
}

BoundingBox AnimatedMesh::computeSkinnedAABB() const
{
    if (buffer.vertices.empty())
        return BoundingBox{};

    if (finalMatrices.empty())
        return aabb;

    BoundingBox posed;
    for (const AnimatedVertex &v : buffer.vertices)
    {
        glm::vec4 skinned(0.0f);
        const int ids[4] = {v.boneIds.x, v.boneIds.y, v.boneIds.z, v.boneIds.w};
        const float weights[4] = {v.boneWeights.x, v.boneWeights.y, v.boneWeights.z, v.boneWeights.w};

        for (int i = 0; i < 4; ++i)
        {
            if (weights[i] <= 0.0f)
                continue;
            if (ids[i] < 0 || ids[i] >= (int)finalMatrices.size())
                continue;
            skinned += weights[i] * (finalMatrices[ids[i]] * glm::vec4(v.position, 1.0f));
        }

        if (skinned.w == 0.0f)
            skinned = glm::vec4(v.position, 1.0f);

        posed.expand(glm::vec3(skinned));
    }

    return posed;
}

 

void VertexAnimatedMesh::compute_normals()
{
    for (auto &v : buffer.vertices)
        v.normal = glm::vec3(0.0f);

    const auto &idx = buffer.indices;
    auto &verts = buffer.vertices;

    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        auto &v0 = verts[idx[i]];
        auto &v1 = verts[idx[i + 1]];
        auto &v2 = verts[idx[i + 2]];

        glm::vec3 e1 = v1.position - v0.position;
        glm::vec3 e2 = v2.position - v0.position;
        glm::vec3 n = glm::cross(e1, e2);

        v0.normal += n;
        v1.normal += n;
        v2.normal += n;
    }

    for (auto &v : verts)
        if (glm::length(v.normal) > 1e-6f)
            v.normal = glm::normalize(v.normal);
}

void VertexAnimatedMesh::upload()
{
    compute_aabb();
    compute_surface_aabbs();
    buffer.upload();
}

int VertexAnimatedMesh::findTag(const char *name) const
{
    if (!name || tagsPerFrame <= 0 || tags.empty())
        return -1;

    const int count = std::min(tagsPerFrame, (int)tags.size());
    for (int i = 0; i < count; ++i)
    {
        if (std::strncmp(tags[i].tag, name, sizeof(tags[i].tag)) == 0)
            return i;
    }
    return -1;
}

namespace
{
bool resolve_frame_pair(float frame, int totalFrames, int clipStart, int clipEnd,
                        int &frame0, int &frame1, float &t)
{
    if (totalFrames <= 0)
        return false;

    const int start = glm::clamp(std::min(clipStart, clipEnd), 0, totalFrames - 1);
    const int end = glm::clamp(std::max(clipStart, clipEnd), 0, totalFrames - 1);
    const float span = (float)(end - start + 1);
    if (span <= 0.0f)
        return false;

    float local = std::fmod(frame - (float)start, span);
    if (local < 0.0f)
        local += span;

    const float wrapped = (float)start + local;
    frame0 = (int)wrapped;
    frame1 = (frame0 >= end) ? start : (frame0 + 1);
    t = wrapped - (float)frame0;
    return true;
}
}

void VertexAnimatedMesh::setFrame(float frame)
{
    const int frames = frameCount();
    const std::size_t verts = buffer.vertices.size();
    if (frames <= 0 || verts == 0 || framePositions.size() != std::size_t(frames) * verts)
        return;

    currentFrame = frame;

    float wrapped = frame;
    while (wrapped < 0.0f)
        wrapped += (float)frames;
    if (frames > 0)
        wrapped = std::fmod(wrapped, (float)frames);

    const int frame0 = (int)wrapped;
    const int frame1 = (frame0 + 1) % frames;
    const float t = wrapped - (float)frame0;

    const std::size_t base0 = std::size_t(frame0) * verts;
    const std::size_t base1 = std::size_t(frame1) * verts;

    for (std::size_t i = 0; i < verts; ++i)
    {
        const glm::vec3 p0 = framePositions[base0 + i];
        const glm::vec3 p1 = framePositions[base1 + i];
        const glm::vec3 p = glm::mix(p0, p1, t);
        buffer.vertices[i].position = p;
        if (i < buffer.positions.size())
            buffer.positions[i] = p;
    }

    compute_normals();
    compute_aabb();
    compute_surface_aabbs();
    buffer.update();
}

void VertexAnimatedMesh::setFrame(float frame, int startFrame, int endFrame)
{
    const int frames = frameCount();
    const std::size_t verts = buffer.vertices.size();
    if (frames <= 0 || verts == 0 || framePositions.size() != std::size_t(frames) * verts)
        return;

    currentFrame = frame;

    int frame0 = 0;
    int frame1 = 0;
    float t = 0.0f;
    if (!resolve_frame_pair(frame, frames, startFrame, endFrame, frame0, frame1, t))
        return;

    const std::size_t base0 = std::size_t(frame0) * verts;
    const std::size_t base1 = std::size_t(frame1) * verts;

    for (std::size_t i = 0; i < verts; ++i)
    {
        const glm::vec3 p0 = framePositions[base0 + i];
        const glm::vec3 p1 = framePositions[base1 + i];
        const glm::vec3 p = glm::mix(p0, p1, t);
        buffer.vertices[i].position = p;
        if (i < buffer.positions.size())
            buffer.positions[i] = p;
    }

    compute_normals();
    compute_aabb();
    compute_surface_aabbs();
    buffer.update();
}

void VertexAnimatedMesh::setFrameBlended(float fromFrame, int fromStartFrame, int fromEndFrame,
                                         float toFrame, int toStartFrame, int toEndFrame, float alpha)
{
    const int frames = frameCount();
    const std::size_t verts = buffer.vertices.size();
    if (frames <= 0 || verts == 0 || framePositions.size() != std::size_t(frames) * verts)
        return;

    currentFrame = toFrame;

    int from0 = 0;
    int from1 = 0;
    float fromT = 0.0f;
    if (!resolve_frame_pair(fromFrame, frames, fromStartFrame, fromEndFrame, from0, from1, fromT))
        return;

    int to0 = 0;
    int to1 = 0;
    float toT = 0.0f;
    if (!resolve_frame_pair(toFrame, frames, toStartFrame, toEndFrame, to0, to1, toT))
        return;

    alpha = glm::clamp(alpha, 0.0f, 1.0f);

    const std::size_t baseFrom0 = std::size_t(from0) * verts;
    const std::size_t baseFrom1 = std::size_t(from1) * verts;
    const std::size_t baseTo0 = std::size_t(to0) * verts;
    const std::size_t baseTo1 = std::size_t(to1) * verts;

    for (std::size_t i = 0; i < verts; ++i)
    {
        const glm::vec3 fromPose = glm::mix(framePositions[baseFrom0 + i], framePositions[baseFrom1 + i], fromT);
        const glm::vec3 toPose = glm::mix(framePositions[baseTo0 + i], framePositions[baseTo1 + i], toT);
        const glm::vec3 p = glm::mix(fromPose, toPose, alpha);
        buffer.vertices[i].position = p;
        if (i < buffer.positions.size())
            buffer.positions[i] = p;
    }

    compute_normals();
    compute_aabb();
    compute_surface_aabbs();
    buffer.update();
}

bool VertexAnimatedMesh::sampleTag(int tagIndex, float frame, glm::mat4 &out) const
{
    const int frames = frameCount();
    if (tagIndex < 0 || tagsPerFrame <= 0 || frames <= 0)
        return false;
    if (tagIndex >= tagsPerFrame)
        return false;
    if ((int)tags.size() < tagsPerFrame * frames)
        return false;

    // Wrap do frame para [0, frames)
    float wrapped = std::fmod(frame, (float)frames);
    if (wrapped < 0.0f)
        wrapped += (float)frames;

    const int   frame0 = (int)wrapped;
    const int   frame1 = (frame0 + 1) % frames;
    const float t      = wrapped - (float)frame0;

    const MeshTag &a = tags[frame0 * tagsPerFrame + tagIndex];
    const MeshTag &b = tags[frame1 * tagsPerFrame + tagIndex];

    // Interpola origin
    const glm::vec3 origin = glm::mix(a.origin, b.origin, t);

    // Interpola os 3 eixos
    glm::vec3 axis0 = glm::mix(a.axis[0], b.axis[0], t);
    glm::vec3 axis1 = glm::mix(a.axis[1], b.axis[1], t);

    // Gram-Schmidt — garante base ortonormal sem depender de orthonormalizeBasis
    // X fica como referência principal (axis0 do MD3 = right)
    const glm::vec3 x = glm::normalize(axis0);

    // Y ortogonal a X (axis1 do MD3 = forward)
    const glm::vec3 y = glm::normalize(axis1 - glm::dot(axis1, x) * x);

    // Z = cross(X, Y) — completamente derivado, sempre ortogonal
    const glm::vec3 z = glm::cross(x, y);

 
    out = glm::mat4(
        glm::vec4(x,      0.0f),   // coluna 0 — eixo X (right)
        glm::vec4(y,      0.0f),   // coluna 1 — eixo Y (forward)
        glm::vec4(z,      0.0f),   // coluna 2 — eixo Z (up)
        glm::vec4(origin, 1.0f)    // coluna 3 — translação
    );
    return true;
}

bool VertexAnimatedMesh::sampleTag(int tagIndex, float frame, int startFrame, int endFrame, glm::mat4 &out) const
{
    const int frames = frameCount();
    if (tagIndex < 0 || tagsPerFrame <= 0 || frames <= 0)
        return false;
    if (tagIndex >= tagsPerFrame)
        return false;
    if ((int)tags.size() < tagsPerFrame * frames)
        return false;

    int frame0 = 0;
    int frame1 = 0;
    float t = 0.0f;
    if (!resolve_frame_pair(frame, frames, startFrame, endFrame, frame0, frame1, t))
        return false;

    const MeshTag &a = tags[frame0 * tagsPerFrame + tagIndex];
    const MeshTag &b = tags[frame1 * tagsPerFrame + tagIndex];

    const glm::vec3 origin = glm::mix(a.origin, b.origin, t);
    glm::vec3 axis0 = glm::mix(a.axis[0], b.axis[0], t);
    glm::vec3 axis1 = glm::mix(a.axis[1], b.axis[1], t);

    const glm::vec3 x = glm::normalize(axis0);
    const glm::vec3 y = glm::normalize(axis1 - glm::dot(axis1, x) * x);
    const glm::vec3 z = glm::cross(x, y);

    out = glm::mat4(
        glm::vec4(x, 0.0f),
        glm::vec4(y, 0.0f),
        glm::vec4(z, 0.0f),
        glm::vec4(origin, 1.0f));
    return true;
}

bool VertexAnimatedMesh::sampleTagBlended(int tagIndex,
                                          float fromFrame, int fromStartFrame, int fromEndFrame,
                                          float toFrame, int toStartFrame, int toEndFrame,
                                          float alpha, glm::mat4 &out) const
{
    glm::mat4 fromTag;
    if (!sampleTag(tagIndex, fromFrame, fromStartFrame, fromEndFrame, fromTag))
        return false;

    glm::mat4 toTag;
    if (!sampleTag(tagIndex, toFrame, toStartFrame, toEndFrame, toTag))
        return false;

    alpha = glm::clamp(alpha, 0.0f, 1.0f);

    const glm::vec3 origin = glm::mix(glm::vec3(fromTag[3]), glm::vec3(toTag[3]), alpha);
    glm::vec3 axis0 = glm::mix(glm::vec3(fromTag[0]), glm::vec3(toTag[0]), alpha);
    glm::vec3 axis1 = glm::mix(glm::vec3(fromTag[1]), glm::vec3(toTag[1]), alpha);

    const glm::vec3 x = glm::normalize(axis0);
    const glm::vec3 y = glm::normalize(axis1 - glm::dot(axis1, x) * x);
    const glm::vec3 z = glm::cross(x, y);

    out = glm::mat4(
        glm::vec4(x, 0.0f),
        glm::vec4(y, 0.0f),
        glm::vec4(z, 0.0f),
        glm::vec4(origin, 1.0f));
    return true;
}

bool VertexAnimatedMesh::sampleTag(const char *name, float frame, glm::mat4 &out) const
{
    return sampleTag(findTag(name), frame, out);
}

void AnimatedMesh::applyBoneMatrices(Shader *sh) const
{
    if (!sh) return;
    const int count = (int)finalMatrices.size();
    for (int i = 0; i < count && i < 100; ++i)
    {
        // glUniform is cheap with caching; batch all bones
        sh->setMat4("u_boneMatrices[" + std::to_string(i) + "]", finalMatrices[i]);
    }
}

// ============================================================
//  Picking helpers (shared logic via template lambda)
// ============================================================
namespace {

// Transform world ray into object space (handles non-uniform scale).
// Returns the local-space direction (NOT normalised — normalise before use).
inline void worldRayToLocal(const Ray         &worldRay,
                             const glm::mat4   &model,
                             glm::vec3         &localOrig,
                             glm::vec3         &localDirN)
{
    glm::mat4 inv = glm::inverse(model);
    localOrig = glm::vec3(inv * glm::vec4(worldRay.origin,    1.f));
    // direction: 0-component → not translated, only rotated/scaled
    glm::vec3 ld = glm::vec3(inv * glm::vec4(worldRay.direction, 0.f));
    localDirN = glm::normalize(ld);
}

// Convert a local-space hit back into world space and write it into `result`.
inline void localHitToWorld(PickResult       &result,
                             const glm::vec3  &localOrig,
                             const glm::vec3  &localDirN,
                             const glm::mat4  &model,
                             const glm::vec3  &worldRayOrigin)
{
    glm::vec3 localPt = localOrig + localDirN * result.distance;
    result.point       = glm::vec3(model * glm::vec4(localPt, 1.f));
    result.distance    = glm::length(result.point - worldRayOrigin);
    // Normal: inverse-transpose handles non-uniform scale correctly
    glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(model)));
    result.normal = glm::normalize(nm * result.normal);
}

} // anonymous namespace

// ============================================================
//  Mesh::pick
// ============================================================
PickResult Mesh::pick(const Ray &worldRay, const glm::mat4 &model) const
{
    // ── Phase 1: ray → object space ────────────────────────────────────
    glm::vec3 lo, ld;
    worldRayToLocal(worldRay, model, lo, ld);

    // ── Phase 2: AABB reject ─────────────────────────────────────────
    if (!aabb.is_valid() || aabb.intersects_ray(lo, ld) < 0.f)
        return {};

    // ── Phase 3: triangle test (Möller–Trumbore) ────────────────────
    const auto &verts = buffer.vertices;
    const auto &idx   = buffer.indices;

    PickResult best;
    best.distance = std::numeric_limits<float>::max();
    Triangle tri;

    for (int s = 0; s < (int)surfaces.size(); ++s)
    {
        const auto &surf = surfaces[s];
        const uint32_t end = surf.index_start + surf.index_count;
        for (uint32_t i = surf.index_start; i + 2 < end; i += 3)
        {
            tri.v0 = verts[idx[i    ]].position;
            tri.v1 = verts[idx[i + 1]].position;
            tri.v2 = verts[idx[i + 2]].position;
            float t = tri.intersect_ray(lo, ld);
            if (t > 0.f && t < best.distance)
            {
                best.hit           = true;
                best.distance      = t;
                best.surfaceIndex  = s;
                best.triangleIndex = (int)((i - surf.index_start) / 3);
                best.normal        = tri.normal();
            }
        }
    }

    if (!best.hit) return {};
    localHitToWorld(best, lo, ld, model, worldRay.origin);
    return best;
}

// ============================================================
//  AnimatedMesh::pick  (uses rest-pose vertex positions)
// ============================================================
PickResult AnimatedMesh::pick(const Ray &worldRay, const glm::mat4 &model) const
{
    glm::vec3 lo, ld;
    worldRayToLocal(worldRay, model, lo, ld);

    if (!aabb.is_valid() || aabb.intersects_ray(lo, ld) < 0.f)
        return {};

    const auto &verts = buffer.vertices;
    const auto &idx   = buffer.indices;

    PickResult best;
    best.distance = std::numeric_limits<float>::max();
    Triangle tri;

    for (int s = 0; s < (int)surfaces.size(); ++s)
    {
        const auto &surf = surfaces[s];
        const uint32_t end = surf.index_start + surf.index_count;
        for (uint32_t i = surf.index_start; i + 2 < end; i += 3)
        {
            tri.v0 = verts[idx[i    ]].position;
            tri.v1 = verts[idx[i + 1]].position;
            tri.v2 = verts[idx[i + 2]].position;
            float t = tri.intersect_ray(lo, ld);
            if (t > 0.f && t < best.distance)
            {
                best.hit           = true;
                best.distance      = t;
                best.surfaceIndex  = s;
                best.triangleIndex = (int)((i - surf.index_start) / 3);
                best.normal        = tri.normal();
            }
        }
    }

    if (!best.hit) return {};
    localHitToWorld(best, lo, ld, model, worldRay.origin);
    return best;
}

PickResult VertexAnimatedMesh::pick(const Ray &worldRay, const glm::mat4 &model) const
{
    glm::vec3 lo, ld;
    worldRayToLocal(worldRay, model, lo, ld);

    if (!aabb.is_valid() || aabb.intersects_ray(lo, ld) < 0.f)
        return {};

    const auto &verts = buffer.vertices;
    const auto &idx   = buffer.indices;

    PickResult best;
    best.distance = std::numeric_limits<float>::max();
    Triangle tri;

    for (int s = 0; s < (int)surfaces.size(); ++s)
    {
        const auto &surf = surfaces[s];
        const uint32_t end = surf.index_start + surf.index_count;
        for (uint32_t i = surf.index_start; i + 2 < end; i += 3)
        {
            tri.v0 = verts[idx[i    ]].position;
            tri.v1 = verts[idx[i + 1]].position;
            tri.v2 = verts[idx[i + 2]].position;
            float t = tri.intersect_ray(lo, ld);
            if (t > 0.f && t < best.distance)
            {
                best.hit           = true;
                best.distance      = t;
                best.surfaceIndex  = s;
                best.triangleIndex = (int)((i - surf.index_start) / 3);
                best.normal        = tri.normal();
            }
        }
    }

    if (!best.hit) return {};
    localHitToWorld(best, lo, ld, model, worldRay.origin);
    return best;
}

//**************************************************************************** */
