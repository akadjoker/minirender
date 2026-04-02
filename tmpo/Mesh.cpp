#include "pch.h"
#include "Vertex.hpp"
#include "Driver.hpp"
#include "Mesh.hpp"

#include "Texture.hpp"
#include "Stream.hpp"
#include "Batch.hpp"
#include "Pixmap.hpp"
#include "Animation.hpp"
#include "glad/glad.h"

Material::Material()
{
    m_layers = 1;
    m_shininess = 0.0f;
    for (int i = 0; i < MAX_TEXTURES; i++)
        m_texture[i] = TextureManager::Instance().GetDefault();
}

void Material::SetTexture(u32 index, Texture *texture)
{
    if (index < MAX_TEXTURES && texture)
    {
        m_layers = Max(m_layers, index + 1);
        m_texture[index] = texture;
    }
}

Texture *Material::GetTexture(u32 index) const
{
    if (index < MAX_TEXTURES)
        return m_texture[index];
    return nullptr;
}

MeshBuffer::MeshBuffer(const std::string &name) : Spatial(name)
{
    buffer = new VertexArray();
    vb = nullptr;
    ib = nullptr;
    m_boundBox.clear();
    m_vdirty = true;
    m_idirty = true;
}

MeshBuffer::~MeshBuffer()
{
    delete buffer;
}

VertexBuffer *MeshBuffer::CreateVertexBuffer(u32 vertexCount, bool dynamic)
{
    if (vb)
    {
        LogWarning("[MeshBuffer] Vertex buffer already created.");
        return vb;
    }
    m_DynamicVertexBuffer = dynamic;
    m_vdirty = true;
    vb = buffer->AddVertexBuffer(sizeof(Vertex), vertexCount, dynamic);
    return vb;
}

IndexBuffer *MeshBuffer::CreateIndexBuffer(u32 indexCount, bool dynamic)
{
    if (ib)
    {
        LogWarning("[MeshBuffer] Index buffer already created.");
        return ib;
    }
    m_DynamicIndexBuffer = dynamic;
    ib = buffer->CreateIndexBuffer(indexCount, dynamic, false);
    m_idirty = true;
    return ib;
}

void MeshBuffer::Clear()
{
    vertices.clear();
    indices.clear();
    m_vdirty = true;
    m_idirty = true;
}

void MeshBuffer::ClearVertices()
{
    vertices.clear();
    m_vdirty = true;
}

void MeshBuffer::ClearIndices()
{
    indices.clear();
    m_idirty = true;
}

u32 MeshBuffer::AddVertex(const Vertex &v)
{
    m_boundBox.expand(v.x, v.y, v.z);
    vertices.push_back(v);
    m_vdirty = true;
    return vertices.size() - 1;
}

u32 MeshBuffer::AddVertex(float x, float y, float z, float u, float v)
{
    m_vdirty = true;
    return AddVertex({x, y, z, 0, 0, 0, u, v});
}

u32 MeshBuffer::AddVertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
{
    m_vdirty = true;
    return AddVertex({x, y, z, nx, ny, nz, u, v});
}

u32 MeshBuffer::AddIndex(u32 index)
{
    m_idirty = true;
    indices.push_back(index);
    return indices.size() - 1;
}

u32 MeshBuffer::AddFace(u32 i0, u32 i1, u32 i2)
{
    m_idirty = true;
    indices.push_back(i0);
    indices.push_back(i1);
    indices.push_back(i2);
    return indices.size() - 3;
}

void MeshBuffer::SetMaterial(u32 material)
{
    m_material = material;
}

void MeshBuffer::Build()
{

    if (!vb)
    {
        vb = buffer->AddVertexBuffer(sizeof(Vertex), vertices.size(), m_DynamicVertexBuffer);

        auto *decl = buffer->GetVertexDeclaration();

        decl->AddElement(0, 0, VET_FLOAT3, VES_POSITION);
        decl->AddElement(0, 3 * sizeof(float), VET_FLOAT3, VES_NORMAL);
        decl->AddElement(0, 6 * sizeof(float), VET_FLOAT2, VES_TEXCOORD, 0);
    }

    if (!ib)
    {
        ib = buffer->CreateIndexBuffer(indices.size(), m_DynamicIndexBuffer, false);
    }

    if (m_vdirty)
    {
        if (m_DynamicVertexBuffer)
        {
            vb->SetSubData(0, vertices.size(), vertices.data());
        }
        else
        {
            vb->SetData(vertices.data());
        }
    }

    if (m_idirty)
    {
        if (m_DynamicIndexBuffer)
        {
            ib->SetSubData(0, indices.size(), indices.data());
        }
        else
        {
            ib->SetData(indices.data());
        }
    }

    buffer->Build();

    m_idirty = false;
    m_vdirty = false;
}

void MeshBuffer::Render()
{
    if (m_idirty || m_vdirty)
    {
        Build();
    }
    buffer->Render(PrimitiveType::PT_TRIANGLES, indices.size());
}

void MeshBuffer::Render(PrimitiveType type, u32 count)
{
    if (m_idirty || m_vdirty)
    {
        Build();
    }
    buffer->Render(type, count);
}

void MeshBuffer::Debug(RenderBatch *batch)
{

    // tris lines

    // batch->SetColor(255, 0, 0);
    // for (size_t i = 0; i < indices.size(); i += 3)
    // {

    //     const Vertex v0 = vertices[indices[i]];
    //     const Vertex v1 = vertices[indices[i + 1]];
    //     const Vertex v2 = vertices[indices[i + 2]];
    //     const Vec3 p0 = Vec3(v0.x, v0.y, v0.z);
    //     const Vec3 p1 = Vec3(v1.x, v1.y, v1.z);
    //     const Vec3 p2 = Vec3(v2.x, v2.y, v2.z);
    //   //  batch->TriangleLines(p0, p1, p2);
    //    // batch->Line3D(p0, p1);
    // }

    /// vertex animaltions lines

    if (!m_skinnedVertices.empty())
    {
        batch->SetColor(0, 255, 0);
        for (size_t i = 0; i < indices.size(); i += 3)
        {

            const Vertex &v0 = m_skinnedVertices[indices[i]];
            const Vertex &v1 = m_skinnedVertices[indices[i + 1]];
            const Vertex &v2 = m_skinnedVertices[indices[i + 2]];
            const Vec3 p0 = Vec3(v0.x, v0.y, v0.z);
            const Vec3 p1 = Vec3(v1.x, v1.y, v1.z);
            const Vec3 p2 = Vec3(v2.x, v2.y, v2.z);
            batch->TriangleLines(p0, p1, p2);
        }
    }
    else
    {
        // LogWarning("Skinned vertices empty");
    }
}

void MeshBuffer::UpdateSkinning(const std::vector<Mat4> &boneMatrices)
{

    if (!m_isSkinned || boneMatrices.empty() || vertices.empty() || m_skinData.empty())
        return;

    for (size_t i = 0; i < vertices.size(); i++)
    {
        const Vertex &original = vertices[i];
        const VertexSkin &skin = m_skinData[i];

        Vec3 finalPos(0, 0, 0);
        Vec3 finalNormal(0, 0, 0);

        for (int j = 0; j < 4; j++)
        {
            float weight = skin.weights[j];
            if (weight == 0.0f)
                continue;

            u8 boneID = skin.boneIDs[j];

            if (boneID >= boneMatrices.size())
                continue;

            const Mat4 &boneMatrix = boneMatrices[boneID];

            Vec3 transformedPos = boneMatrix.TransformPoint(Vec3(original.x, original.y, original.z));
            finalPos += transformedPos * weight;

            Vec3 transformedNormal = boneMatrix.TransformVector(Vec3(original.nx, original.ny, original.nz));
            finalNormal += transformedNormal * weight;
        }

        // normalizar normal
        float len = sqrt(finalNormal.x * finalNormal.x +
                         finalNormal.y * finalNormal.y +
                         finalNormal.z * finalNormal.z);
        if (len > 0.0f)
            finalNormal = finalNormal / len;

        m_skinnedVertices[i].x = finalPos.x;
        m_skinnedVertices[i].y = finalPos.y;
        m_skinnedVertices[i].z = finalPos.z;
        m_skinnedVertices[i].nx = finalNormal.x;
        m_skinnedVertices[i].ny = finalNormal.y;
        m_skinnedVertices[i].nz = finalNormal.z;
        m_skinnedVertices[i].u = original.u;
        m_skinnedVertices[i].v = original.v;
    }

    vb->SetData(m_skinnedVertices.data());
    buffer->Build();
}

void MeshBuffer::Transform(const Mat4 &matrix)
{
    // Extrai a matriz 3x3 para transformar normais
    Mat3 normalMatrix;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            normalMatrix(i, j) = matrix(i, j);

    normalMatrix = normalMatrix.inverse().transposed();

    for (auto &v : vertices)
    {

        Vec3 pos(v.x, v.y, v.z);
        pos = matrix * pos;
        v.x = pos.x;
        v.y = pos.y;
        v.z = pos.z;

        Vec3 normal(v.nx, v.ny, v.nz);
        normal = normalMatrix * normal;
        normal = normal.normalized();
        v.nx = normal.x;
        v.ny = normal.y;
        v.nz = normal.z;
    }

    m_vdirty = true;
}

void MeshBuffer::TransformPositions(const Mat4 &matrix)
{
    for (auto &v : vertices)
    {
        Vec3 pos(v.x, v.y, v.z);
        pos = matrix * pos;
        v.x = pos.x;
        v.y = pos.y;
        v.z = pos.z;
    }
    m_vdirty = true;
}

void MeshBuffer::TransformNormals(const Mat3 &normalMatrix)
{
    for (auto &v : vertices)
    {
        Vec3 normal(v.nx, v.ny, v.nz);
        normal = normalMatrix * normal;
        normal = normal.normalized();
        v.nx = normal.x;
        v.ny = normal.y;
        v.nz = normal.z;
    }
    m_vdirty = true;
}

void MeshBuffer::Translate(const Vec3 &offset)
{
    Mat4 mat = Mat4::Translation(offset);
    TransformPositions(mat);
}

void MeshBuffer::Rotate(const Quat &rotation)
{
    Mat4 mat = rotation.toMat4();
    Transform(mat);
}

void MeshBuffer::Rotate(const Vec3 &axis, float angle)
{
    Quat quat = Quat::FromAxisAngle(axis, angle);
    Rotate(quat);
}

void MeshBuffer::Scale(const Vec3 &scale)
{
    Mat4 mat = Mat4::Scale(scale);
    Transform(mat);
}

void MeshBuffer::Scale(float uniformScale)
{
    Scale(Vec3(uniformScale, uniformScale, uniformScale));
}

void MeshBuffer::RemoveDuplicateVertices(float threshold)
{
    struct VertexHash
    {
        size_t operator()(const Vertex &v) const
        {

            size_t h1 = std::hash<float>{}(v.x);
            size_t h2 = std::hash<float>{}(v.y);
            size_t h3 = std::hash<float>{}(v.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct VertexEqual
    {
        float threshold;

        VertexEqual(float t) : threshold(t) {}

        bool operator()(const Vertex &a, const Vertex &b) const
        {
            float dx = a.x - b.x;
            float dy = a.y - b.y;
            float dz = a.z - b.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq > threshold * threshold)
                return false;

            float dnx = a.nx - b.nx;
            float dny = a.ny - b.ny;
            float dnz = a.nz - b.nz;
            float normalDistSq = dnx * dnx + dny * dny + dnz * dnz;

            if (normalDistSq > threshold * threshold)
                return false;

            float du = a.u - b.u;
            float dv = a.v - b.v;
            float uvDistSq = du * du + dv * dv;

            return uvDistSq <= threshold * threshold;
        }
    };

    std::vector<Vertex> uniqueVertices;
    std::vector<u32> remapTable(vertices.size());

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const Vertex &v = vertices[i];

        bool found = false;
        for (size_t j = 0; j < uniqueVertices.size(); ++j)
        {
            if (VertexEqual(threshold)(v, uniqueVertices[j]))
            {
                remapTable[i] = (u32)j;
                found = true;
                break;
            }
        }

        if (!found)
        {
            remapTable[i] = (u32)uniqueVertices.size();
            uniqueVertices.push_back(v);
        }
    }

    // Remapeia índices
    for (auto &idx : indices)
    {
        idx = remapTable[idx];
    }

    vertices = std::move(uniqueVertices);

    m_idirty = true;
    m_vdirty = true;
}

void MeshBuffer::Optimize()
{

    const size_t cacheSize = 32;
    std::vector<u32> newIndices;
    std::vector<bool> emitted(indices.size() / 3, false);
    std::vector<int> lastUsed(vertices.size(), -1);

    newIndices.reserve(indices.size());

    int currentTime = 0;

    while (newIndices.size() < indices.size())
    {
        int bestTriangle = -1;
        int bestScore = -1;

        for (size_t i = 0; i < indices.size() / 3; ++i)
        {
            if (emitted[i])
                continue;

            u32 i0 = indices[i * 3 + 0];
            u32 i1 = indices[i * 3 + 1];
            u32 i2 = indices[i * 3 + 2];

            int score = 0;

            if (lastUsed[i0] >= 0 && (currentTime - lastUsed[i0]) < (int)cacheSize)
                score += 10;
            if (lastUsed[i1] >= 0 && (currentTime - lastUsed[i1]) < (int)cacheSize)
                score += 10;
            if (lastUsed[i2] >= 0 && (currentTime - lastUsed[i2]) < (int)cacheSize)
                score += 10;

            if (score > bestScore)
            {
                bestScore = score;
                bestTriangle = (int)i;
            }
        }

        if (bestTriangle < 0)
        {
            for (size_t i = 0; i < indices.size() / 3; ++i)
            {
                if (!emitted[i])
                {
                    bestTriangle = (int)i;
                    break;
                }
            }
        }

        if (bestTriangle >= 0)
        {
            u32 i0 = indices[bestTriangle * 3 + 0];
            u32 i1 = indices[bestTriangle * 3 + 1];
            u32 i2 = indices[bestTriangle * 3 + 2];

            newIndices.push_back(i0);
            newIndices.push_back(i1);
            newIndices.push_back(i2);

            emitted[bestTriangle] = true;

            lastUsed[i0] = currentTime++;
            lastUsed[i1] = currentTime++;
            lastUsed[i2] = currentTime++;
        }
    }

    indices = std::move(newIndices);
}

void MeshBuffer::CalculateNormals(bool smooth)
{
    if (smooth)
    {

        for (auto &v : vertices)
        {
            v.nx = v.ny = v.nz = 0.0f;
        }

        for (size_t i = 0; i < indices.size(); i += 3)
        {
            u32 i0 = indices[i];
            u32 i1 = indices[i + 1];
            u32 i2 = indices[i + 2];

            Vec3 v0(vertices[i0].x, vertices[i0].y, vertices[i0].z);
            Vec3 v1(vertices[i1].x, vertices[i1].y, vertices[i1].z);
            Vec3 v2(vertices[i2].x, vertices[i2].y, vertices[i2].z);

            Vec3 edge1 = v1 - v0;
            Vec3 edge2 = v2 - v0;
            Vec3 normal = edge1.cross(edge2);

            vertices[i0].nx += normal.x;
            vertices[i0].ny += normal.y;
            vertices[i0].nz += normal.z;

            vertices[i1].nx += normal.x;
            vertices[i1].ny += normal.y;
            vertices[i1].nz += normal.z;

            vertices[i2].nx += normal.x;
            vertices[i2].ny += normal.y;
            vertices[i2].nz += normal.z;
        }

        for (auto &v : vertices)
        {
            Vec3 n(v.nx, v.ny, v.nz);
            n = n.normalized();
            v.nx = n.x;
            v.ny = n.y;
            v.nz = n.z;
        }
    }
    else // Flat shading
    {

        std::vector<Vertex> newVertices;
        std::vector<u32> newIndices;

        newVertices.reserve(indices.size());
        newIndices.reserve(indices.size());

        for (size_t i = 0; i < indices.size(); i += 3)
        {
            u32 i0 = indices[i];
            u32 i1 = indices[i + 1];
            u32 i2 = indices[i + 2];

            Vec3 v0(vertices[i0].x, vertices[i0].y, vertices[i0].z);
            Vec3 v1(vertices[i1].x, vertices[i1].y, vertices[i1].z);
            Vec3 v2(vertices[i2].x, vertices[i2].y, vertices[i2].z);

            Vec3 edge1 = v1 - v0;
            Vec3 edge2 = v2 - v0;
            Vec3 normal = edge1.cross(edge2).normalized();

            Vertex nv0 = vertices[i0];
            Vertex nv1 = vertices[i1];
            Vertex nv2 = vertices[i2];

            nv0.nx = nv1.nx = nv2.nx = normal.x;
            nv0.ny = nv1.ny = nv2.ny = normal.y;
            nv0.nz = nv1.nz = nv2.nz = normal.z;

            u32 newIdx = (u32)newVertices.size();
            newVertices.push_back(nv0);
            newVertices.push_back(nv1);
            newVertices.push_back(nv2);

            newIndices.push_back(newIdx);
            newIndices.push_back(newIdx + 1);
            newIndices.push_back(newIdx + 2);
        }

        vertices = std::move(newVertices);
        indices = std::move(newIndices);
    }
    m_vdirty = true;
    m_idirty = true;
}

void MeshBuffer::CalculateBoundingBox()
{
    m_boundBox.clear();
    for (const auto &v : vertices)
    {
        m_boundBox.expand(Vec3(v.x, v.y, v.z));
    }
}

void MeshBuffer::Reverse()
{
    // Inverte a ordem de winding de todos os triângulos
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        std::swap(indices[i + 1], indices[i + 2]);
    }

    m_idirty = true;
}

void MeshBuffer::FlipNormals()
{
    for (auto &v : vertices)
    {
        v.nx = -v.nx;
        v.ny = -v.ny;
        v.nz = -v.nz;
    }
    m_vdirty = true;
}

Vec3 MeshBuffer::Center()
{
    if (vertices.empty())
        return Vec3(0, 0, 0);

    Vec3 sum(0, 0, 0);
    for (const auto &v : vertices)
    {
        sum.x += v.x;
        sum.y += v.y;
        sum.z += v.z;
    }
    m_vdirty = true;
    return sum / (float)vertices.size();
}

void MeshBuffer::Merge(const MeshBuffer &other)
{
    u32 indexOffset = (u32)vertices.size();

    // Adiciona vértices
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());

    // Adiciona índices com offset
    for (u32 idx : other.indices)
    {
        indices.push_back(idx + indexOffset);
    }
    m_vdirty = true;
}

Vec3 MeshBuffer::GetVertexPosition(u32 index) const
{
    if (index >= vertices.size())
        return Vec3(0, 0, 0);
    const Vertex &v = vertices[index];
    return Vec3(v.x, v.y, v.z);
}

void MeshBuffer::SetVertexPosition(u32 index, const Vec3 &position)
{
    if (index >= vertices.size())
        return;
    Vertex &v = vertices[index];
    v.x = position.x;
    v.y = position.y;
    v.z = position.z;
    m_vdirty = true;
}

Vertex &MeshBuffer::GetVertex(u32 index)
{
    if (index >= vertices.size())
        return vertices[0];
    return vertices[index];
}

Vertex MeshBuffer::GetVertex(u32 index) const
{
    if (index >= vertices.size())
        return vertices[0];
    return vertices[index];
}

void MeshBuffer::SetVertex(u32 index, const Vertex &vertex)
{
    if (index >= vertices.size())
        return;
    vertices[index] = vertex;
    m_vdirty = true;
}

void MeshBuffer::GeneratePlanarUVs(const Vec3 &axis)
{
    Vec3 axisNorm = axis.normalized();

    // Encontra dois vetores perpendiculares ao eixo
    Vec3 tangent, bitangent;

    if (std::abs(axisNorm.y) < 0.999f)
    {
        tangent = Vec3(0, 1, 0).cross(axisNorm).normalized();
    }
    else
    {
        tangent = Vec3(1, 0, 0).cross(axisNorm).normalized();
    }

    bitangent = axisNorm.cross(tangent);

    // Calcula bounding box para normalizar UVs
    BoundingBox bbox;
    for (const auto &v : vertices)
    {
        bbox.expand(Vec3(v.x, v.y, v.z));
    }

    Vec3 size = bbox.size();
    float maxSize = std::max(std::max(size.x, size.y), size.z);

    for (auto &v : vertices)
    {
        Vec3 pos(v.x, v.y, v.z);
        Vec3 relative = pos - bbox.center();

        v.u = relative.dot(tangent) / maxSize + 0.5f;
        v.v = relative.dot(bitangent) / maxSize + 0.5f;
    }
    m_vdirty = true;
}

void MeshBuffer::GenerateBoxUVs()
{
    for (auto &v : vertices)
    {
        Vec3 pos(v.x, v.y, v.z);
        Vec3 normal(v.nx, v.ny, v.nz);
        Vec3 absNormal(std::abs(normal.x), std::abs(normal.y), std::abs(normal.z));

        // Determina a face dominante
        if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z)
        {
            // Face X
            v.u = pos.z;
            v.v = pos.y;
        }
        else if (absNormal.y >= absNormal.x && absNormal.y >= absNormal.z)
        {
            // Face Y
            v.u = pos.x;
            v.v = pos.z;
        }
        else
        {
            // Face Z
            v.u = pos.x;
            v.v = pos.y;
        }

        // Normaliza para [0, 1]
        v.u = v.u * 0.5f + 0.5f;
        v.v = v.v * 0.5f + 0.5f;
    }
    m_vdirty = true;
}

void MeshBuffer::GenerateSphericalUVs()
{
    const float PI = 3.14159265358979323846f;

    for (auto &v : vertices)
    {
        Vec3 pos(v.x, v.y, v.z);
        Vec3 normalized = pos.normalized();

        // Coordenadas esféricas
        float theta = std::atan2(normalized.z, normalized.x);
        float phi = std::asin(normalized.y);

        v.u = (theta / (2.0f * PI)) + 0.5f;
        v.v = (phi / PI) + 0.5f;
    }
    m_vdirty = true;
}

void MeshBuffer::ScaleUVs(const Vec2 &scale)
{
    for (auto &v : vertices)
    {
        v.u *= scale.x;
        v.v *= scale.y;
    }
    m_vdirty = true;
}

void MeshBuffer::OffsetUVs(const Vec2 &offset)
{
    for (auto &v : vertices)
    {
        v.u += offset.x;
        v.v += offset.y;
    }
    m_vdirty = true;
}

void MeshBuffer::GeneratePlanarUVsAuto(float resolution)
{
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        u32 idx0 = indices[i + 0];
        u32 idx1 = indices[i + 1];
        u32 idx2 = indices[i + 2];

        Vec3 p0(vertices[idx0].x, vertices[idx0].y, vertices[idx0].z);
        Vec3 p1(vertices[idx1].x, vertices[idx1].y, vertices[idx1].z);
        Vec3 p2(vertices[idx2].x, vertices[idx2].y, vertices[idx2].z);

        // Calcula normal MANUALMENTE para garantir normalização
        Vec3 edge1 = p1 - p0;
        Vec3 edge2 = p2 - p0;
        Vec3 normal = edge1.cross(edge2);

        // FORÇA normalização
        normal = normal.normalized();

        // Pega absoluto
        float absX = fabs(normal.x);
        float absY = fabs(normal.y);
        float absZ = fabs(normal.z);

        // Aplica UV mapping
        if (absX > absY && absX > absZ)
        {
            for (u32 j = 0; j < 3; ++j)
            {
                u32 idx = indices[i + j];
                vertices[idx].u = vertices[idx].y * resolution;
                vertices[idx].v = vertices[idx].z * resolution;
            }
        }
        else if (absY > absX && absY > absZ)
        {
            for (u32 j = 0; j < 3; ++j)
            {
                u32 idx = indices[i + j];
                vertices[idx].u = vertices[idx].x * resolution;
                vertices[idx].v = vertices[idx].z * resolution;
            }
        }
        else
        {
            for (u32 j = 0; j < 3; ++j)
            {
                u32 idx = indices[i + j];
                vertices[idx].u = vertices[idx].x * resolution;
                vertices[idx].v = vertices[idx].y * resolution;
            }
        }
    }

    m_vdirty = true;
}

void MeshBuffer::GeneratePlanarUVsAxis(float resolutionS, float resolutionT,
                                       int axis, const Vec3 &offset)
{
    // axis: 0=X, 1=Y, 2=Z

    for (auto &v : vertices)
    {
        if (axis == 0) // Eixo X - projeta no plano YZ
        {
            v.u = 0.5f + (v.z + offset.z) * resolutionS;
            v.v = 0.5f - (v.y + offset.y) * resolutionT;
        }
        else if (axis == 1) // Eixo Y - projeta no plano XZ
        {
            v.u = 0.5f + (v.x + offset.x) * resolutionS;
            v.v = 1.0f - (v.z + offset.z) * resolutionT;
        }
        else if (axis == 2) // Eixo Z - projeta no plano XY
        {
            v.u = 0.5f + (v.x + offset.x) * resolutionS;
            v.v = 0.5f - (v.y + offset.y) * resolutionT;
        }
    }
}

void MeshBuffer::GeneratePlanarUVsAdvanced(float resolutionS, float resolutionT,
                                           const Vec3 &offset, bool autoDetect)
{
    if (autoDetect)
    {
        // Detecta automaticamente o eixo por triângulo
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            u32 i0 = indices[i + 0];
            u32 i1 = indices[i + 1];
            u32 i2 = indices[i + 2];

            Vec3 p0(vertices[i0].x, vertices[i0].y, vertices[i0].z);
            Vec3 p1(vertices[i1].x, vertices[i1].y, vertices[i1].z);
            Vec3 p2(vertices[i2].x, vertices[i2].y, vertices[i2].z);

            Vec3 edge1 = p1 - p0;
            Vec3 edge2 = p2 - p0;
            Vec3 normal = edge1.cross(edge2).normalized();
            Vec3 absNormal(std::abs(normal.x), std::abs(normal.y), std::abs(normal.z));

            if (absNormal.x > absNormal.y && absNormal.x > absNormal.z)
            {
                // Projeção YZ
                for (u32 j = 0; j < 3; ++j)
                {
                    u32 idx = indices[i + j];
                    vertices[idx].u = 0.5f + (vertices[idx].z + offset.z) * resolutionS;
                    vertices[idx].v = 0.5f - (vertices[idx].y + offset.y) * resolutionT;
                }
            }
            else if (absNormal.y > absNormal.x && absNormal.y > absNormal.z)
            {
                // Projeção XZ
                for (u32 j = 0; j < 3; ++j)
                {
                    u32 idx = indices[i + j];
                    vertices[idx].u = 0.5f + (vertices[idx].x + offset.x) * resolutionS;
                    vertices[idx].v = 1.0f - (vertices[idx].z + offset.z) * resolutionT;
                }
            }
            else
            {
                // Projeção XY
                for (u32 j = 0; j < 3; ++j)
                {
                    u32 idx = indices[i + j];
                    vertices[idx].u = 0.5f + (vertices[idx].x + offset.x) * resolutionS;
                    vertices[idx].v = 0.5f - (vertices[idx].y + offset.y) * resolutionT;
                }
            }
        }
    }
    else
    {
        // Usa a normal média da mesh para escolher um eixo único
        Vec3 avgNormal(0, 0, 0);

        for (size_t i = 0; i < indices.size(); i += 3)
        {
            u32 i0 = indices[i + 0];
            u32 i1 = indices[i + 1];
            u32 i2 = indices[i + 2];

            Vec3 p0(vertices[i0].x, vertices[i0].y, vertices[i0].z);
            Vec3 p1(vertices[i1].x, vertices[i1].y, vertices[i1].z);
            Vec3 p2(vertices[i2].x, vertices[i2].y, vertices[i2].z);

            Vec3 edge1 = p1 - p0;
            Vec3 edge2 = p2 - p0;
            Vec3 normal = edge1.cross(edge2);

            avgNormal = avgNormal + normal;
        }

        avgNormal = avgNormal.normalized();
        Vec3 absNormal(std::abs(avgNormal.x), std::abs(avgNormal.y), std::abs(avgNormal.z));

        // Aplica projeção uniforme baseada no eixo dominante
        int axis = 2; // Z por padrão
        if (absNormal.x > absNormal.y && absNormal.x > absNormal.z)
            axis = 0;
        else if (absNormal.y > absNormal.x && absNormal.y > absNormal.z)
            axis = 1;

        GeneratePlanarUVsAxis(resolutionS, resolutionT, axis, offset);
    }
}

MeshBuffer *Mesh::AddBuffer(u32 material)
{
    MeshBuffer *buffer = new MeshBuffer();
    buffer->m_material = material;
    buffers.push_back(buffer);
    return buffer;
}

bool Mesh::AppendBuffer(MeshBuffer *buffer)
{
    if (!buffer)
    {
        return false;
    }
    buffers.push_back(buffer);
    return true;
}

bool Mesh::AppendMaterial(Material *material)
{
    if (!material)
    {
        return false;
    }
    materials.push_back(material);
    return true;
}

void Mesh::Render()
{
    for (MeshBuffer *buffer : buffers)
    {

        const int materialID = buffer->m_material;
        if (materialID >= 0 && materialID < (int)materials.size())
        {
            const u8 layer = materials[materialID]->GetLayers();
            for (u8 i = 0; i < layer; i++)
            {
                const Texture *texture = materials[materialID]->GetTexture(i);
                if (texture)
                {
                    texture->Bind(i);
                }
            }
            buffer->Render();
        }
        else
        {
            buffer->Render();
        }
    }
}

void Mesh::CalculateNormals()
{
    for (MeshBuffer *buffer : buffers)
    {
        buffer->CalculateNormals(true);
    }
}

MeshBuffer *Mesh::DropBuffer(u32 index)
{
    if (index >= buffers.size())
    {
        LogWarning("[Mesh] Invalid mesh buffer index: %d", index);
        return nullptr;
    }
    MeshBuffer *buffer = buffers[index];
    buffers.erase(buffers.begin() + index);
    return buffer;
}

Material *Mesh::DropMaterial(u32 index)
{
    if (index >= materials.size())
    {
        LogWarning("[Mesh] Invalid material index: %d", index);
        return nullptr;
    }
    Material *material = materials[index];
    materials.erase(materials.begin() + index);
    return material;
}

bool Mesh::RemoveBuffer(u32 index)
{
    if (index >= buffers.size())
    {
        LogWarning("[Mesh] Invalid mesh buffer index: %d", index);
        return false;
    }
    MeshBuffer *buffer = buffers[index];
    buffers.erase(buffers.begin() + index);

    delete buffer;
    return true;
}

bool Mesh::RemoveMaterial(u32 index)
{
    if (index >= materials.size())
    {
        LogWarning("[Mesh] Invalid material index: %d", index);
        return false;
    }
    Material *material = materials[index];
    materials.erase(materials.begin() + index);
    delete material;
    return true;
}

void Mesh::RemoveMaterials()
{
    for (Material *material : materials)
    {
        delete material;
    }
    materials.clear();
}

Mesh::Mesh(const std::string &name) : Visual(name)
{
}

Mesh::~Mesh()
{
    for (MeshBuffer *buffer : buffers)
    {
        delete buffer;
    }
    buffers.clear();

    m_bones.clear();
}

bool Mesh::SetBufferMaterial(u32 index, u32 material)
{

    if (material >= materials.size())
    {
        LogWarning("[Mesh] Invalid material index: %d", material);
        return false;
    }

    if (index >= buffers.size())
    {
        LogWarning("[Mesh] Invalid mesh buffer index: %d", index);
        return false;
    }

    buffers[index]->SetMaterial(material);
    return true;
}

bool Mesh::SetMaterial(u32 material)
{
    if (material >= materials.size())
    {
        LogWarning("[Mesh] Invalid material index: %d", material);
        return false;
    }
    for (MeshBuffer *buffer : buffers)
    {
        buffer->SetMaterial(material);
    }
    return true;
}

void Mesh::OptimizeBuffers()
{
    if (buffers.empty())
        return;

    SortByMaterial();

    std::map<u32, std::vector<MeshBuffer *>> buffersByMaterial;
    for (auto *buffer : buffers)
    {
        buffersByMaterial[buffer->m_material].push_back(buffer);
    }

    std::vector<MeshBuffer *> newBuffers;
    for (auto &[materialId, materialBuffers] : buffersByMaterial)
    {
        if (materialBuffers.size() == 1)
        {
            newBuffers.push_back(materialBuffers[0]);
            continue;
        }

        MeshBuffer *combined = new MeshBuffer();
        combined->m_material = materialId;
        for (auto *buffer : materialBuffers)
        {
            u32 vertexOffset = combined->vertices.size();

            combined->vertices.insert(
                combined->vertices.end(),
                buffer->vertices.begin(),
                buffer->vertices.end());

            for (u32 index : buffer->indices)
            {
                combined->indices.push_back(index + vertexOffset);
            }

            delete buffer;
        }

        combined->m_vdirty = true;
        combined->m_idirty = true;
        newBuffers.push_back(combined);
    }

    buffers.clear();

    buffers = std::move(newBuffers);

    SortByMaterial();
}

const Bone &Mesh::GetBone(u32 index) const
{
    DEBUG_BREAK_IF(index >= m_bones.size());
    return m_bones[index];
}

Bone *Mesh::AddBone(const std::string &name)
{
    Bone bone(name);
    m_bones.push_back(bone);
    return &m_bones.back();
}

void Mesh::CalculateBoundingBox()
{
    if (buffers.empty())
        return;
    m_boundBox.clear();
    for (MeshBuffer *buffer : buffers)
    {
        buffer->CalculateBoundingBox();
        m_boundBox.expand(buffer->GetBoundingBox());
    }
}

void Mesh::Debug(RenderBatch *batch)
{

    if (buffers.empty())
        return;
    for (MeshBuffer *buffer : buffers)
    {
        buffer->Debug(batch);
    }
}

Bone *Mesh::FindBone(const std::string &name)
{
    for (u32 i = 0; i < m_bones.size(); i++)
    {
        if (m_bones[i].name == name)
            return &m_bones[i];
    }
    LogWarning("Bone %s not found", name.c_str());
    return nullptr;
}

void PrintMatrix(const Mat4 &mat)
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            std::cout << mat(i, j) << " ";
        }
        std::cout << std::endl;
    }
}

Bone::Bone(const std::string &name)
{

    this->name = name;
    hasAnimation = false;
    parentIndex = -1;
}

int Mesh::FindBoneIndex(const std::string &name)
{
    for (u32 i = 0; i < m_bones.size(); i++)
    {
        if (m_bones[i].name == name)
            return i;
    }
    return -1;
}

void Mesh::SortByMaterial()
{
    std::sort(buffers.begin(), buffers.end(),
              [](const MeshBuffer *a, const MeshBuffer *b)
              {
                  return a->GetMaterial() < b->GetMaterial();
              });
}

void Mesh::Clear()
{
    for (MeshBuffer *buffer : buffers)
    {
        delete buffer;
    }
    buffers.clear();
}

void Mesh::Build()
{
    for (MeshBuffer *buffer : buffers)
    {
        buffer->Build();
    }
}

bool MeshLoader::CanLoad(const std::string &filename) const
{
    auto extensions = GetExtensions();
    for (const auto &ext : extensions)
    {
        if (HasExtension(filename, ext))
            return true;
    }
    return false;
}

bool MeshLoader::HasExtension(const std::string &filename, const std::string &ext) const
{
    if (filename.length() < ext.length() + 1)
        return false;

    size_t pos = filename.rfind('.');
    if (pos == std::string::npos)
        return false;

    std::string fileExt = filename.substr(pos + 1);

    // Comparação case-insensitive
    return std::equal(fileExt.begin(), fileExt.end(), ext.begin(), ext.end(),
                      [](char a, char b)
                      { return tolower(a) == tolower(b); });
}

MeshManager &MeshManager::Instance()
{
    static MeshManager instance;
    return instance;
}

Mesh *MeshManager::Get(const std::string &name)
{
    auto it = m_meshes.find(name);
    return (it != m_meshes.end()) ? it->second : nullptr;
}

void MeshManager::Add(const std::string &name, Mesh *mesh)
{
    auto it = m_meshes.find(name);
    if (it != m_meshes.end())
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return;
    }
    m_meshes[name] = mesh;
}

bool MeshManager::Exists(const std::string &name) const
{
    return m_meshes.find(name) != m_meshes.end();
}

Mesh *MeshManager::Create(const std::string &name)
{
    auto it = m_meshes.find(name);
    if (it != m_meshes.end())
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return it->second;
    }
    Mesh *mesh = new Mesh();
    mesh->m_boundBox.clear();
    m_meshes[name] = mesh;
    return mesh;
}

Mesh *MeshManager::CreateCube(const std::string &name, float size)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }
    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    float h = size * 0.5f;

    // Frente (Z+) - olhando para +Z
    u32 v3 = buffer->AddVertex(-h, -h, h, 0, 0, 1, 0, 0);
    u32 v2 = buffer->AddVertex(h, -h, h, 0, 0, 1, 1, 0);
    u32 v1 = buffer->AddVertex(h, h, h, 0, 0, 1, 1, 1);
    u32 v0 = buffer->AddVertex(-h, h, h, 0, 0, 1, 0, 1);
    buffer->AddFace(v0, v2, v1);
    buffer->AddFace(v0, v3, v2);

    // Trás (Z-) - olhando para -Z
    v3 = buffer->AddVertex(h, -h, -h, 0, 0, -1, 0, 0);
    v2 = buffer->AddVertex(-h, -h, -h, 0, 0, -1, 1, 0);
    v1 = buffer->AddVertex(-h, h, -h, 0, 0, -1, 1, 1);
    v0 = buffer->AddVertex(h, h, -h, 0, 0, -1, 0, 1);
    buffer->AddFace(v0, v2, v1);
    buffer->AddFace(v0, v3, v2);

    // Direita (X+)
    v3 = buffer->AddVertex(h, -h, h, 1, 0, 0, 0, 0);
    v2 = buffer->AddVertex(h, -h, -h, 1, 0, 0, 1, 0);
    v1 = buffer->AddVertex(h, h, -h, 1, 0, 0, 1, 1);
    v0 = buffer->AddVertex(h, h, h, 1, 0, 0, 0, 1);
    buffer->AddFace(v0, v2, v1);
    buffer->AddFace(v0, v3, v2);

    // Esquerda (X-)
    v3 = buffer->AddVertex(-h, -h, -h, -1, 0, 0, 0, 0);
    v2 = buffer->AddVertex(-h, -h, h, -1, 0, 0, 1, 0);
    v1 = buffer->AddVertex(-h, h, h, -1, 0, 0, 1, 1);
    v0 = buffer->AddVertex(-h, h, -h, -1, 0, 0, 0, 1);
    buffer->AddFace(v0, v2, v1);
    buffer->AddFace(v0, v3, v2);

    // Cima (Y+)
    v3 = buffer->AddVertex(-h, h, h, 0, 1, 0, 0, 0);
    v2 = buffer->AddVertex(h, h, h, 0, 1, 0, 1, 0);
    v1 = buffer->AddVertex(h, h, -h, 0, 1, 0, 1, 1);
    v0 = buffer->AddVertex(-h, h, -h, 0, 1, 0, 0, 1);
    buffer->AddFace(v0, v2, v1);
    buffer->AddFace(v0, v3, v2);

    // Baixo (Y-)
    v3 = buffer->AddVertex(-h, -h, -h, 0, -1, 0, 0, 0);
    v2 = buffer->AddVertex(h, -h, -h, 0, -1, 0, 1, 0);
    v1 = buffer->AddVertex(h, -h, h, 0, -1, 0, 1, 1);
    v0 = buffer->AddVertex(-h, -h, h, 0, -1, 0, 0, 1);
    buffer->AddFace(v0, v2, v1);
    buffer->AddFace(v0, v3, v2);

    mesh->m_boundBox.merge(buffer->GetBoundingBox());
    buffer->CalculateNormals();
    //  mesh->CalculateBoundingBox();
    buffer->Build();

    return mesh;
}

Mesh *MeshManager::CreatePlane(const std::string &name, float width, float height,
                               int detailX, int detailY, float tilesH, float tilesV)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }
    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    float hw = width * 0.5f;
    float hh = height * 0.5f;
    buffer->m_boundBox.clear();
    // Gera vértices
    for (int y = 0; y <= detailY; y++)
    {
        for (int x = 0; x <= detailX; x++)
        {
            float u = ((float)x / detailX) * tilesH; // Multiplica por tilesH
            float v = ((float)y / detailY) * tilesV; // Multiplica por tilesV

            float px = -hw + ((float)x / detailX) * width;
            float py = 0.0f;
            float pz = -hh + ((float)y / detailY) * height;

            buffer->AddVertex(px, py, pz, 0.0f, 1.0f, 0.0f, u, v);
        }
    }

    // LogInfo("[MeshManager] Created plane: %s (%f, %f, %f) - (%f, %f, %f)", name.c_str(),buffer->m_boundBox.min.x,buffer->m_boundBox.min.y,buffer->m_boundBox.min.z,buffer->m_boundBox.max.x,buffer->m_boundBox.max.y,buffer->m_boundBox.max.z);
    //  Gera índices (mantém igual)
    for (int y = 0; y < detailY; y++)
    {
        for (int x = 0; x < detailX; x++)
        {
            u32 i0 = y * (detailX + 1) + x;
            u32 i1 = i0 + 1;
            u32 i2 = (y + 1) * (detailX + 1) + x;
            u32 i3 = i2 + 1;

            buffer->AddFace(i0, i2, i1);
            buffer->AddFace(i1, i2, i3);
        }
    }
    mesh->m_boundBox.clear();
    mesh->m_boundBox.merge(buffer->m_boundBox);
    buffer->CalculateNormals();
    buffer->Build();

    return mesh;
}

Mesh *MeshManager::CreateSphere(const std::string &name, float radius, int segments, int rings)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }
    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    const float PI = 3.14159265359f;

    // Gera vértices
    for (int ring = 0; ring <= rings; ring++)
    {
        float theta = ring * PI / rings;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int seg = 0; seg <= segments; seg++)
        {
            float phi = seg * 2.0f * PI / segments;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            float x = radius * sinTheta * cosPhi;
            float y = radius * cosTheta;
            float z = radius * sinTheta * sinPhi;

            // Normal é a posição normalizada
            float nx = sinTheta * cosPhi;
            float ny = cosTheta;
            float nz = sinTheta * sinPhi;

            float u = (float)seg / segments;
            float v = (float)ring / rings;

            buffer->AddVertex(x, y, z, nx, ny, nz, u, v);
        }
    }

    // Gera índices
    for (int ring = 0; ring < rings; ring++)
    {
        for (int seg = 0; seg < segments; seg++)
        {
            u32 i0 = ring * (segments + 1) + seg;
            u32 i1 = i0 + 1;
            u32 i2 = (ring + 1) * (segments + 1) + seg;
            u32 i3 = i2 + 1;

            buffer->AddFace(i0, i2, i1);
            buffer->AddFace(i1, i2, i3);
        }
    }
    mesh->m_boundBox.clear();
    mesh->m_boundBox.merge(buffer->m_boundBox);
    buffer->CalculateNormals();
    buffer->Build();

    return mesh;
}

Mesh *MeshManager::CreateCylinder(const std::string &name, float radius, float height, int segments, bool caps)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }
    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    const float PI = 3.14159265359f;
    float halfHeight = height * 0.5f;

    // Corpo do cilindro
    for (int i = 0; i <= segments; i++)
    {
        float angle = (float)i * 2.0f * PI / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        float u = (float)i / segments;

        // Normal lateral
        float nx = std::cos(angle);
        float nz = std::sin(angle);

        // Vértice inferior
        buffer->AddVertex(x, -halfHeight, z, nx, 0, nz, u, 0);
        // Vértice superior
        buffer->AddVertex(x, halfHeight, z, nx, 0, nz, u, 1);
    }

    // Índices do corpo
    for (int i = 0; i < segments; i++)
    {
        u32 i0 = i * 2;
        u32 i1 = i0 + 1;
        u32 i2 = (i + 1) * 2;
        u32 i3 = i2 + 1;

        buffer->AddFace(i0, i2, i1);
        buffer->AddFace(i1, i2, i3);
    }

    if (caps)
    {
        u32 baseStart = buffer->vertices.size();

        // Tampa inferior (centro)
        buffer->AddVertex(0, -halfHeight, 0, 0, -1, 0, 0.5f, 0.5f);

        for (int i = 0; i <= segments; i++)
        {
            float angle = (float)i * 2.0f * PI / segments;
            float x = radius * std::cos(angle);
            float z = radius * std::sin(angle);
            float u = 0.5f + 0.5f * std::cos(angle);
            float v = 0.5f + 0.5f * std::sin(angle);

            buffer->AddVertex(x, -halfHeight, z, 0, -1, 0, u, v);
        }

        // Índices tampa inferior
        for (int i = 0; i < segments; i++)
        {
            buffer->AddFace(baseStart, baseStart + i + 2, baseStart + i + 1);
        }

        // Tampa superior (centro)
        u32 topStart = buffer->vertices.size();
        buffer->AddVertex(0, halfHeight, 0, 0, 1, 0, 0.5f, 0.5f);

        for (int i = 0; i <= segments; i++)
        {
            float angle = (float)i * 2.0f * PI / segments;
            float x = radius * std::cos(angle);
            float z = radius * std::sin(angle);
            float u = 0.5f + 0.5f * std::cos(angle);
            float v = 0.5f + 0.5f * std::sin(angle);

            buffer->AddVertex(x, halfHeight, z, 0, 1, 0, u, v);
        }

        // Índices tampa superior
        for (int i = 0; i < segments; i++)
        {
            buffer->AddFace(topStart, topStart + i + 1, topStart + i + 2);
        }
    }

    buffer->CalculateNormals();
    buffer->Build();
    return mesh;
}

Mesh *MeshManager::CreateCone(const std::string &name, float radius, float height, int segments)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }
    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    const float PI = 3.14159265359f;
    float halfHeight = height * 0.5f;

    // Vértice do topo
    u32 topVertex = buffer->AddVertex(0, halfHeight, 0, 0, 1, 0, 0.5f, 1.0f);

    // Vértices da base
    for (int i = 0; i <= segments; i++)
    {
        float angle = (float)i * 2.0f * PI / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);

        // Calcula normal da lateral (aproximada)
        float slant = std::sqrt(radius * radius + height * height);
        float nx = (x / radius) * (height / slant);
        float ny = radius / slant;
        float nz = (z / radius) * (height / slant);

        buffer->AddVertex(x, -halfHeight, z, nx, ny, nz, (float)i / segments, 0);
    }

    // Faces laterais
    for (int i = 0; i < segments; i++)
    {
        buffer->AddFace(topVertex, i + 1, i + 2);
    }

    // Tampa da base (centro)
    u32 baseCenter = buffer->AddVertex(0, -halfHeight, 0, 0, -1, 0, 0.5f, 0.5f);

    for (int i = 0; i <= segments; i++)
    {
        float angle = (float)i * 2.0f * PI / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        float u = 0.5f + 0.5f * std::cos(angle);
        float v = 0.5f + 0.5f * std::sin(angle);

        buffer->AddVertex(x, -halfHeight, z, 0, -1, 0, u, v);
    }

    // Índices da base
    u32 baseStart = baseCenter + 1;
    for (int i = 0; i < segments; i++)
    {
        buffer->AddFace(baseCenter, baseStart + i + 1, baseStart + i);
    }

    buffer->CalculateNormals();
    buffer->Build();

    return mesh;
}

/**
 * Creates a simple quad (2 triangles) oriented according to the face normal
 *
 * @param name Unique name for the mesh
 * @param face Normal direction (e.g., Vec3(0,1,0) for floor, Vec3(1,0,0) for side wall)
 * @param size Size of the quad (default: 1.0)
 * @param tilesU Texture tiling in U direction (default: 1.0)
 * @param tilesV Texture tiling in V direction (default: 1.0)
 *
 * Face examples:
 *   Vec3( 0,  1,  0) = Floor   (facing up)
 *   Vec3( 0, -1,  0) = Ceiling (facing down)
 *   Vec3( 1,  0,  0) = Wall right (facing +X)
 *   Vec3(-1,  0,  0) = Wall left  (facing -X)
 *   Vec3( 0,  0,  1) = Wall front (facing +Z)
 *   Vec3( 0,  0, -1) = Wall back  (facing -Z)
 */
Mesh *MeshManager::CreateQuad(const std::string &name, const Vec3 &face,
                              float size, float tilesU, float tilesV)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }

    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    // Normalize the face vector to ensure it's a unit vector
    Vec3 normal = face.normalized();

    // Calculate tangent and bitangent vectors to form a coordinate system
    Vec3 tangent, bitangent;

    // Choose an arbitrary vector that's not parallel to the normal
    Vec3 up = Vec3(0.0f, 1.0f, 0.0f);
    if (fabs(normal.y) > 0.99f)      // If normal is nearly vertical
        up = Vec3(1.0f, 0.0f, 0.0f); // Use right vector instead

    // Calculate tangent (perpendicular to normal)
    tangent = up.cross(normal).normalized();

    // Calculate bitangent (perpendicular to both normal and tangent)
    bitangent = normal.cross(tangent).normalized();

    // Half size
    float hs = size * 0.5f;

    // Define quad corners in local space (centered at origin)
    // Using tangent and bitangent to orient the quad correctly
    Vec3 v0 = (-tangent - bitangent) * hs; // Bottom-left
    Vec3 v1 = (tangent - bitangent) * hs;  // Bottom-right
    Vec3 v2 = (tangent + bitangent) * hs;  // Top-right
    Vec3 v3 = (-tangent + bitangent) * hs; // Top-left

    // Add vertices with proper normals and UVs
    // Order: position (x,y,z), normal (nx,ny,nz), texcoord (u,v)

    // Vertex 0: Bottom-left
    buffer->AddVertex(v0.x, v0.y, v0.z,
                      normal.x, normal.y, normal.z,
                      tilesU, 0.0f);

    // Vertex 1: Bottom-right
    buffer->AddVertex(v1.x, v1.y, v1.z,
                      normal.x, normal.y, normal.z,
                      0, 0.0f);

    // Vertex 2: Top-right
    buffer->AddVertex(v2.x, v2.y, v2.z,
                      normal.x, normal.y, normal.z,
                      0, tilesV);

    // Vertex 3: Top-left
    buffer->AddVertex(v3.x, v3.y, v3.z,
                      normal.x, normal.y, normal.z,
                      tilesU, tilesV);

    // Add two triangles (counter-clockwise winding)
    buffer->AddFace(0, 1, 2); // First triangle
    buffer->AddFace(0, 2, 3); // Second triangle

    buffer->Build();
    mesh->m_boundBox.merge(buffer->m_boundBox);

    LogInfo("[MeshManager] Created quad '%s' facing (%g, %g, %g)",
            name.c_str(), normal.x, normal.y, normal.z);

    return mesh;
}

Mesh *MeshManager::CreateHillPlane(
    const std::string &name,
    float width, float height,
    int segmentsX, int segmentsY,
    float hillHeight,
    float hillCountX,
    float hillCountY,
    float tilesU,
    float tilesV)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }

    // Criar mesh e buffer
    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    // Calcular centro
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    Vec3 center(halfWidth, 0.0f, halfHeight);

    // Garantir que hillCount não seja zero
    if (hillCountX < 0.01f)
        hillCountX = 1.0f;
    if (hillCountY < 0.01f)
        hillCountY = 1.0f;

    // Texture coordinate step
    float txStep = tilesU / (float)segmentsX;
    float tyStep = tilesV / (float)segmentsY;

    // Adicionar 1 para ter o número correto de tiles
    int vertCountX = segmentsX + 1;
    int vertCountY = segmentsY + 1;

    buffer->m_boundBox.clear();

    float sx = 0.0f;
    float tsx = 0.0f;

    for (int x = 0; x < vertCountX; ++x)
    {
        float sy = 0.0f;
        float tsy = 0.0f;

        for (int y = 0; y < vertCountY; ++y)
        {
            // Posição base
            float px = sx - center.x;
            float py = 0.0f;
            float pz = sy - center.z;

            if (hillHeight != 0.0f)
            {
                py = sinf(px * hillCountX * Pi / center.x) *
                     cosf(pz * hillCountY * Pi / center.z) *
                     hillHeight;
            }

            // UV coordinates
            float u = tsx;
            float v = 1.0f - tsy; // Flip V

            // Adicionar vértice
            buffer->AddVertex(px, py, pz, 0.0f, 1.0f, 0.0f, u, v);

            sy += height / segmentsY;
            tsy += tyStep;
        }

        sx += width / segmentsX;
        tsx += txStep;
    }

    for (int x = 0; x < segmentsX; ++x)
    {
        for (int y = 0; y < segmentsY; ++y)
        {
            int current = x * vertCountY + y;
            int next = current + vertCountY;

            // Triângulo 1
            buffer->AddFace(current, current + 1, next);

            // Triângulo 2
            buffer->AddFace(current + 1, next + 1, next);
        }
    }

    buffer->CalculateNormals();

    mesh->m_boundBox.clear();
    mesh->m_boundBox.merge(buffer->m_boundBox);
    buffer->Build();

    LogInfo("[MeshManager] Created hill plane: %s (%dx%d, height=%.2f)",
            name.c_str(), segmentsX, segmentsY, hillHeight);

    return mesh;
}

// MeshManager.cpp

Mesh *MeshManager::CreateTerrainFromHeightmap(
    const std::string &name,
    const std::string &heightmapPath,
    float width, float height, float maxHeight,
    int detailX, int detailY,
    float tilesU, float tilesV)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }

    Pixmap heightmap;
    if (!heightmap.Load(heightmapPath.c_str()))
    {
        LogError("[MeshManager] Failed to load heightmap: %s", heightmapPath.c_str());
        return nullptr;
    }

    LogInfo("[MeshManager] Loaded heightmap: %s (%dx%d, %d components)",
            heightmapPath.c_str(),
            heightmap.width,
            heightmap.height,
            heightmap.components);

    Pixmap *grayscale = nullptr;
    const Pixmap *sourceMap = &heightmap;

    if (heightmap.components > 1)
    {
        // Se for RGB/RGBA, converte para grayscale
        grayscale = new Pixmap(heightmap.width, heightmap.height, 1);

        for (int y = 0; y < heightmap.height; y++)
        {
            for (int x = 0; x < heightmap.width; x++)
            {
                Color pixel = heightmap.GetPixelColor(x, y);

                u8 gray = (u8)((pixel.r + pixel.g + pixel.b) / 3);
                grayscale->SetPixel(x, y, gray, gray, gray, 255);
            }
        }

        sourceMap = grayscale;
        LogInfo("[MeshManager] Converted heightmap to grayscale");
    }

    // Criar terrain
    Mesh *terrain = CreateTerrainFromPixmap(
        name,
        sourceMap,
        width, height, maxHeight,
        detailX, detailY,
        tilesU, tilesV);

    // Limpar grayscale se foi criado
    if (grayscale)
        delete grayscale;

    return terrain;
}

Mesh *MeshManager::CreateTerrainFromPixmap(
    const std::string &name,
    const Pixmap *heightmap,
    float width, float height, float maxHeight,
    int detailX, int detailY,
    float tilesU, float tilesV)
{
    // Criar mesh e buffer
    Mesh *mesh = Create(name);
    MeshBuffer *buffer = mesh->AddBuffer(0);

    // Calcular dimensões
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;

    int vertCountX = detailX + 1;
    int vertCountY = detailY + 1;

    buffer->m_boundBox.clear();

    for (int y = 0; y < vertCountY; ++y)
    {
        for (int x = 0; x < vertCountX; ++x)
        {
            // Posição normalizada [0, 1]
            float nx = (float)x / detailX;
            float ny = (float)y / detailY;

            // Posição world space
            float px = -halfWidth + nx * width;
            float pz = -halfHeight + ny * height;

            int imgX = (int)(nx * (heightmap->width - 1));
            int imgY = (int)(ny * (heightmap->height - 1));

            Color pixel = heightmap->GetPixelColor(imgX, imgY);

            // Normalizar altura [0, 255] -> [0, 1]
            float heightValue = pixel.r / 255.0f; // Usar canal R (grayscale)
            float py = heightValue * maxHeight;

            // UV coordinates
            float u = nx * tilesU;
            float v = ny * tilesV;

            // Adicionar vértice
            buffer->AddVertex(px, py, pz, 0.0f, 1.0f, 0.0f, u, v);
        }
    }

    for (int y = 0; y < detailY; ++y)
    {
        for (int x = 0; x < detailX; ++x)
        {
            int i0 = y * vertCountX + x;
            int i1 = i0 + 1;
            int i2 = (y + 1) * vertCountX + x;
            int i3 = i2 + 1;

            // Triângulo 1
            buffer->AddFace(i0, i2, i1);

            // Triângulo 2
            buffer->AddFace(i1, i2, i3);
        }
    }

    buffer->CalculateNormals();

    mesh->m_boundBox.clear();
    mesh->m_boundBox.merge(buffer->m_boundBox);
    buffer->Build();

    LogInfo("[MeshManager] Created terrain: %s (%d verts, %d tris)",
            name.c_str(),
            buffer->GetVertexCount(),
            buffer->GetIndexCount());

    return mesh;
}

void MeshManager::UnloadAll()
{

    for (auto it = m_meshes.begin(); it != m_meshes.end(); it++)
    {
        delete it->second;
    }
    m_meshes.clear();
    for (auto it = m_m3d_meshes.begin(); it != m_m3d_meshes.end(); it++)
    {
        delete it->second;
    }
    m_m3d_meshes.clear();
    LogInfo("[MeshManager] Unloaded all meshes");
}

MeshManager::MeshManager()
{
    RegisterImporter(new OBJMeshLoader());
    RegisterImporter(new Loader3DS());
}

MeshManager::~MeshManager()
{
    for (auto *loader : m_loaders)
        delete loader;
    m_loaders.clear();
}

Mesh *MeshManager::Load(const std::string &name, const std::string &filename)
{
    Mesh *mesh = Create(name);
    MeshReader reader;

    if (!reader.Load(filename, mesh))
    {
        return nullptr;
    }
    return mesh;
}

bool MeshManager::Save(const std::string &filename, const Mesh *mesh)
{
    MeshWriter writer;
    return writer.Save(mesh, filename);
}

void MeshManager::RegisterImporter(MeshLoader *loader)
{
    m_loaders.push_back(loader);
}

Mesh *MeshManager::Import(const std::string &name, const std::string &filename)
{
    if (Exists(name))
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return Get(name);
    }

    // Encontra o loader apropriado
    MeshLoader *loader = nullptr;
    for (auto *l : m_loaders)
    {
        if (l->CanLoad(filename))
        {
            loader = l;
            break;
        }
    }

    if (!loader)
    {
        LogError("[MeshManager] No loader found for: %s", filename.c_str());
        return nullptr;
    }

    // Abre o arquivo
    FileStream stream(filename, "rb");
    if (!stream.IsOpen())
    {
        LogError("[MeshManager] Failed to open file: %s", filename.c_str());
        return nullptr;
    }

    // Cria a mesh
    Mesh *mesh = Create(name);

    // Carrega
    if (!loader->Load(&stream, mesh))
    {
        delete mesh;
        m_meshes.erase(name);
        LogError("[MeshManager] Failed to load mesh: %s", filename.c_str());
        return nullptr;
    }

    LogInfo("[MeshManager] Loaded: %s from: %s", name.c_str(), filename.c_str());

    return mesh;
}

Mesh *MeshManager::ImportFromStream(const std::string &name, Stream *stream, const std::string &extension)
{
    if (Exists(name))
        return Get(name);

    MeshLoader *loader = nullptr;
    for (auto *l : m_loaders)
    {
        auto exts = l->GetExtensions();
        for (const auto &ext : exts)
        {
            if (ext == extension)
            {
                loader = l;
                break;
            }
        }
        if (loader)
            break;
    }

    if (!loader)
    {
        LogError("[MeshManager] No loader found for: %s", extension.c_str());
        return nullptr;
    }

    Mesh *mesh = Create(name);
    if (!loader->Load(stream, mesh))
    {
        delete mesh;
        m_meshes.erase(name);
        LogError("[MeshManager] Failed to load mesh: %s", extension.c_str());
        return nullptr;
    }

    LogInfo("[MeshManager] Loaded: %s from stream", name.c_str());

    return mesh;
}

MeshM3D *MeshManager::LoadM3D(const std::string &name, const std::string &filename)
{
    auto it = m_m3d_meshes.find(name);
    if (it != m_m3d_meshes.end())
    {
        LogWarning("[MeshManager] Mesh already exists: %s", name.c_str());
        return it->second;
    }
    MeshM3D *mesh = new MeshM3D(name);
    if (!mesh->Load(filename))
    {
        delete mesh;
        return nullptr;
    }

    m_m3d_meshes[name] = mesh;
    return mesh;
}

MeshM3D *MeshManager::GetM3D(const std::string &name)
{
    auto it = m_m3d_meshes.find(name);
    if (it == m_m3d_meshes.end())
        return nullptr;
    return it->second;
}

//***************************************** */

bool MeshWriter::Save(const Mesh *mesh, const std::string &filename)
{
    FileStream stream(filename, "wb");
    if (!stream.IsOpen())
    {
        LogError("[MeshWriter] Failed to open: %s", filename.c_str());
        return false;
    }

    m_stream = &stream;
    m_stream->SetBigEndian(false);

    // Magic + Version
    m_stream->WriteUInt(MESH_MAGIC);
    m_stream->WriteUInt(MESH_VERSION);

    // Materials
    WriteMaterialsChunk(mesh);

    // Skeleton
    if (mesh->HasSkeleton())
        WriteSkeletonChunk(mesh);

    // Buffers
    for (size_t i = 0; i < mesh->GetBufferCount(); i++)
    {
        if (mesh->GetBuffer(i)->GetIndexCount() == 0 || mesh->GetBuffer(i)->GetVertexCount() == 0)
            continue;
        WriteBufferChunk(mesh->GetBuffer(i));
    }

    LogInfo("[MeshWriter] Saved: %zu buffers, %zu materials, %zu bones",
            mesh->GetBufferCount(), mesh->GetMaterialCount(),
            mesh->HasSkeleton() ? mesh->GetBoneCount() : 0);

    return true;
}

void MeshWriter::BeginChunk(u32 chunkId, long *posOut)
{
    m_stream->WriteUInt(chunkId);
    m_stream->WriteUInt(0); // placeholder
    *posOut = m_stream->Tell();
}

void MeshWriter::EndChunk(long startPos)
{
    long currentPos = m_stream->Tell();
    u32 length = currentPos - startPos;

    m_stream->Seek(startPos - 4, SeekOrigin::Begin);
    m_stream->WriteUInt(length);
    m_stream->Seek(currentPos, SeekOrigin::Begin);
}

void MeshWriter::WriteCString(const std::string &str)
{
    m_stream->Write(str.c_str(), str.length() + 1);
}

void MeshWriter::WriteMaterialsChunk(const Mesh *mesh)
{
    long startPos;
    BeginChunk(CHUNK_MATS, &startPos);

    u32 numMaterials = mesh->GetMaterialCount();
    m_stream->WriteUInt(numMaterials);

    for (u32 i = 0; i < numMaterials; i++)
    {
        Material *mat = mesh->GetMaterial(i);

        WriteCString(mat->GetName());

        Vec3 diffuse = mat->GetDiffuse();
        m_stream->WriteFloat(diffuse.x);
        m_stream->WriteFloat(diffuse.y);
        m_stream->WriteFloat(diffuse.z);

        Vec3 specular = mat->GetSpecular();
        m_stream->WriteFloat(specular.x);
        m_stream->WriteFloat(specular.y);
        m_stream->WriteFloat(specular.z);

        m_stream->WriteFloat(mat->GetShininess());

        // Texturas
        u8 numLayers = mat->GetLayers();
        m_stream->WriteByte(numLayers);

        for (u8 j = 0; j < numLayers; j++)
        {
            Texture *tex = mat->GetTexture(j);
            if (tex)
                WriteCString(tex->GetName());
            else
                m_stream->WriteByte(0);
        }
    }

    EndChunk(startPos);
}

void MeshWriter::WriteSkeletonChunk(const Mesh *mesh)
{
    long startPos;
    BeginChunk(CHUNK_SKEL, &startPos);

    u32 numBones = mesh->GetBoneCount();
    m_stream->WriteUInt(numBones);

    for (u32 i = 0; i < numBones; i++)
    {
        const Bone &bone = mesh->GetBone(i);

        WriteCString(bone.name);
        m_stream->WriteInt(bone.parentIndex);

        // Local transform (16 floats)
        const Mat4 &local = bone.localPose;
        for (int j = 0; j < 16; j++)
            m_stream->WriteFloat(local.m[j]);

        // Inverse bind pose (16 floats)
        const Mat4 &invBind = bone.inverseBindPose;
        for (int j = 0; j < 16; j++)
            m_stream->WriteFloat(invBind.m[j]);
    }

    EndChunk(startPos);
}

void MeshWriter::WriteBufferChunk(const MeshBuffer *buffer)
{
    long startPos;
    BeginChunk(CHUNK_BUFF, &startPos);

    m_stream->WriteUInt(buffer->GetMaterial());

    u32 flags = buffer->IsSkinned() ? BUFFER_FLAG_SKINNED : 0;
    m_stream->WriteUInt(flags);

    WriteVerticesChunk(buffer);
    WriteIndicesChunk(buffer);

    // Skinning
    if (flags & BUFFER_FLAG_SKINNED)
        WriteSkinChunk(buffer);

    EndChunk(startPos);
}

void MeshWriter::WriteVerticesChunk(const MeshBuffer *buffer)
{
    long startPos;
    BeginChunk(CHUNK_VRTS, &startPos);

    u32 numVertices = buffer->GetVertexCount();
    m_stream->WriteUInt(numVertices);

    const Vertex *vertices = buffer->GetVertices();

    for (u32 i = 0; i < numVertices; i++)
    {
        m_stream->WriteFloat(vertices[i].x);
        m_stream->WriteFloat(vertices[i].y);
        m_stream->WriteFloat(vertices[i].z);
        m_stream->WriteFloat(vertices[i].nx);
        m_stream->WriteFloat(vertices[i].ny);
        m_stream->WriteFloat(vertices[i].nz);
        m_stream->WriteFloat(vertices[i].u);
        m_stream->WriteFloat(vertices[i].v);
    }

    EndChunk(startPos);
}

void MeshWriter::WriteIndicesChunk(const MeshBuffer *buffer)
{
    long startPos;
    BeginChunk(CHUNK_IDXS, &startPos);

    u32 numIndices = buffer->GetIndexCount();
    m_stream->WriteUInt(numIndices);

    const u32 *indices = buffer->GetIndices();
    for (u32 i = 0; i < numIndices / 3; i++)
    {
        m_stream->WriteUInt(indices[i * 3]);
        m_stream->WriteUInt(indices[i * 3 + 1]);
        m_stream->WriteUInt(indices[i * 3 + 2]);
    }

    EndChunk(startPos);
}

void MeshWriter::WriteSkinChunk(const MeshBuffer *buffer)
{
    long startPos;
    BeginChunk(CHUNK_SKIN, &startPos);

    u32 numVertices = buffer->GetVertexCount();
    m_stream->WriteUInt(numVertices);

    const VertexSkin *skinData = buffer->GetSkinData();
    for (u32 i = 0; i < numVertices; i++)
    {
        m_stream->WriteByte(skinData[i].boneIDs[0]);
        m_stream->WriteByte(skinData[i].boneIDs[1]);
        m_stream->WriteByte(skinData[i].boneIDs[2]);
        m_stream->WriteByte(skinData[i].boneIDs[3]);

        m_stream->WriteFloat(skinData[i].weights[0]);
        m_stream->WriteFloat(skinData[i].weights[1]);
        m_stream->WriteFloat(skinData[i].weights[2]);
        m_stream->WriteFloat(skinData[i].weights[3]);
    }

    EndChunk(startPos);
}

// void PrintBoneTree(Mesh *mesh, u32 boneIndex, int depth)
// {
//     Bone *bone = mesh->GetBone(boneIndex);

//     std::string indent(depth * 2, ' ');
//     std::cout << "  " << indent << "[" << boneIndex << "] " << bone->name << std::endl;

//     // Print children
//     for (size_t i = 0; i < mesh->GetBoneCount(); i++)
//     {

//         if (mesh->GetBone(i)->parentIndex == (s32)boneIndex)
//         {
//             PrintBoneTree(mesh, i, depth + 1);
//         }
//     }
// }

// void ValidateBoneHierarchy(Mesh *mesh)
// {

//     std::cout << "\n  Bone Hierarchy:" << std::endl;

//     // Conta roots
//     int rootCount = 0;
//     for (size_t i = 0; i < mesh->GetBoneCount(); i++)
//     {
//         Bone *bone = mesh->GetBone(i);
//         if (bone->parentIndex < 0)
//         {
//             PrintBoneTree(mesh, i, 0);
//             rootCount++;
//         }
//     }

//     if (rootCount == 1)
//     {
//         std::cout << "  ✓ Single root bone (OK)" << std::endl;
//     }
//     else if (rootCount > 1)
//     {
//         std::cout << "  ⚠ Warning: " << rootCount << " root bones detected!" << std::endl;
//     }
// }

bool MeshReader::Load(const std::string &filename, Mesh *mesh)
{
    float S = 0.01f;
    scale = Mat4::Scale(S, S, S);
    FileStream stream(filename, "rb");
    if (!stream.IsOpen())
    {
        LogError("[MeshReader] Failed to open: %s", filename.c_str());
        return false;
    }

    m_stream = &stream;
    m_stream->SetBigEndian(false);

    // Magic
    u32 magic = m_stream->ReadUInt();
    if (magic != MESH_MAGIC)
    {
        LogError("[MeshReader] Invalid magic: 0x%08X", magic);
        return false;
    }

    // Version
    u32 version = m_stream->ReadUInt();
    if (version / 100 > MESH_VERSION / 100)
    {
        LogWarning("[MeshReader] Newer version: %d", version);
    }

    mesh->m_boundBox.clear();

    // Read chunks
    while (!m_stream->IsEOF())
    {
        if (m_stream->Tell() >= m_stream->Size())
            break;

        ChunkHeader header = ReadChunkHeader();
        long chunkEnd = m_stream->Tell() + header.length;

        switch (header.id)
        {
        case CHUNK_MATS:
            ReadMaterialsChunk(mesh, header);
            break;

        case CHUNK_SKEL:
            ReadSkeletonChunk(mesh, header);
            break;

        case CHUNK_BUFF:
            ReadBufferChunk(mesh, header);
            break;

        default:
            // Skip unknown chunks
            LogWarning("[MeshReader] Unknown chunk: 0x%08X", header.id);
            SkipChunk(header);
            break;
        }

        // Garante alinhamento
        if (m_stream->Tell() < chunkEnd)
            m_stream->Seek(chunkEnd, SeekOrigin::Begin);
    }

    mesh->Build();

    LogInfo("[MeshReader] Loaded: %zu buffers, %zu materials, %zu bones",
            mesh->GetBufferCount(), mesh->GetMaterialCount(),
            mesh->GetBoneCount());

    //  ValidateBoneHierarchy(mesh);

    return true;
}

ChunkHeader MeshReader::ReadChunkHeader()
{
    ChunkHeader header;
    header.id = m_stream->ReadUInt();
    header.length = m_stream->ReadUInt();
    return header;
}

void MeshReader::SkipChunk(const ChunkHeader &header)
{
    m_stream->Seek(header.length, SeekOrigin::Current);
}

std::string MeshReader::ReadCString()
{
    std::string result;
    result.reserve(64);
    while (true)
    {
        char c = m_stream->ReadByte();
        if (c == 0)
            break;
        result += c;
    }
    return result;
}

void MeshReader::ReadMaterialsChunk(Mesh *mesh, const ChunkHeader &header)
{
    u32 numMaterials = m_stream->ReadUInt();

    for (u32 i = 0; i < numMaterials; i++)
    {
        std::string name = ReadCString();
        Material *mat = mesh->AddMaterial(name);

        LogInfo("[MeshReader] Material: %s", name.c_str());

        Vec3 diffuse;
        diffuse.x = m_stream->ReadFloat();
        diffuse.y = m_stream->ReadFloat();
        diffuse.z = m_stream->ReadFloat();
        mat->SetDiffuse(diffuse);

        Vec3 specular;
        specular.x = m_stream->ReadFloat();
        specular.y = m_stream->ReadFloat();
        specular.z = m_stream->ReadFloat();
        mat->SetSpecular(specular);

        mat->SetShininess(m_stream->ReadFloat());

        u8 numLayers = m_stream->ReadByte();
        for (u8 j = 0; j < numLayers; j++)
        {
            std::string path = ReadCString();
            if (!path.empty())
            {
                Texture *tex = TextureManager::Instance().Add(path);
                if (tex)
                    mat->SetTexture(j, tex);
                else
                    mat->SetTexture(j, TextureManager::Instance().GetDefault());
            }
        }
    }
}

void MeshReader::ReadSkeletonChunk(Mesh *mesh, const ChunkHeader &header)
{
    u32 numBones = m_stream->ReadUInt();

    for (u32 i = 0; i < numBones; i++)
    {
        Bone *bone = mesh->AddBone(ReadCString());

        bone->parentIndex = m_stream->ReadInt();
        //
        LogInfo("[MeshReader] Bone: %s Parent(%d)", bone->name.c_str(), bone->parentIndex);

        // Local transform
        for (int j = 0; j < 16; j++)
            bone->localPose.m[j] = m_stream->ReadFloat();

        // PrintMatrix(bone->localPose);

        for (int j = 0; j < 16; j++)
            bone->inverseBindPose.m[j] = m_stream->ReadFloat();

        // PrintMatrix(bone->inverseBindPose);
    }
}

void MeshReader::ReadBufferChunk(Mesh *mesh, const ChunkHeader &header)
{
    long endPos = m_stream->Tell() + header.length;

    u32 materialIndex = m_stream->ReadUInt();
    u32 flags = m_stream->ReadUInt();
    (void)flags;

    MeshBuffer *buffer = mesh->AddBuffer(materialIndex);

    // Read sub-chunks
    while (m_stream->Tell() < endPos)
    {
        ChunkHeader subHeader = ReadChunkHeader();
        long subEnd = m_stream->Tell() + subHeader.length;

        switch (subHeader.id)
        {
        case CHUNK_VRTS:
            ReadVerticesChunk(buffer, subHeader);
            break;

        case CHUNK_IDXS:
            ReadIndicesChunk(buffer, subHeader);
            break;

        case CHUNK_SKIN:
            ReadSkinChunk(buffer, subHeader);
            break;

        default:
            SkipChunk(subHeader);
            break;
        }

        // Alinhamento
        if (m_stream->Tell() < subEnd)
            m_stream->Seek(subEnd, SeekOrigin::Begin);
    }
    mesh->m_boundBox.merge(buffer->m_boundBox);
}

void MeshReader::ReadVerticesChunk(MeshBuffer *buffer, const ChunkHeader &header)
{

    u32 numVertices = m_stream->ReadUInt();

    for (u32 i = 0; i < numVertices; i++)
    {
        Vertex v;
        v.x = m_stream->ReadFloat();
        v.y = m_stream->ReadFloat();
        v.z = m_stream->ReadFloat();
        v.nx = m_stream->ReadFloat();
        v.ny = m_stream->ReadFloat();
        v.nz = m_stream->ReadFloat();
        v.u = m_stream->ReadFloat();
        v.v = m_stream->ReadFloat();
        buffer->AddVertex(v);
    }
}

void MeshReader::ReadIndicesChunk(MeshBuffer *buffer, const ChunkHeader &header)
{
    u32 numIndices = m_stream->ReadUInt();
    for (size_t i = 0; i < numIndices; i += 3)
    {
        u32 a = m_stream->ReadUInt();
        u32 b = m_stream->ReadUInt();
        u32 c = m_stream->ReadUInt();
        buffer->AddFace(a, b, c);
    }
}

void MeshReader::ReadSkinChunk(MeshBuffer *buffer, const ChunkHeader &header)
{
    u32 numVertices = m_stream->ReadUInt();
    // LogInfo("ReadSkinChunk numVertices %d", numVertices);

    buffer->m_skinData.resize(numVertices);
    buffer->m_isSkinned = true;
    buffer->m_skinnedVertices.resize(numVertices);

    for (u32 i = 0; i < numVertices; i++)
    {
        buffer->m_skinData[i].boneIDs[0] = m_stream->ReadByte();
        buffer->m_skinData[i].boneIDs[1] = m_stream->ReadByte();
        buffer->m_skinData[i].boneIDs[2] = m_stream->ReadByte();
        buffer->m_skinData[i].boneIDs[3] = m_stream->ReadByte();

        buffer->m_skinData[i].weights[0] = m_stream->ReadFloat();
        buffer->m_skinData[i].weights[1] = m_stream->ReadFloat();
        buffer->m_skinData[i].weights[2] = m_stream->ReadFloat();
        buffer->m_skinData[i].weights[3] = m_stream->ReadFloat();
    }
}

bool Animation::Load(const std::string &filename)
{
    m_currentTime = 0.0f;
    m_ticksPerSecond = 25.0f;

    AnimReader reader;
    AnimReader::FrameAnimation *frameAnim = reader.Load(filename);
    if (!frameAnim)
        return false;

    m_name = frameAnim->name;
    m_duration = frameAnim->duration;
    m_ticksPerSecond = frameAnim->ticksPerSecond;

    // Converte channels
    m_channels.resize(frameAnim->channels.size());
    for (size_t i = 0; i < frameAnim->channels.size(); i++)
    {
        m_channels[i].boneName = frameAnim->channels[i].boneName;
        m_channels[i].boneIndex = -1;
        m_channels[i].keyframes = frameAnim->channels[i].keyframes;
        m_boneMap[m_channels[i].boneName] = m_channels[i];
    }

    delete frameAnim;
    return true;
}

void Animation::Bind(Node3D *parent)
{
    for (auto &channel : m_channels)
    {
        channel.boneIndex = parent->findJointIndex(channel.boneName);
        if (channel.boneIndex == -1)
        {
            LogWarning("Bone not found: %s", channel.boneName.c_str());
        }
    }
}

AnimationChannel *Animation::FindChannel(const std::string &name)
{
    auto it = m_boneMap.find(name);
    if (it != m_boneMap.end())
        return &it->second;
    return nullptr;
}

Vec3 Animation::InterpolatePosition(const AnimationChannel &ch, float time)
{
    if (ch.keyframes.empty())
    {

        return Vec3(0, 0, 0);
    }

    if (ch.keyframes.size() == 1)
    {

        return ch.keyframes[0].position;
    }
    for (size_t i = 0; i < ch.keyframes.size() - 1; i++)
    {
        float t0 = ch.keyframes[i].time;
        float t1 = ch.keyframes[i + 1].time;

        if (time >= t0 && time <= t1)
        {
            float factor = (time - t0) / (t1 - t0);
            const Vec3 &p0 = ch.keyframes[i].position;
            const Vec3 &p1 = ch.keyframes[i + 1].position;
            return Vec3::Lerp(p0, p1, factor);
        }
    }

    return ch.keyframes.back().position;
}

Quat Animation::InterpolateRotation(const AnimationChannel &ch, float time)
{
    if (ch.keyframes.empty())
        return Quat(0, 0, 0, 1);

    if (ch.keyframes.size() == 1)
        return ch.keyframes[0].rotation;

    for (size_t i = 0; i < ch.keyframes.size() - 1; i++)
    {
        float t0 = ch.keyframes[i].time;
        float t1 = ch.keyframes[i + 1].time;

        if (time >= t0 && time <= t1)
        {
            float factor = (time - t0) / (t1 - t0);

            const Quat &q0 = ch.keyframes[i].rotation;
            const Quat &q1 = ch.keyframes[i + 1].rotation;
            return Quat::Slerp(q0, q1, factor);
        }
    }

    return ch.keyframes.back().rotation;
}

AnimReader::FrameAnimation *AnimReader::Load(const std::string &filename)
{
    FileStream stream;
    if (!stream.Open(filename, "rb"))
    {
        LogError("[AnimReader] Failed to open: %s", filename.c_str());
        return nullptr;
    }

    m_stream = &stream;
    m_stream->SetBigEndian(false);

    // Read magic + version
    u32 magic = m_stream->ReadUInt();
    u32 version = m_stream->ReadUInt();

    if (magic != ANIM_MAGIC)
    {
        LogError("[AnimReader] Invalid magic: 0x%08X", magic);
        stream.Close();
        return nullptr;
    }

    if (version != ANIM_VERSION)
    {
        LogError("[AnimReader] Invalid version: %d", version);
        stream.Close();
        return nullptr;
    }

    FrameAnimation *animation = new FrameAnimation();

    // Read chunks
    while (!m_stream->IsEOF())
    {
        // Check if enough data for chunk header
        long currentPos = m_stream->Tell();
        long fileSize = m_stream->Size();

        if (currentPos + 8 > fileSize)
            break;

        u32 chunkId = m_stream->ReadUInt();
        u32 chunkLength = m_stream->ReadUInt();

        long nextChunkPos = m_stream->Tell() + chunkLength;

        if (chunkId == ANIM_CHUNK_INFO)
        {
            if (!ReadInfoChunk(*animation))
            {
                LogError("[AnimReader] Failed to read INFO chunk");
                delete animation;
                stream.Close();
                return nullptr;
            }
        }
        else if (chunkId == ANIM_CHUNK_CHAN)
        {
            Channel channel;
            if (!ReadChannelChunk(channel))
            {
                LogError("[AnimReader] Failed to read CHAN chunk");
                delete animation;
                stream.Close();
                return nullptr;
            }

            animation->channels.push_back(channel);
        }
        else
        {
            LogError("[AnimReader] Unknown chunk: 0x%08X", chunkId);
        }

        // Seek to next chunk
        m_stream->Seek(nextChunkPos, SeekOrigin::Begin);
    }

    stream.Close();

    LogInfo("[AnimReader] Loaded animation: %s (%d channels, %.2f seconds)", animation->name.c_str(), animation->channels.size(), animation->duration);

    return animation;
}

bool AnimReader::ReadInfoChunk(FrameAnimation &anim)
{
    // Name (64 bytes fixed)
    char name[64];
    m_stream->Read(name, 64);
    name[63] = '\0'; // Ensure null-terminated
    anim.name = name;

    // Duration, ticks per second, num channels
    anim.duration = m_stream->ReadFloat();
    anim.ticksPerSecond = m_stream->ReadFloat();
    u32 numChannels = m_stream->ReadUInt();

    anim.channels.reserve(numChannels);

    return true;
}

bool AnimReader::ReadChannelChunk(Channel &channel)
{
    // Bone name (null-terminated string)
    channel.boneName = m_stream->ReadCString();

    // Number of keyframes
    u32 numKeys = m_stream->ReadUInt();
    channel.keyframes.reserve(numKeys);

    // LogInfo("[AnimReader] Reading channel: %s (%d keyframes)", channel.boneName.c_str(), numKeys);

    // Read all keyframes
    for (u32 i = 0; i < numKeys; i++)
    {
        AnimationKeyframe key;

        // Time
        key.time = m_stream->ReadFloat();

        // Position
        key.position.x = m_stream->ReadFloat();
        key.position.y = m_stream->ReadFloat();
        key.position.z = m_stream->ReadFloat();

        // key.position *= modelScale;

        // Rotation (quaternion)
        key.rotation.x = m_stream->ReadFloat();
        key.rotation.y = m_stream->ReadFloat();
        key.rotation.z = m_stream->ReadFloat();
        key.rotation.w = m_stream->ReadFloat();

        // Scale
        m_stream->ReadFloat();
        m_stream->ReadFloat();
        m_stream->ReadFloat();

        channel.keyframes.push_back(key);
    }

    return true;
}

Visual::Visual(const std::string &name) : Spatial(name)
{
}

Visual::~Visual()
{

    for (Material *material : materials)
    {
        delete material;
    }
    materials.clear();
}

void Visual::SetTexture(u32 layer, Texture *texture)
{
    for (Material *material : materials)
    {
        material->SetTexture(layer, texture);
    }
}

void Visual::SetMaterialTexture(u32 material, u32 layer, Texture *texture)
{
    DEBUG_BREAK_IF(material >= materials.size());
    materials[material]->SetTexture(layer, texture);
}

Material *Visual::AddMaterial(const std::string &name)
{
    Material *material = new Material();
    material->SetName(name);
    materials.push_back(material);
    return material;
}

Material *Visual::GetMaterial(u32 index) const
{
    DEBUG_BREAK_IF(index >= materials.size());

    return materials[index];
}
