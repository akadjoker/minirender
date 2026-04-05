#include "TerrainNode.hpp"
#include "Camera.hpp"
#include <algorithm>
#include <cmath>

TerrainLodNode::TerrainLodNode(const std::string &name, int maxLOD,
                               TerrainPatchSize patchSz, float detailScale)
    : m_patchSize(static_cast<int>(patchSz))
    , m_calcPatchSize(static_cast<int>(patchSz) - 1)
    , m_maxLOD(maxLOD)
    , m_detailScale(detailScale)
{
    this->name = name;
}

TerrainLodNode::~TerrainLodNode()
{
    delete m_renderBuffer;
    delete[] m_heightData;
}

bool TerrainLodNode::loadFromHeightmap(const std::string &path,
                                       float heightScale,
                                       int smoothFactor)
{
    Pixmap img;
    if (!img.Load(path.c_str()))
        return false;

    m_size = img.width;
    m_heightScale = heightScale;

    switch (m_patchSize)
    {
    case 9: m_maxLOD = std::min(m_maxLOD, 3); break;
    case 17: m_maxLOD = std::min(m_maxLOD, 4); break;
    case 33: m_maxLOD = std::min(m_maxLOD, 5); break;
    case 65: m_maxLOD = std::min(m_maxLOD, 6); break;
    default: m_maxLOD = std::min(m_maxLOD, 7); break;
    }

    delete[] m_heightData;
    m_heightData = new float[m_size * m_size];

    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        uint32_t pixel = img.GetPixel(x, z);
        uint8_t r = pixel & 0xFF;
        m_heightData[z * m_size + x] = static_cast<float>(r) / 255.f;
    }

    if (smoothFactor > 0)
        smooth(smoothFactor);

    const float tdSize = 1.f / static_cast<float>(m_size - 1);
    m_sourceVerts.resize(static_cast<size_t>(m_size) * m_size);

    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        float fx = static_cast<float>(x) * tdSize;
        float fz = static_cast<float>(z) * tdSize;
        float h = m_heightData[z * m_size + x] * m_heightScale;

        TerrainVertex &v = m_sourceVerts[static_cast<size_t>(z) * m_size + x];
        v.position = {fx, h, fz};
        v.normal = {0.f, 1.f, 0.f};
        v.uv = {fx * m_texScale, fz * m_texScale};
        v.uv2 = {fx * m_detailScale, fz * m_detailScale};
    }

    delete m_renderBuffer;
    m_renderBuffer = new TerrainBuffer();
    m_renderBuffer->vertices = m_sourceVerts;

    applyTransformation();

    const size_t maxIndices = static_cast<size_t>(m_patchCount) * m_patchCount *
                              m_calcPatchSize * m_calcPatchSize * 6;
    m_renderBuffer->indices.resize(maxIndices);
    m_renderBuffer->allocateDynamicIndices(maxIndices);
    m_forceRecalc = true;
    return true;
}

void TerrainLodNode::smooth(int iterations)
{
    for (int iter = 0; iter < iterations; ++iter)
    {
        std::vector<float> tmp(static_cast<size_t>(m_size) * m_size);
        int yd = m_size;
        for (int y = 1; y < m_size - 1; ++y)
        {
            for (int x = 1; x < m_size - 1; ++x)
            {
                tmp[x + yd] = (m_heightData[x - 1 + yd] +
                               m_heightData[x + 1 + yd] +
                               m_heightData[x + yd - m_size] +
                               m_heightData[x + yd + m_size]) * 0.25f;
            }
            yd += m_size;
        }
        for (int y = 1; y < m_size - 1; ++y)
        for (int x = 1; x < m_size - 1; ++x)
            m_heightData[x + y * m_size] = tmp[x + y * m_size];
    }
}

void TerrainLodNode::calculateNormals()
{
    auto posAt = [&](int x, int z) -> glm::vec3
    {
        x = std::max(0, std::min(x, m_size - 1));
        z = std::max(0, std::min(z, m_size - 1));
        return m_renderBuffer->vertices[static_cast<size_t>(z) * m_size + x].position;
    };

    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        glm::vec3 L = posAt(x - 1, z);
        glm::vec3 R = posAt(x + 1, z);
        glm::vec3 D = posAt(x, z - 1);
        glm::vec3 U = posAt(x, z + 1);
        glm::vec3 n = glm::cross(U - D, R - L);
        float len = glm::length(n);
        m_renderBuffer->vertices[static_cast<size_t>(z) * m_size + x].normal =
            (len > 1e-6f) ? n / len : glm::vec3(0.f, 1.f, 0.f);
    }
}

void TerrainLodNode::applyTransformation()
{
    if (m_sourceVerts.empty())
        return;

    const glm::vec3 pos = position;
    const glm::vec3 scl = scale;

    const size_t count = m_sourceVerts.size();
    m_renderBuffer->vertices.resize(count);

    for (size_t i = 0; i < count; ++i)
    {
        const TerrainVertex &src = m_sourceVerts[i];
        TerrainVertex &dst = m_renderBuffer->vertices[i];
        dst.position = src.position * scl + pos;
        dst.uv = src.uv;
        dst.uv2 = src.uv2;
    }

    calculateNormals();

    if (m_renderBuffer->vbo != 0)
        m_renderBuffer->update();

    calculateDistanceThresholds();
    createPatches();
    calculatePatchData();
}

void TerrainLodNode::calculateDistanceThresholds()
{
    m_lodDist.resize(m_maxLOD);

    const float normPatch = static_cast<float>(m_calcPatchSize) / static_cast<float>(m_size - 1);
    const float patchSzX = normPatch * scale.x;
    const float patchSzZ = normPatch * scale.z;
    const float diagonal = std::sqrt(patchSzX * patchSzX + patchSzZ * patchSzZ);

    for (int i = 0; i < m_maxLOD; ++i)
    {
        float d = diagonal * std::pow(2.f, static_cast<float>(i));
        m_lodDist[i] = d * d;
    }
}

void TerrainLodNode::createPatches()
{
    m_patchCount = (m_size - 1) / m_calcPatchSize;
    m_patches.assign(static_cast<size_t>(m_patchCount) * m_patchCount, Patch{});
}

void TerrainLodNode::calculatePatchData()
{
    if (!m_renderBuffer || m_patches.empty())
        return;

    m_aabb = BoundingBox{};

    for (int px = 0; px < m_patchCount; ++px)
    for (int pz = 0; pz < m_patchCount; ++pz)
    {
        Patch &p = m_patches[static_cast<size_t>(px) * m_patchCount + pz];
        p.aabb = BoundingBox{};

        const int xStart = pz * m_calcPatchSize;
        const int zStart = px * m_calcPatchSize;
        const int xEnd = xStart + m_calcPatchSize;
        const int zEnd = zStart + m_calcPatchSize;

        bool first = true;
        for (int x = xStart; x <= xEnd; ++x)
        for (int z = zStart; z <= zEnd; ++z)
        {
            const glm::vec3 &pos = m_renderBuffer->vertices[static_cast<size_t>(z) * m_size + x].position;
            if (first)
            {
                p.aabb.min = p.aabb.max = pos;
                first = false;
            }
            else
            {
                p.aabb.expand(pos);
            }
        }

        p.center = p.aabb.center();
        p.top = (px > 0) ? &m_patches[static_cast<size_t>(px - 1) * m_patchCount + pz] : nullptr;
        p.bottom = (px < m_patchCount - 1) ? &m_patches[static_cast<size_t>(px + 1) * m_patchCount + pz] : nullptr;
        p.left = (pz > 0) ? &m_patches[static_cast<size_t>(px) * m_patchCount + (pz - 1)] : nullptr;
        p.right = (pz < m_patchCount - 1) ? &m_patches[static_cast<size_t>(px) * m_patchCount + (pz + 1)] : nullptr;

        m_aabb.expand(p.aabb.min);
        m_aabb.expand(p.aabb.max);
    }
}

bool TerrainLodNode::preRenderLODCalculations(const glm::vec3 &camPos,
                                              const glm::vec3 &camForward,
                                              const Frustum &frustum)
{
    if (!m_forceRecalc)
    {
        glm::vec3 dPos = camPos - m_oldCamPos;
        float movedSq = glm::dot(dPos, dPos);
        float dotForward = glm::dot(camForward, m_oldCamForward);
        if (movedSq < m_camMoveDelta * m_camMoveDelta && dotForward > m_camRotDelta)
            return false;
    }

    m_oldCamPos = camPos;
    m_oldCamForward = camForward;
    m_forceRecalc = false;

    const int count = m_patchCount * m_patchCount;
    for (int j = 0; j < count; ++j)
    {
        Patch &p = m_patches[j];
        if (frustum.intersectsLoose(p.aabb))
        {
            float distSq = glm::dot(camPos - p.center, camPos - p.center);
            p.currentLOD = 0;
            for (int i = m_maxLOD - 1; i > 0; --i)
            {
                if (distSq >= m_lodDist[i])
                {
                    p.currentLOD = i;
                    break;
                }
            }
        }
        else
        {
            p.currentLOD = -1;
        }
    }
    return true;
}

void TerrainLodNode::preRenderIndicesCalculations()
{
    m_indicesToRender = 0;
    auto &indices = m_renderBuffer->indices;

    int patchIdx = 0;
    for (int i = 0; i < m_patchCount; ++i)
    for (int j = 0; j < m_patchCount; ++j, ++patchIdx)
    {
        if (m_patches[patchIdx].currentLOD < 0)
            continue;

        const int step = 1 << m_patches[patchIdx].currentLOD;
        int x = 0;
        int z = 0;

        while (z < m_calcPatchSize)
        {
            uint32_t i11 = getIndex(j, i, patchIdx, static_cast<uint32_t>(x), static_cast<uint32_t>(z));
            uint32_t i21 = getIndex(j, i, patchIdx, static_cast<uint32_t>(x + step), static_cast<uint32_t>(z));
            uint32_t i12 = getIndex(j, i, patchIdx, static_cast<uint32_t>(x), static_cast<uint32_t>(z + step));
            uint32_t i22 = getIndex(j, i, patchIdx, static_cast<uint32_t>(x + step), static_cast<uint32_t>(z + step));

            indices[m_indicesToRender++] = i11;
            indices[m_indicesToRender++] = i12;
            indices[m_indicesToRender++] = i22;
            indices[m_indicesToRender++] = i11;
            indices[m_indicesToRender++] = i22;
            indices[m_indicesToRender++] = i21;

            x += step;
            if (x >= m_calcPatchSize)
            {
                x = 0;
                z += step;
            }
        }
    }

    m_renderBuffer->updateIndices(m_indicesToRender);
}

uint32_t TerrainLodNode::getIndex(int patchX, int patchZ, int patchIdx, uint32_t vX, uint32_t vZ) const
{
    const Patch &p = m_patches[patchIdx];
    const uint32_t calcSz = static_cast<uint32_t>(m_calcPatchSize);

    if (vZ == 0 && p.top && p.top->currentLOD >= 0 &&
        p.currentLOD < p.top->currentLOD &&
        (vX % (1u << p.top->currentLOD)) != 0)
    {
        vX -= vX % (1u << p.top->currentLOD);
    }
    else if (vZ == calcSz && p.bottom && p.bottom->currentLOD >= 0 &&
             p.currentLOD < p.bottom->currentLOD &&
             (vX % (1u << p.bottom->currentLOD)) != 0)
    {
        vX -= vX % (1u << p.bottom->currentLOD);
    }

    if (vX == 0 && p.left && p.left->currentLOD >= 0 &&
        p.currentLOD < p.left->currentLOD &&
        (vZ % (1u << p.left->currentLOD)) != 0)
    {
        vZ -= vZ % (1u << p.left->currentLOD);
    }
    else if (vX == calcSz && p.right && p.right->currentLOD >= 0 &&
             p.currentLOD < p.right->currentLOD &&
             (vZ % (1u << p.right->currentLOD)) != 0)
    {
        vZ -= vZ % (1u << p.right->currentLOD);
    }

    if (vZ >= static_cast<uint32_t>(m_patchSize)) vZ = calcSz;
    if (vX >= static_cast<uint32_t>(m_patchSize)) vX = calcSz;

    return (vZ + static_cast<uint32_t>(m_calcPatchSize * patchZ)) * static_cast<uint32_t>(m_size) +
           (vX + static_cast<uint32_t>(m_calcPatchSize * patchX));
}

void TerrainLodNode::setLODDistance(int lod, float dist)
{
    if (lod >= 0 && lod < static_cast<int>(m_lodDist.size()))
        m_lodDist[lod] = dist * dist;
}

void TerrainLodNode::setTextureScale(float s)
{
    m_texScale = s;
    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        float fx = static_cast<float>(x) / static_cast<float>(m_size - 1);
        float fz = static_cast<float>(z) / static_cast<float>(m_size - 1);
        m_sourceVerts[static_cast<size_t>(z) * m_size + x].uv = {fx * m_texScale, fz * m_texScale};
    }
    applyTransformation();
}

float TerrainLodNode::sampleHeight(int x, int z) const
{
    x = std::max(0, std::min(x, m_size - 1));
    z = std::max(0, std::min(z, m_size - 1));
    return m_heightData[z * m_size + x];
}

float TerrainLodNode::getHeightAt(float worldX, float worldZ) const
{
    if (!m_heightData || !m_renderBuffer)
        return 0.f;

    float lx = (worldX - position.x) / scale.x;
    float lz = (worldZ - position.z) / scale.z;
    lx = std::max(0.f, std::min(1.f, lx));
    lz = std::max(0.f, std::min(1.f, lz));

    float gx = lx * static_cast<float>(m_size - 1);
    float gz = lz * static_cast<float>(m_size - 1);
    int ix = std::min(static_cast<int>(gx), m_size - 2);
    int iz = std::min(static_cast<int>(gz), m_size - 2);
    float fx = gx - static_cast<float>(ix);
    float fz = gz - static_cast<float>(iz);

    auto wy = [&](int x, int z)
    {
        return m_renderBuffer->vertices[static_cast<size_t>(z) * m_size + x].position.y;
    };

    return wy(ix, iz) * (1 - fx) * (1 - fz) + wy(ix + 1, iz) * fx * (1 - fz) +
           wy(ix, iz + 1) * (1 - fx) * fz + wy(ix + 1, iz + 1) * fx * fz;
}

glm::vec3 TerrainLodNode::calcNormal(int x, int z) const
{
    float hL = sampleHeight(x - 1, z) * m_heightScale * scale.y;
    float hR = sampleHeight(x + 1, z) * m_heightScale * scale.y;
    float hD = sampleHeight(x, z - 1) * m_heightScale * scale.y;
    float hU = sampleHeight(x, z + 1) * m_heightScale * scale.y;
    float sx = scale.x / static_cast<float>(m_size - 1);
    float sz = scale.z / static_cast<float>(m_size - 1);
    return glm::normalize(glm::vec3((hL - hR) / (2.f * sx), 1.f, (hD - hU) / (2.f * sz)));
}

glm::vec3 TerrainLodNode::getNormalAt(float worldX, float worldZ) const
{
    if (!m_heightData || !m_renderBuffer)
        return {0.f, 1.f, 0.f};

    float lx = (worldX - position.x) / scale.x;
    float lz = (worldZ - position.z) / scale.z;
    float gx = std::max(0.f, std::min(1.f, lx)) * static_cast<float>(m_size - 1);
    float gz = std::max(0.f, std::min(1.f, lz)) * static_cast<float>(m_size - 1);
    int ix = std::max(0, std::min(static_cast<int>(std::round(gx)), m_size - 1));
    int iz = std::max(0, std::min(static_cast<int>(std::round(gz)), m_size - 1));
    return glm::normalize(m_renderBuffer->vertices[static_cast<size_t>(iz) * m_size + ix].normal);
}

TerrainRaycastResult TerrainLodNode::raycast(const Ray &ray, float maxDist) const
{
    TerrainRaycastResult r;
    if (!m_heightData)
        return r;

    float cellSz = std::min(scale.x, scale.z) / static_cast<float>(m_size - 1);
    for (float t = 0.f; t <= maxDist; t += cellSz * 2.f)
    {
        glm::vec3 p = ray.origin + ray.direction * t;
        float th = getHeightAt(p.x, p.z);
        if (t > 0.f && p.y <= th)
        {
            float lo = t - cellSz * 2.f;
            float hi = t;
            for (int i = 0; i < 8; ++i)
            {
                float tm = (lo + hi) * 0.5f;
                glm::vec3 pm = ray.origin + ray.direction * tm;
                if (pm.y <= getHeightAt(pm.x, pm.z))
                    hi = tm;
                else
                    lo = tm;
            }
            float tf = (lo + hi) * 0.5f;
            glm::vec3 h = ray.origin + ray.direction * tf;
            r.hit = true;
            r.position = h;
            r.normal = getNormalAt(h.x, h.z);
            r.distance = tf;
            return r;
        }
    }
    return r;
}

bool TerrainLodNode::prepareForRender(const Camera *camera, const Frustum &frustum)
{
    if (!visible || !m_material || !m_renderBuffer || m_patches.empty() || !camera)
        return false;

    const glm::vec3 camPos = camera->worldPosition();
    const glm::vec3 camForward = glm::normalize(camera->forward());
    const bool changed = preRenderLODCalculations(camPos, camForward, frustum);
    if (changed)
        preRenderIndicesCalculations();

    return m_indicesToRender > 0;
}
