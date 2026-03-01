#include "TerrainNode.hpp"
#include "Pixmap.hpp"
#include "RenderPipeline.hpp"
#include "Math.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainNode
// ─────────────────────────────────────────────────────────────────────────────
TerrainNode::TerrainNode(const std::string &name)
{
    this->name = name;
}

TerrainNode::~TerrainNode()
{
    for (auto *b : m_blocks)
        delete b;
    delete[] m_heightData;
}

// ── Load ─────────────────────────────────────────────────────────────────────
bool TerrainNode::loadFromHeightmap(const std::string &path,
                                    float scaleX, float scaleY, float scaleZ,
                                    float texScaleU, float texScaleV,
                                    float detailScale)
{
    Pixmap img;
    if (!img.Load(path.c_str()))
        return false;

    m_mapW         = img.width;
    m_mapH         = img.height;
    m_terrainScale = {scaleX, scaleY, scaleZ};
    m_texScaleU    = texScaleU;
    m_texScaleV    = texScaleV;
    m_detailScale  = detailScale;

    delete[] m_heightData;
    m_heightData = new float[m_mapW * m_mapH];

    // Normalise: use red channel, map to [0,1]
    for (int z = 0; z < m_mapH; ++z)
    for (int x = 0; x < m_mapW; ++x)
    {
        uint32_t pixel = img.GetPixel(x, z);
        uint8_t  r     = (pixel >> 24) & 0xFF;   // Pixmap stores RGBA
        m_heightData[z * m_mapW + x] = (float)r / 255.f;
    }

    // Build block meshes
    for (auto *b : m_blocks) delete b;
    m_blocks.clear();
    m_blockAABBs.clear();
    m_aabb = BoundingBox{};

    int blocksX = std::max(1, (m_mapW  - 1) / (BLOCK_VERTS - 1));
    int blocksZ = std::max(1, (m_mapH - 1) / (BLOCK_VERTS - 1));

    m_blocks.reserve(blocksX * blocksZ);
    m_blockAABBs.reserve(blocksX * blocksZ);

    for (int bz = 0; bz < blocksZ; ++bz)
    for (int bx = 0; bx < blocksX; ++bx)
    {
        auto  *buf  = new TerrainBuffer();
        BoundingBox blockAABB;
        if (generateBlock(buf, blockAABB, bx * (BLOCK_VERTS - 1), bz * (BLOCK_VERTS - 1),
                          texScaleU, texScaleV, detailScale))
        {
            buf->upload();
            buf->aabb = blockAABB;
            m_aabb.expand(blockAABB.min);
            m_aabb.expand(blockAABB.max);
            m_blocks.push_back(buf);
            m_blockAABBs.push_back(blockAABB);
        }
        else
        {
            delete buf;
        }
    }

    return !m_blocks.empty();
}

// ── Build a single block ──────────────────────────────────────────────────────
bool TerrainNode::generateBlock(TerrainBuffer *buf, BoundingBox &outAABB,
                                int originX, int originZ,
                                float texScaleU, float texScaleV,
                                float detailScale)
{
    const float invW = 1.f / (float)(m_mapW  - 1);
    const float invH = 1.f / (float)(m_mapH - 1);

    int endX = std::min(originX + BLOCK_VERTS, m_mapW);
    int endZ = std::min(originZ + BLOCK_VERTS, m_mapH);
    int countX = endX - originX;
    int countZ = endZ - originZ;

    if (countX < 2 || countZ < 2)
        return false;

    buf->vertices.reserve((size_t)countX * countZ);
    buf->indices.reserve((size_t)(countX - 1) * (countZ - 1) * 6);

    outAABB = BoundingBox{};

    for (int lz = 0; lz < countZ; ++lz)
    for (int lx = 0; lx < countX; ++lx)
    {
        int wx = originX + lx;
        int wz = originZ + lz;

        float h = sampleHeight(wx, wz) * m_terrainScale.y;
        float px = (float)wx * invW * m_terrainScale.x;
        float pz = (float)wz * invH * m_terrainScale.z;

        TerrainVertex v;
        v.position = {px, h, pz};
        v.normal   = calcNormal(wx, wz);
        v.uv       = {(float)wx * invW * texScaleU,
                      (float)wz * invH * texScaleV};
        v.uv2      = {(float)wx * invW * detailScale,
                      (float)wz * invH * detailScale};

        outAABB.expand(v.position);
        buf->vertices.push_back(v);
    }

    // Indices — two triangles per quad
    for (int lz = 0; lz < countZ - 1; ++lz)
    for (int lx = 0; lx < countX - 1; ++lx)
    {
        uint32_t tl = (uint32_t)(lz     * countX + lx);
        uint32_t tr = (uint32_t)(lz     * countX + lx + 1);
        uint32_t bl = (uint32_t)((lz+1) * countX + lx);
        uint32_t br = (uint32_t)((lz+1) * countX + lx + 1);

        buf->indices.push_back(tl);
        buf->indices.push_back(bl);
        buf->indices.push_back(tr);

        buf->indices.push_back(tr);
        buf->indices.push_back(bl);
        buf->indices.push_back(br);
    }

    return true;
}

// ── Height / normal queries ───────────────────────────────────────────────────
float TerrainNode::sampleHeight(int x, int z) const
{
    x = std::max(0, std::min(x, m_mapW  - 1));
    z = std::max(0, std::min(z, m_mapH - 1));
    return m_heightData[z * m_mapW + x];
}

glm::vec3 TerrainNode::calcNormal(int x, int z) const
{
    float hL = sampleHeight(x - 1, z) * m_terrainScale.y;
    float hR = sampleHeight(x + 1, z) * m_terrainScale.y;
    float hD = sampleHeight(x, z - 1) * m_terrainScale.y;
    float hU = sampleHeight(x, z + 1) * m_terrainScale.y;

    float scaleX = m_terrainScale.x / (float)(m_mapW  - 1);
    float scaleZ = m_terrainScale.z / (float)(m_mapH - 1);

    glm::vec3 n = glm::normalize(glm::vec3(
        (hL - hR) / (2.f * scaleX),
        1.f,
        (hD - hU) / (2.f * scaleZ)));
    return n;
}

float TerrainNode::getHeightAt(float worldX, float worldZ) const
{
    if (!m_heightData || m_mapW < 2 || m_mapH < 2) return 0.f;

    const glm::mat4 inv = glm::inverse(worldMatrix());
    const glm::vec4 local = inv * glm::vec4(worldX, 0, worldZ, 1);

    float fx = local.x / m_terrainScale.x * (float)(m_mapW  - 1);
    float fz = local.z / m_terrainScale.z * (float)(m_mapH - 1);

    int x0 = (int)std::floor(fx);
    int z0 = (int)std::floor(fz);
    float tx = fx - (float)x0;
    float tz = fz - (float)z0;

    float h00 = sampleHeight(x0,   z0  ) * m_terrainScale.y;
    float h10 = sampleHeight(x0+1, z0  ) * m_terrainScale.y;
    float h01 = sampleHeight(x0,   z0+1) * m_terrainScale.y;
    float h11 = sampleHeight(x0+1, z0+1) * m_terrainScale.y;

    float localH = lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);

    // Transform back: apply world Y
    return worldMatrix()[3][1] + localH * worldMatrix()[1][1];
}

glm::vec3 TerrainNode::getNormalAt(float worldX, float worldZ) const
{
    if (!m_heightData) return {0,1,0};

    const glm::mat4 inv = glm::inverse(worldMatrix());
    const glm::vec4 local = inv * glm::vec4(worldX, 0, worldZ, 1);

    float fx = local.x / m_terrainScale.x * (float)(m_mapW  - 1);
    float fz = local.z / m_terrainScale.z * (float)(m_mapH - 1);

    int x = (int)std::round(fx);
    int z = (int)std::round(fz);

    glm::vec3 localN = calcNormal(x, z);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(worldMatrix())));
    return glm::normalize(normalMat * localN);
}

TerrainRaycastResult TerrainNode::raycast(const Ray &ray, float maxDist) const
{
    TerrainRaycastResult result;
    if (!m_heightData) return result;

    // Step along ray in small increments
    const float step = std::min(m_terrainScale.x, m_terrainScale.z) /
                       (float)(std::max(m_mapW, m_mapH) - 1);

    for (float t = 0.f; t <= maxDist; t += step * 2.f)
    {
        glm::vec3 p = ray.origin + ray.direction * t;
        float terrH = getHeightAt(p.x, p.z);

        if (t > 0.f && p.y <= terrH)
        {
            // Refine between previous and current
            float tLo = t - step * 2.f;
            float tHi = t;
            for (int i = 0; i < 8; ++i)
            {
                float tMid = (tLo + tHi) * 0.5f;
                glm::vec3 pm = ray.origin + ray.direction * tMid;
                float hm = getHeightAt(pm.x, pm.z);
                if (pm.y <= hm) tHi = tMid;
                else            tLo = tMid;
            }

            float tFinal = (tLo + tHi) * 0.5f;
            glm::vec3 hit = ray.origin + ray.direction * tFinal;

            result.hit      = true;
            result.position = hit;
            result.normal   = getNormalAt(hit.x, hit.z);
            result.distance = tFinal;
            return result;
        }
    }

    return result;
}

// ── Render integration ────────────────────────────────────────────────────────
void TerrainNode::gatherRenderItems(RenderQueue &q, const FrameContext &ctx)
{
    if (!visible || !m_material || m_blocks.empty()) return;

    const glm::mat4 world = worldMatrix();
    const Frustum  &frust = ctx.frustum;

    for (size_t i = 0; i < m_blocks.size(); ++i)
    {
        TerrainBuffer *buf = m_blocks[i];
        BoundingBox worldAABB = m_blockAABBs[i].transformed(world);

        if (!frust.contains(worldAABB))
            continue;

        RenderItem item;
        item.drawable   = buf;
        item.material   = m_material;
        item.model      = world;
        item.worldAABB  = worldAABB;
        item.indexStart = 0;
        item.indexCount = (uint32_t)buf->indices.size();
        item.passMask   = RenderPassMask::Opaque;

        q.add(item);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainLodNode
// ─────────────────────────────────────────────────────────────────────────────
TerrainLodNode::TerrainLodNode(const std::string &name, int maxLOD, TerrainPatchSize patchSz,
                               float detailScale)
    : m_patchVerts(static_cast<int>(patchSz))
    , m_maxLOD(std::min(maxLOD, MAX_LOD))
    , m_detailScale(detailScale)
{
    this->name = name;
}

TerrainLodNode::~TerrainLodNode()
{
    delete[] m_heightData;
}

bool TerrainLodNode::loadFromHeightmap(const std::string &path,
                                       float heightScale,
                                       int   smoothFactor)
{
    Pixmap img;
    if (!img.Load(path.c_str()))
        return false;

    m_hmW        = img.width;
    m_hmH        = img.height;
    m_heightScale = heightScale;

    delete[] m_heightData;
    m_heightData = new float[m_hmW * m_hmH];

    for (int z = 0; z < m_hmH; ++z)
    for (int x = 0; x < m_hmW; ++x)
    {
        uint32_t pixel = img.GetPixel(x, z);
        uint8_t  r     = (pixel >> 24) & 0xFF;
        m_heightData[z * m_hmW + x] = (float)r / 255.f;
    }

    // Smooth
    if (smoothFactor > 0)
        smooth(smoothFactor);

    m_terrainScale = scale; // inherit from Node3D

    buildPatches();
    computeLODDist();

    return !m_patches.empty();
}

void TerrainLodNode::smooth(int iterations)
{
    std::vector<float> tmp(m_hmW * m_hmH);
    for (int iter = 0; iter < iterations; ++iter)
    {
        for (int z = 0; z < m_hmH; ++z)
        for (int x = 0; x < m_hmW; ++x)
        {
            float sum = 0.f;
            int   cnt = 0;
            for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
            {
                int nx = x + dx, nz = z + dz;
                if (nx >= 0 && nx < m_hmW && nz >= 0 && nz < m_hmH)
                {
                    sum += m_heightData[nz * m_hmW + nx];
                    ++cnt;
                }
            }
            tmp[z * m_hmW + x] = sum / (float)cnt;
        }
        std::memcpy(m_heightData, tmp.data(), tmp.size() * sizeof(float));
    }
}

void TerrainLodNode::buildPatches()
{
    int step = m_patchVerts - 1;
    m_patchCount = std::max(1, (std::max(m_hmW, m_hmH) - 1) / step);

    m_patches.resize((size_t)m_patchCount * m_patchCount);
    m_aabb = BoundingBox{};

    for (int pz = 0; pz < m_patchCount; ++pz)
    for (int px = 0; px < m_patchCount; ++px)
    {
        Patch &p = m_patches[(size_t)pz * m_patchCount + px];

        // Pre-compute center & AABB by sampling corners
        int originX = px * step;
        int originZ = pz * step;
        int endX    = std::min(originX + step, m_hmW - 1);
        int endZ    = std::min(originZ + step, m_hmH - 1);

        float minH = 1e30f, maxH = -1e30f;
        for (int sz = originZ; sz <= endZ; ++sz)
        for (int sx = originX; sx <= endX; ++sx)
        {
            float h = sampleHeight(sx, sz) * m_heightScale;
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }

        float wx0 = (float)originX / (float)(m_hmW - 1) * scale.x;
        float wz0 = (float)originZ / (float)(m_hmH - 1) * scale.z;
        float wx1 = (float)endX    / (float)(m_hmW - 1) * scale.x;
        float wz1 = (float)endZ    / (float)(m_hmH - 1) * scale.z;

        p.aabb.min = {wx0, minH, wz0};
        p.aabb.max = {wx1, maxH, wz1};
        p.center   = p.aabb.center();

        m_aabb.expand(p.aabb.min);
        m_aabb.expand(p.aabb.max);

        // Build all LOD meshes eagerly
        for (int lod = 0; lod < m_maxLOD; ++lod)
        {
            p.meshes[lod] = new TerrainBuffer();
            buildPatchMesh(p, px, pz, lod);
            p.meshes[lod]->aabb = p.aabb;
            p.meshes[lod]->upload();
        }
    }
}

void TerrainLodNode::buildPatchMesh(Patch &p, int patchX, int patchZ, int lod)
{
    int stride    = 1 << lod;   // 1, 2, 4, 8
    int step      = m_patchVerts - 1;
    int originX   = patchX * step;
    int originZ   = patchZ * step;

    int endX      = std::min(originX + step, m_hmW - 1);
    int endZ      = std::min(originZ + step, m_hmH - 1);

    float invW    = 1.f / (float)(m_hmW - 1);
    float invH    = 1.f / (float)(m_hmH - 1);

    TerrainBuffer *buf = p.meshes[lod];
    buf->vertices.clear();
    buf->indices.clear();

    // Build vertex grid at the given stride
    std::vector<int> xCoords, zCoords;
    for (int x = originX; x <= endX; x += stride) xCoords.push_back(x);
    for (int z = originZ; z <= endZ; z += stride) zCoords.push_back(z);

    // Make sure last vertex hits the patch edge
    if (xCoords.empty() || xCoords.back() != endX) xCoords.push_back(endX);
    if (zCoords.empty() || zCoords.back() != endZ) zCoords.push_back(endZ);

    int vCountX = (int)xCoords.size();
    int vCountZ = (int)zCoords.size();

    buf->vertices.reserve((size_t)vCountX * vCountZ);
    buf->indices.reserve((size_t)(vCountX - 1) * (vCountZ - 1) * 6);

    for (int lz = 0; lz < vCountZ; ++lz)
    for (int lx = 0; lx < vCountX; ++lx)
    {
        int hmx = xCoords[lx];
        int hmz = zCoords[lz];

        float h  = sampleHeight(hmx, hmz) * m_heightScale;
        glm::vec3 n = calcNormal(hmx, hmz);

        TerrainVertex v;
        v.position = {(float)hmx * invW * scale.x,
                      h,
                      (float)hmz * invH * scale.z};
        v.normal   = n;
        v.uv       = {(float)hmx * invW * m_texScale,
                      (float)hmz * invH * m_texScale};
        v.uv2      = {(float)hmx * invW * m_detailScale,
                      (float)hmz * invH * m_detailScale};

        buf->vertices.push_back(v);
    }

    for (int lz = 0; lz < vCountZ - 1; ++lz)
    for (int lx = 0; lx < vCountX - 1; ++lx)
    {
        uint32_t tl = (uint32_t)(lz     * vCountX + lx);
        uint32_t tr = (uint32_t)(lz     * vCountX + lx + 1);
        uint32_t bl = (uint32_t)((lz+1) * vCountX + lx);
        uint32_t br = (uint32_t)((lz+1) * vCountX + lx + 1);

        buf->indices.push_back(tl);
        buf->indices.push_back(bl);
        buf->indices.push_back(tr);

        buf->indices.push_back(tr);
        buf->indices.push_back(bl);
        buf->indices.push_back(br);
    }
}

void TerrainLodNode::computeLODDist()
{
    m_lodDist.resize(m_maxLOD);
    float baseSize = (float)(m_patchVerts - 1) * scale.x / (float)(m_hmW - 1);
    for (int i = 0; i < m_maxLOD; ++i)
    {
        float d = baseSize * (float)(2 << i) * 4.f; // heuristic multiplier
        m_lodDist[i] = d * d;
    }
}

void TerrainLodNode::setLODDistance(int lod, float dist)
{
    if (lod >= 0 && lod < (int)m_lodDist.size())
        m_lodDist[lod] = dist * dist;
}

int TerrainLodNode::calcLOD(float distSq) const
{
    for (int i = 0; i < (int)m_lodDist.size(); ++i)
        if (distSq < m_lodDist[i])
            return i;
    return m_maxLOD - 1;
}

float TerrainLodNode::sampleHeight(int x, int z) const
{
    x = std::max(0, std::min(x, m_hmW - 1));
    z = std::max(0, std::min(z, m_hmH - 1));
    return m_heightData[z * m_hmW + x];
}

glm::vec3 TerrainLodNode::calcNormal(int x, int z) const
{
    float hL = sampleHeight(x-1, z) * m_heightScale;
    float hR = sampleHeight(x+1, z) * m_heightScale;
    float hD = sampleHeight(x, z-1) * m_heightScale;
    float hU = sampleHeight(x, z+1) * m_heightScale;
    float sx  = scale.x / (float)(m_hmW - 1);
    float sz  = scale.z / (float)(m_hmH - 1);
    return glm::normalize(glm::vec3((hL - hR) / (2.f * sx), 1.f, (hD - hU) / (2.f * sz)));
}

float TerrainLodNode::getHeightAt(float worldX, float worldZ) const
{
    if (!m_heightData) return 0.f;
    const glm::mat4 inv = glm::inverse(worldMatrix());
    const glm::vec4 local = inv * glm::vec4(worldX, 0, worldZ, 1);
    float fx = local.x / scale.x * (float)(m_hmW - 1);
    float fz = local.z / scale.z * (float)(m_hmH - 1);
    int x0 = (int)std::floor(fx), z0 = (int)std::floor(fz);
    float tx = fx - x0, tz = fz - z0;
    float h00 = sampleHeight(x0,   z0  ) * m_heightScale;
    float h10 = sampleHeight(x0+1, z0  ) * m_heightScale;
    float h01 = sampleHeight(x0,   z0+1) * m_heightScale;
    float h11 = sampleHeight(x0+1, z0+1) * m_heightScale;
    return worldMatrix()[3][1] + lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);
}

glm::vec3 TerrainLodNode::getNormalAt(float worldX, float worldZ) const
{
    if (!m_heightData) return {0,1,0};
    const glm::mat4 inv = glm::inverse(worldMatrix());
    const glm::vec4 local = inv * glm::vec4(worldX, 0, worldZ, 1);
    float fx = local.x / scale.x * (float)(m_hmW - 1);
    float fz = local.z / scale.z * (float)(m_hmH - 1);
    glm::vec3 ln = calcNormal((int)std::round(fx), (int)std::round(fz));
    glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(worldMatrix())));
    return glm::normalize(nm * ln);
}

TerrainRaycastResult TerrainLodNode::raycast(const Ray &ray, float maxDist) const
{
    TerrainRaycastResult r;
    if (!m_heightData) return r;
    float step = std::min(scale.x, scale.z) / (float)(std::max(m_hmW, m_hmH) - 1);
    for (float t = 0.f; t <= maxDist; t += step * 2.f)
    {
        glm::vec3 p = ray.origin + ray.direction * t;
        float       th = getHeightAt(p.x, p.z);
        if (t > 0.f && p.y <= th)
        {
            float tLo = t - step * 2.f, tHi = t;
            for (int i = 0; i < 8; ++i)
            {
                float tm = (tLo + tHi) * 0.5f;
                glm::vec3 pm = ray.origin + ray.direction * tm;
                if (pm.y <= getHeightAt(pm.x, pm.z)) tHi = tm;
                else                                  tLo = tm;
            }
            float tf  = (tLo + tHi) * 0.5f;
            glm::vec3 hit = ray.origin + ray.direction * tf;
            r.hit      = true;
            r.position = hit;
            r.normal   = getNormalAt(hit.x, hit.z);
            r.distance = tf;
            return r;
        }
    }
    return r;
}

void TerrainLodNode::gatherRenderItems(RenderQueue &q, const FrameContext &ctx)
{
    if (!visible || !m_material || m_patches.empty()) return;

    const glm::vec3 camPos  = ctx.camera->worldPosition();
    const glm::mat4 world   = worldMatrix();
    const Frustum  &frustum = ctx.frustum;

    for (Patch &p : m_patches)
    {
        BoundingBox worldAABB = p.aabb.transformed(world);
        if (!frustum.contains(worldAABB))
            continue;

        glm::vec3 wCenter = world * glm::vec4(p.center, 1.f);
        glm::vec3 diff    = wCenter - camPos;
        float distSq      = glm::dot(diff, diff);
        int   lod         = calcLOD(distSq);
        lod               = std::max(0, std::min(lod, m_maxLOD - 1));

        TerrainBuffer *buf = p.meshes[lod];
        if (!buf || buf->indices.empty()) continue;

        RenderItem item;
        item.drawable   = buf;
        item.material   = m_material;
        item.model      = world;
        item.worldAABB  = worldAABB;
        item.indexStart = 0;
        item.indexCount = (uint32_t)buf->indices.size();
        item.passMask   = RenderPassMask::Opaque;
        q.add(item);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  TiledTerrainNode
// ─────────────────────────────────────────────────────────────────────────────
TiledTerrainNode::TiledTerrainNode(int tilesInTextureSide, float patchLength,
                                   int tilesPerPatch, uint8_t defaultTile,
                                   const std::string &name)
    : m_tilesInSide(tilesInTextureSide)
    , m_patchLen(patchLength)
    , m_tilesPerPatch(tilesPerPatch)
    , m_defaultTile(defaultTile)
{
    this->name = name;
}

TiledTerrainNode::~TiledTerrainNode()
{
    for (auto &pb : m_patches)
        delete pb.mesh;
    delete[] m_tileMap;
}

void TiledTerrainNode::loadTilemap(Pixmap *img)
{
    if (!img || !img->IsValid()) return;
    std::vector<uint8_t> data((size_t)img->width * img->height);
    for (int z = 0; z < img->height; ++z)
    for (int x = 0; x < img->width;  ++x)
    {
        uint32_t p = img->GetPixel(x, z);
        data[(size_t)z * img->width + x] = (uint8_t)((p >> 24) & 0xFF);
    }
    loadTilemap((uint32_t)img->width, (uint32_t)img->height, data.data());
}

void TiledTerrainNode::loadTilemap(uint32_t w, uint32_t h, const uint8_t *data)
{
    delete[] m_tileMap;
    m_mapW    = w;
    m_mapH    = h;
    m_tileMap = new uint8_t[w * h];
    std::memcpy(m_tileMap, data, w * h);
    rebuildPatches();
}

void TiledTerrainNode::setTileAt(uint32_t x, uint32_t z, uint8_t tile)
{
    if (x < m_mapW && z < m_mapH)
        m_tileMap[z * m_mapW + x] = tile;
}

uint8_t TiledTerrainNode::getTileAt(uint32_t x, uint32_t z) const
{
    if (x < m_mapW && z < m_mapH)
        return m_tileMap[z * m_mapW + x];
    return m_defaultTile;
}

uint8_t TiledTerrainNode::getTileWrapped(int x, int z) const
{
    if (!m_tileMap) return m_defaultTile;
    x = ((x % (int)m_mapW) + (int)m_mapW) % (int)m_mapW;
    z = ((z % (int)m_mapH) + (int)m_mapH) % (int)m_mapH;
    return m_tileMap[z * m_mapW + x];
}

void TiledTerrainNode::getTileUVs(uint8_t tile, float stepUV, float uvs[4][2]) const
{
    int tx = tile % m_tilesInSide;
    int tz = tile / m_tilesInSide;
    float u0 = (float)tx * stepUV;
    float v0 = (float)tz * stepUV;
    float u1 = u0 + stepUV;
    float v1 = v0 + stepUV;
    uvs[0][0] = u0; uvs[0][1] = v0;
    uvs[1][0] = u1; uvs[1][1] = v0;
    uvs[2][0] = u0; uvs[2][1] = v1;
    uvs[3][0] = u1; uvs[3][1] = v1;
}

void TiledTerrainNode::rebuildPatches()
{
    for (auto &pb : m_patches) delete pb.mesh;
    m_patches.clear();

    if (!m_tileMap || m_mapW == 0 || m_mapH == 0) return;

    // Each patch covers m_tilesPerPatch × m_tilesPerPatch tiles
    int patchesX = (int)std::ceil((float)m_mapW / (float)m_tilesPerPatch);
    int patchesZ = (int)std::ceil((float)m_mapH / (float)m_tilesPerPatch);

    m_patches.reserve((size_t)patchesX * patchesZ);

    for (int pz = 0; pz < patchesZ; ++pz)
    for (int px = 0; px < patchesX; ++px)
    {
        PatchBuf pb;
        pb.patchX = px;
        pb.patchZ = pz;
        pb.mesh   = new TerrainBuffer();

        float wx0 = (float)px * m_tilesPerPatch * m_patchLen / (float)m_tilesPerPatch;
        float wz0 = (float)pz * m_tilesPerPatch * m_patchLen / (float)m_tilesPerPatch;
        float wx1 = wx0 + m_patchLen;
        float wz1 = wz0 + m_patchLen;
        pb.aabb   = {{wx0, -0.1f, wz0}, {wx1, 0.1f, wz1}};

        buildPatch(pb);
        pb.mesh->aabb = pb.aabb;
        pb.mesh->upload();
        m_patches.push_back(std::move(pb));
    }
}

void TiledTerrainNode::buildPatch(PatchBuf &pb)
{
    float stepUV    = 1.f / (float)m_tilesInSide;
    float tileWorld = m_patchLen / (float)m_tilesPerPatch;

    int tileOriginX = pb.patchX * m_tilesPerPatch;
    int tileOriginZ = pb.patchZ * m_tilesPerPatch;

    float patchWorldX = (float)tileOriginX * tileWorld;
    float patchWorldZ = (float)tileOriginZ * tileWorld;

    TerrainBuffer *buf = pb.mesh;
    buf->vertices.clear();
    buf->indices.clear();
    buf->vertices.reserve((size_t)m_tilesPerPatch * m_tilesPerPatch * 4);
    buf->indices.reserve ((size_t)m_tilesPerPatch * m_tilesPerPatch * 6);

    // One quad per tile — each quad has its own 4 vertices for correct UV per tile
    for (int tz = 0; tz < m_tilesPerPatch; ++tz)
    for (int tx = 0; tx < m_tilesPerPatch; ++tx)
    {
        uint8_t tile = getTileWrapped(tileOriginX + tx, tileOriginZ + tz);

        float uvs[4][2];
        getTileUVs(tile, stepUV, uvs);

        float x0 = patchWorldX + (float)tx       * tileWorld;
        float x1 = patchWorldX + (float)(tx + 1) * tileWorld;
        float z0 = patchWorldZ + (float)tz       * tileWorld;
        float z1 = patchWorldZ + (float)(tz + 1) * tileWorld;

        auto base = (uint32_t)buf->vertices.size();

        TerrainVertex v0, v1, v2, v3;
        glm::vec3 n = {0.f, 1.f, 0.f};

        // uv  = atlas coords (base tile texture)
        // uv2 = world-space tiling for detail (detail tiles across every tile)
        glm::vec2 det0 = {x0 * (float)m_tilesInSide, z0 * (float)m_tilesInSide};
        glm::vec2 det1 = {x1 * (float)m_tilesInSide, z0 * (float)m_tilesInSide};
        glm::vec2 det2 = {x0 * (float)m_tilesInSide, z1 * (float)m_tilesInSide};
        glm::vec2 det3 = {x1 * (float)m_tilesInSide, z1 * (float)m_tilesInSide};

        v0.position = {x0, 0, z0}; v0.normal = n; v0.uv = {uvs[0][0], uvs[0][1]}; v0.uv2 = det0;
        v1.position = {x1, 0, z0}; v1.normal = n; v1.uv = {uvs[1][0], uvs[1][1]}; v1.uv2 = det1;
        v2.position = {x0, 0, z1}; v2.normal = n; v2.uv = {uvs[2][0], uvs[2][1]}; v2.uv2 = det2;
        v3.position = {x1, 0, z1}; v3.normal = n; v3.uv = {uvs[3][0], uvs[3][1]}; v3.uv2 = det3;

        buf->vertices.push_back(v0);
        buf->vertices.push_back(v1);
        buf->vertices.push_back(v2);
        buf->vertices.push_back(v3);

        buf->indices.push_back(base + 0);
        buf->indices.push_back(base + 2);
        buf->indices.push_back(base + 1);
        buf->indices.push_back(base + 1);
        buf->indices.push_back(base + 2);
        buf->indices.push_back(base + 3);
    }
}

void TiledTerrainNode::gatherRenderItems(RenderQueue &q, const FrameContext &ctx)
{
    if (!visible || !m_material || m_patches.empty()) return;

    const glm::mat4 world   = worldMatrix();
    const Frustum  &frustum = ctx.frustum;

    for (auto &pb : m_patches)
    {
        BoundingBox worldAABB = pb.aabb.transformed(world);
        if (!frustum.contains(worldAABB))
            continue;

        RenderItem item;
        item.drawable   = pb.mesh;
        item.material   = m_material;
        item.model      = world;
        item.worldAABB  = worldAABB;
        item.indexStart = 0;
        item.indexCount = (uint32_t)pb.mesh->indices.size();
        item.passMask   = RenderPassMask::Opaque;
        q.add(item);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  InfiniteTerrainNode
// ─────────────────────────────────────────────────────────────────────────────
InfiniteTerrainNode::InfiniteTerrainNode(const std::string &name)
{
    this->name = name;
}

InfiniteTerrainNode::~InfiniteTerrainNode()
{
    for (auto &kv : m_cache)
        delete kv.second;
    delete[] m_base.heights;
}

bool InfiniteTerrainNode::loadBaseHeightmap(const std::string &path, float heightScale)
{
    Pixmap img;
    if (!img.Load(path.c_str()))
        return false;

    m_base.width  = (uint32_t)img.width;
    m_base.height = (uint32_t)img.height;
    m_base.scale  = heightScale;

    delete[] m_base.heights;
    m_base.heights = new float[(size_t)m_base.width * m_base.height];

    for (int z = 0; z < img.height; ++z)
    for (int x = 0; x < img.width;  ++x)
    {
        uint32_t pixel = img.GetPixel(x, z);
        uint8_t  r     = (pixel >> 24) & 0xFF;
        m_base.heights[(size_t)z * m_base.width + x] = (float)r / 255.f;
    }

    return true;
}

void InfiniteTerrainNode::configure(int patchesVisible, int vertsPerPatch, float patchWorldSize)
{
    m_visibleHalf  = patchesVisible;
    m_vertsPerPatch = vertsPerPatch;
    m_patchWorld    = patchWorldSize;
}

long long InfiniteTerrainNode::patchKey(int px, int pz) const
{
    return ((long long)px << 32) | (uint32_t)pz;
}

float InfiniteTerrainNode::sampleBase(float u, float v) const
{
    if (!m_base.heights) return 0.f;

    // Wrap UVs for tiling
    u = u - std::floor(u);
    v = v - std::floor(v);

    float fx = u * (float)(m_base.width  - 1);
    float fz = v * (float)(m_base.height - 1);
    int x0 = (int)fx, z0 = (int)fz;
    float tx = fx - x0, tz = fz - z0;

    int x1 = (x0 + 1) % (int)m_base.width;
    int z1 = (z0 + 1) % (int)m_base.height;

    float h00 = m_base.heights[(size_t)z0 * m_base.width + x0];
    float h10 = m_base.heights[(size_t)z0 * m_base.width + x1];
    float h01 = m_base.heights[(size_t)z1 * m_base.width + x0];
    float h11 = m_base.heights[(size_t)z1 * m_base.width + x1];

    return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz) * m_base.scale;
}

glm::vec3 InfiniteTerrainNode::calcNormal(float u, float v) const
{
    float du = 1.f / (float)(m_base.width  - 1);
    float dv = 1.f / (float)(m_base.height - 1);
    float hL = sampleBase(u - du, v);
    float hR = sampleBase(u + du, v);
    float hD = sampleBase(u, v - dv);
    float hU = sampleBase(u, v + dv);
    return glm::normalize(glm::vec3((hL - hR) / (2.f * m_patchWorld * du * (m_base.width - 1)),
                                    1.f,
                                    (hD - hU) / (2.f * m_patchWorld * dv * (m_base.height - 1))));
}

int InfiniteTerrainNode::calcLOD(float distSq) const
{
    float d0 = m_patchWorld * 2.f;
    for (int i = 0; i < MAX_LOD - 1; ++i)
    {
        float dt = d0 * (float)(1 << i);
        if (distSq < dt * dt)
            return i;
    }
    return MAX_LOD - 1;
}

InfiniteTerrainNode::PatchMesh *InfiniteTerrainNode::getOrCreate(int px, int pz, int lod)
{
    long long key   = patchKey(px, pz);
    auto      it    = m_cache.find(key);
    PatchMesh *patch = (it != m_cache.end()) ? it->second : nullptr;

    if (!patch)
    {
        patch = new PatchMesh();
        m_cache[key] = patch;
    }

    patch->lastFrame = m_frame;

    if (!patch->meshes[lod])
    {
        patch->meshes[lod] = new TerrainBuffer();
        buildMesh(px, pz, lod, patch);
        patch->meshes[lod]->aabb = patch->aabb;
        patch->meshes[lod]->upload();
    }

    return patch;
}

void InfiniteTerrainNode::buildMesh(int px, int pz, int lod, PatchMesh *patch)
{
    int    stride     = 1 << lod;
    int    verts      = m_vertsPerPatch;
    float  worldX0    = (float)px * m_patchWorld;
    float  worldZ0    = (float)pz * m_patchWorld;

    // Determine vertex positions
    std::vector<int> xIdx, zIdx;
    for (int i = 0; i < verts; i += stride) xIdx.push_back(i);
    if (xIdx.empty() || xIdx.back() != verts - 1) xIdx.push_back(verts - 1);
    if (zIdx.empty() || zIdx.back() != verts - 1) zIdx.push_back(verts - 1);
    for (int i = 0; i < verts; i += stride) zIdx.push_back(i);
    if (zIdx.empty() || zIdx.back() != verts - 1) zIdx.push_back(verts - 1);

    int vcX = (int)xIdx.size();
    int vcZ = (int)zIdx.size();

    TerrainBuffer *buf = patch->meshes[lod];
    buf->vertices.reserve((size_t)vcX * vcZ);
    buf->indices.reserve ((size_t)(vcX-1) * (vcZ-1) * 6);

    BoundingBox &aabb = patch->aabb;
    aabb = BoundingBox{};

    float frac = 1.f / (float)(verts - 1);

    for (int iz = 0; iz < vcZ; ++iz)
    for (int ix = 0; ix < vcX; ++ix)
    {
        float localX = (float)xIdx[ix] * frac;
        float localZ = (float)zIdx[iz] * frac;

        float wx = worldX0 + localX * m_patchWorld;
        float wz = worldZ0 + localZ * m_patchWorld;

        // Map world to base heightmap UV
        float totalSize = (float)m_patchWorld * (float)(2 * m_visibleHalf + 1) * 16.f; // assume 16 period
        float u = wx / totalSize;
        float v = wz / totalSize;

        float h = sampleBase(u, v);
        glm::vec3 n = calcNormal(u, v);

        TerrainVertex vert;
        vert.position = {wx, h, wz};
        vert.normal   = n;
        vert.uv       = {u, v};
        vert.uv2      = {wx / m_patchWorld, wz / m_patchWorld};  // detail: one repeat per patch

        aabb.expand(vert.position);
        buf->vertices.push_back(vert);
    }

    for (int iz = 0; iz < vcZ - 1; ++iz)
    for (int ix = 0; ix < vcX - 1; ++ix)
    {
        uint32_t tl = (uint32_t)(iz     * vcX + ix);
        uint32_t tr = (uint32_t)(iz     * vcX + ix + 1);
        uint32_t bl = (uint32_t)((iz+1) * vcX + ix);
        uint32_t br = (uint32_t)((iz+1) * vcX + ix + 1);

        buf->indices.push_back(tl);
        buf->indices.push_back(bl);
        buf->indices.push_back(tr);
        buf->indices.push_back(tr);
        buf->indices.push_back(bl);
        buf->indices.push_back(br);
    }
}

void InfiniteTerrainNode::evictOld()
{
    if ((int)m_cache.size() <= MAX_CACHED) return;

    // Collect all keys sorted by last access frame
    std::vector<std::pair<uint32_t, long long>> items;
    items.reserve(m_cache.size());
    for (const auto &kv : m_cache)
        items.push_back({kv.second->lastFrame, kv.first});

    std::sort(items.begin(), items.end());

    int toRemove = (int)m_cache.size() - MAX_CACHED / 2;
    for (int i = 0; i < toRemove && i < (int)items.size(); ++i)
    {
        delete m_cache[items[i].second];
        m_cache.erase(items[i].second);
    }
}

float InfiniteTerrainNode::getHeightAt(float worldX, float worldZ) const
{
    if (!m_base.heights) return 0.f;

    float totalSize = m_patchWorld * (float)(2 * m_visibleHalf + 1) * 16.f;
    float u = worldX / totalSize;
    float v = worldZ / totalSize;
    return worldMatrix()[3][1] + sampleBase(u, v);
}

glm::vec3 InfiniteTerrainNode::getNormalAt(float worldX, float worldZ) const
{
    if (!m_base.heights) return {0,1,0};
    float totalSize = m_patchWorld * (float)(2 * m_visibleHalf + 1) * 16.f;
    glm::vec3 n = calcNormal(worldX / totalSize, worldZ / totalSize);
    glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(worldMatrix())));
    return glm::normalize(nm * n);
}

void InfiniteTerrainNode::gatherRenderItems(RenderQueue &q, const FrameContext &ctx)
{
    if (!visible || !m_material || !m_base.heights) return;

    ++m_frame;

    const glm::vec3 camPos  = ctx.camera->worldPosition();
    const Frustum  &frustum = ctx.frustum;
    const glm::mat4 world   = worldMatrix();

    int centerPX = (int)std::floor(camPos.x / m_patchWorld);
    int centerPZ = (int)std::floor(camPos.z / m_patchWorld);

    int half = m_visibleHalf;

    for (int pz = centerPZ - half; pz <= centerPZ + half; ++pz)
    for (int px = centerPX - half; px <= centerPX + half; ++px)
    {
        float wpx  = (float)px * m_patchWorld + m_patchWorld * 0.5f;
        float wpz  = (float)pz * m_patchWorld + m_patchWorld * 0.5f;
        float dx   = wpx - camPos.x;
        float dz   = wpz - camPos.z;
        float distSq = dx*dx + dz*dz;

        int lod = calcLOD(distSq);

        PatchMesh *patch = getOrCreate(px, pz, lod);
        if (!patch) continue;

        TerrainBuffer *buf   = patch->meshes[lod];
        if (!buf || buf->indices.empty()) continue;

        BoundingBox worldAABB = patch->aabb.transformed(world);
        if (!frustum.contains(worldAABB))
            continue;

        RenderItem item;
        item.drawable   = buf;
        item.material   = m_material;
        item.model      = world;
        item.worldAABB  = worldAABB;
        item.indexStart = 0;
        item.indexCount = (uint32_t)buf->indices.size();
        item.passMask   = RenderPassMask::Opaque;
        q.add(item);
    }

    evictOld();
}
