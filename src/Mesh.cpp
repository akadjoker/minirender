#include "Mesh.hpp"
#include <glm/gtc/matrix_inverse.hpp>

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
//  Mesh
// ============================================================
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
    buffer.upload();
    compute_aabb();
}

// ============================================================
//  AnimatedMesh
// ============================================================
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
    buffer.upload();
    compute_aabb();
}