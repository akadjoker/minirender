#include "TerrainNode.hpp"
#include "Pixmap.hpp"
#include "RenderPipeline.hpp"
#include "Batch.hpp"
#include "Math.hpp"
#include "Utils.hpp"
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
    {
        SDL_Log("[TerrainNode] Failed to load heightmap: %s", path.c_str());
        return false;
    }

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
        uint8_t  r     = pixel & 0xFF;   // Pixmap: R is in low byte
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
        // uv: texScaleU=1 → texture tiles once across full terrain
        //     texScaleU=8 → texture repeats 8× across the terrain
        v.uv  = {(float)wx * invW * texScaleU,
                 (float)wz * invH * texScaleV};
        // uv2: world-space detail tiling independent of heightmap resolution
        v.uv2 = {px / m_terrainScale.x * detailScale,
                 pz / m_terrainScale.z * detailScale};

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

    for (size_t i = 0; i < m_blocks.size(); ++i)
    {
        TerrainBuffer *buf = m_blocks[i];
        BoundingBox worldAABB = m_blockAABBs[i].transformed(world);

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
//  TerrainLodNode  — GeoMipMap LOD terrain
//  Ported from Irrlicht CTerrainSceneNode (Nikolaus Gebhardt) 
//  Architecture: single shared VBO (static) + dynamic IBO rebuilt each frame.
// ─────────────────────────────────────────────────────────────────────────────
TerrainLodNode::TerrainLodNode(const std::string &name, int maxLOD,
                               TerrainPatchSize patchSz, float detailScale)
    : m_patchSize   (static_cast<int>(patchSz))
    , m_calcPatchSize(static_cast<int>(patchSz) - 1)
    , m_maxLOD      (maxLOD)
    , m_detailScale (detailScale)
{
    this->name = name;
}

TerrainLodNode::~TerrainLodNode()
{
    delete m_renderBuffer;
    delete[] m_heightData;
}

// ── Load ─────────────────────────────────────────────────────────────────────
bool TerrainLodNode::loadFromHeightmap(const std::string &path,
                                       float heightScale,
                                       int   smoothFactor)
{
    Pixmap img;
    if (!img.Load(path.c_str()))
        return false;

    m_size        = img.width;   // assume square
    m_heightScale = heightScale;

    
    switch (m_patchSize)
    {
        case  9: m_maxLOD = std::min(m_maxLOD, 3); break;
        case 17: m_maxLOD = std::min(m_maxLOD, 4); break;
        case 33: m_maxLOD = std::min(m_maxLOD, 5); break;
        case 65: m_maxLOD = std::min(m_maxLOD, 6); break;
        default: m_maxLOD = std::min(m_maxLOD, 7); break;
    }

    // Store raw height data
    delete[] m_heightData;
    m_heightData = new float[m_size * m_size];

    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        uint32_t pixel = img.GetPixel(x, z);
        uint8_t  r     = pixel & 0xFF;
        m_heightData[z * m_size + x] = (float)r / 255.f;
    }

    if (smoothFactor > 0)
        smooth(smoothFactor);

    // Build source vertex buffer — positions in normalised [0,1] space.
    // Heights are raw [0,1] * heightScale (no world scale yet).
    const float tdSize = 1.f / (float)(m_size - 1);
    m_sourceVerts.resize((size_t)m_size * m_size);

    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        float fx = (float)x * tdSize;
        float fz = (float)z * tdSize;
        float h  = m_heightData[z * m_size + x] * m_heightScale;

        TerrainVertex &v = m_sourceVerts[(size_t)z * m_size + x];   // row-major: [z*size+x]
        v.position = { fx, h, fz };        // normalised X/Z, unscaled Y
        v.normal   = { 0.f, 1.f, 0.f };
        v.uv       = { fx * m_texScale,    fz * m_texScale  };
        v.uv2      = { fx * m_detailScale, fz * m_detailScale };
    }

    // Allocate render buffer
    delete m_renderBuffer;
    m_renderBuffer         = new TerrainBuffer();
    m_renderBuffer->vertices = m_sourceVerts;   // copy; positions will be baked below

    // Bake scale+position NOW (uses current Node3D scale/position)
    applyTransformation();   // also calculateNormals, update GPU VBO, calculatePatchData

    // Pre-allocate dynamic IBO at worst-case size
    const size_t maxIndices = (size_t)m_patchCount * m_patchCount *
                              m_calcPatchSize      * m_calcPatchSize * 6;
    m_renderBuffer->indices.resize(maxIndices);
    m_renderBuffer->allocateDynamicIndices(maxIndices);

    // First frame index build
    m_forceRecalc = true;

    SDL_Log("[TerrainLodNode] Loaded: size=%d, patches=%dx%d, maxLOD=%d",
            m_size, m_patchCount, m_patchCount, m_maxLOD);
    return true;
}


void TerrainLodNode::smooth(int iterations)
{
    for (int iter = 0; iter < iterations; ++iter)
    {
        std::vector<float> tmp((size_t)m_size * m_size);
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
        // Copy smoothed interior, keep borders
        for (int y = 1; y < m_size - 1; ++y)
        for (int x = 1; x < m_size - 1; ++x)
            m_heightData[x + y * m_size] = tmp[x + y * m_size];
    }
}

// ── Rebake render buffer from m_heightData ────────────────────────────────────
// Called after any height edit: syncs m_sourceVerts Y, then re-bakes world
// positions, normals, uploads VBO, and recalculates patch data.
void TerrainLodNode::rebakeFromHeightData()
{
    if (m_sourceVerts.empty() || !m_heightData) return;

    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        size_t idx = (size_t)z * m_size + x;
        m_sourceVerts[idx].position.y = m_heightData[idx] * m_heightScale;
    }

    applyTransformation();   // bakes world pos, normals, GPU upload, patches
    m_forceRecalc = true;
}

// ── Smooth area (local gaussian-blend, world-space radius) ───────────────────
void TerrainLodNode::smoothArea(float worldX, float worldZ, float radius, int iterations)
{
    if (!m_heightData || m_size <= 0 || iterations <= 0) return;

    const float cx  = (worldX - position.x) / scale.x * (float)(m_size - 1);
    const float cz  = (worldZ - position.z) / scale.z * (float)(m_size - 1);
    const float rx  = radius / scale.x * (float)(m_size - 1);
    const float rz  = radius / scale.z * (float)(m_size - 1);
    const float mxR = std::max(rx, rz);

    const int minX = std::max(1,           (int)(cx - mxR));
    const int maxX = std::min(m_size - 2,  (int)(cx + mxR));
    const int minZ = std::max(1,           (int)(cz - mxR));
    const int maxZ = std::min(m_size - 2,  (int)(cz + mxR));

    std::vector<float> h(m_heightData, m_heightData + (size_t)m_size * m_size);

    for (int iter = 0; iter < iterations; ++iter)
    {
        std::vector<float> smoothed = h;
        for (int z = minZ; z <= maxZ; ++z)
        for (int x = minX; x <= maxX; ++x)
        {
            const float dx = (x - cx) / rx;
            const float dz = (z - cz) / rz;
            const float ds = dx * dx + dz * dz;
            if (ds > 1.f) continue;

            const int i = z * m_size + x;
            const float avg = (h[i]           * 4.f
                             + h[i - 1]       * 2.f + h[i + 1]       * 2.f
                             + h[i - m_size]  * 2.f + h[i + m_size]  * 2.f
                             + h[i - m_size - 1]    + h[i - m_size + 1]
                             + h[i + m_size - 1]    + h[i + m_size + 1]) / 16.f;

            const float blend = 1.f - ds;
            smoothed[i] = h[i] + (avg - h[i]) * blend;
        }
        h = smoothed;
    }

    for (int z = minZ; z <= maxZ; ++z)
    for (int x = minX; x <= maxX; ++x)
    {
        const float dx = (x - cx) / rx;
        const float dz = (z - cz) / rz;
        if (dx * dx + dz * dz <= 1.f)
            m_heightData[z * m_size + x] = h[z * m_size + x];
    }

    rebakeFromHeightData();
}

// ── Set absolute world-Y height (linear falloff from centre) ─────────────────
void TerrainLodNode::setHeight(float worldX, float worldZ, float newHeight, float radius)
{
    if (!m_heightData || m_size <= 0) return;

    const float cx  = (worldX - position.x) / scale.x * (float)(m_size - 1);
    const float cz  = (worldZ - position.z) / scale.z * (float)(m_size - 1);
    const float rx  = radius / scale.x * (float)(m_size - 1);
    const float rz  = radius / scale.z * (float)(m_size - 1);
    const float mxR = std::max(rx, rz);

    
    const float rawTarget = std::max(0.f, std::min(1.f,
        (newHeight - position.y) / (m_heightScale * scale.y)));

    const int minX = std::max(0,           (int)(cx - mxR));
    const int maxX = std::min(m_size - 1,  (int)(cx + mxR));
    const int minZ = std::max(0,           (int)(cz - mxR));
    const int maxZ = std::min(m_size - 1,  (int)(cz + mxR));

    for (int z = minZ; z <= maxZ; ++z)
    for (int x = minX; x <= maxX; ++x)
    {
        const float dx = (x - cx) / rx;
        const float dz = (z - cz) / rz;
        const float ds = dx * dx + dz * dz;
        if (ds > 1.f) continue;

        const float weight = 1.f - ds;   // linear falloff
        const int i = z * m_size + x;
        m_heightData[i] = std::max(0.f, std::min(1.f,
            m_heightData[i] + (rawTarget - m_heightData[i]) * weight));
    }

    rebakeFromHeightData();
}

// ── Raise / lower height by delta (cosine falloff, world units) ───────────────
void TerrainLodNode::modifyHeight(float worldX, float worldZ, float deltaHeight, float radius)
{
    if (!m_heightData || m_size <= 0) return;

    const float cx  = (worldX - position.x) / scale.x * (float)(m_size - 1);
    const float cz  = (worldZ - position.z) / scale.z * (float)(m_size - 1);
    const float rx  = radius / scale.x * (float)(m_size - 1);
    const float rz  = radius / scale.z * (float)(m_size - 1);
    const float mxR = std::max(rx, rz);

    // Convert world-unit delta → raw
    const float rawDelta = deltaHeight / (m_heightScale * scale.y);

    const int minX = std::max(0,           (int)(cx - mxR));
    const int maxX = std::min(m_size - 1,  (int)(cx + mxR));
    const int minZ = std::max(0,           (int)(cz - mxR));
    const int maxZ = std::min(m_size - 1,  (int)(cz + mxR));

    for (int z = minZ; z <= maxZ; ++z)
    for (int x = minX; x <= maxX; ++x)
    {
        const float dx = (x - cx) / rx;
        const float dz = (z - cz) / rz;
        const float ds = dx * dx + dz * dz;
        if (ds > 1.f) continue;

        const float weight = std::cos(ds * 3.14159265f * 0.5f);   // cosine falloff
        const int i = z * m_size + x;
        m_heightData[i] = std::max(0.f, std::min(1.f,
            m_heightData[i] + rawDelta * weight));
    }

    rebakeFromHeightData();
}

// ── Flatten toward target height (smooth blend to flat plane) ─────────────────
void TerrainLodNode::flatten(float worldX, float worldZ,
                             float targetHeight, float radius, float strength)
{
    if (!m_heightData || m_size <= 0) return;

    const float cx  = (worldX - position.x) / scale.x * (float)(m_size - 1);
    const float cz  = (worldZ - position.z) / scale.z * (float)(m_size - 1);
    const float rx  = radius / scale.x * (float)(m_size - 1);
    const float rz  = radius / scale.z * (float)(m_size - 1);
    const float mxR = std::max(rx, rz);

    const float rawTarget = std::max(0.f, std::min(1.f,
        (targetHeight - position.y) / (m_heightScale * scale.y)));

    const int minX = std::max(0,           (int)(cx - mxR));
    const int maxX = std::min(m_size - 1,  (int)(cx + mxR));
    const int minZ = std::max(0,           (int)(cz - mxR));
    const int maxZ = std::min(m_size - 1,  (int)(cz + mxR));

    for (int z = minZ; z <= maxZ; ++z)
    for (int x = minX; x <= maxX; ++x)
    {
        const float dx = (x - cx) / rx;
        const float dz = (z - cz) / rz;
        const float ds = dx * dx + dz * dz;
        if (ds > 1.f) continue;

        const float weight = (1.f - ds) * strength;
        const int i = z * m_size + x;
        m_heightData[i] = std::max(0.f, std::min(1.f,
            m_heightData[i] + (rawTarget - m_heightData[i]) * weight));
    }

    rebakeFromHeightData();
}

// ── Normals (central differences on baked world-space positions) ──────────────
void TerrainLodNode::calculateNormals()
{
    auto posAt = [&](int x, int z) -> glm::vec3 {
        x = std::max(0, std::min(x, m_size - 1));
        z = std::max(0, std::min(z, m_size - 1));
        return m_renderBuffer->vertices[(size_t)z * m_size + x].position;  // row-major [z*size+x]
    };

    for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
    {
        glm::vec3 L = posAt(x - 1, z);
        glm::vec3 R = posAt(x + 1, z);
        glm::vec3 D = posAt(x, z - 1);
        glm::vec3 U = posAt(x, z + 1);
        // cross(U-D, R-L) => cross(+Z, +X) = +Y  (normals point up)
        glm::vec3 n = glm::cross(U - D, R - L);
        float len = glm::length(n);
        m_renderBuffer->vertices[(size_t)z * m_size + x].normal =
            (len > 1e-6f) ? n / len : glm::vec3(0, 1, 0);
    }
}

// ── ApplyTransformation — re-bake world positions from source verts ──────────
void TerrainLodNode::applyTransformation()
{
    if (m_sourceVerts.empty()) return;

    const glm::vec3 pos = position;   // Node3D::position
    const glm::vec3 scl = scale;      // Node3D::scale

    const size_t count = m_sourceVerts.size();
    m_renderBuffer->vertices.resize(count);

    for (size_t i = 0; i < count; ++i)
    {
        const TerrainVertex &src = m_sourceVerts[i];
        TerrainVertex       &dst = m_renderBuffer->vertices[i];
        dst.position = src.position * scl + pos;
        dst.uv       = src.uv;
        dst.uv2      = src.uv2;
        // normal will be set in calculateNormals
    }

    calculateNormals();

    if (m_renderBuffer->vbo != 0)
        m_renderBuffer->update();   // re-upload VBO

    calculateDistanceThresholds(true);
    createPatches();
    calculatePatchData();
}


void TerrainLodNode::calculateDistanceThresholds(bool /*scaleChanged*/)
{
    m_lodDist.resize(m_maxLOD);

    const float normPatch = (float)m_calcPatchSize / (float)(m_size - 1);
    const float patchSzX  = normPatch * scale.x;
    const float patchSzZ  = normPatch * scale.z;
    const float diagonal  = std::sqrt(patchSzX * patchSzX + patchSzZ * patchSzZ);

    for (int i = 0; i < m_maxLOD; ++i)
    {
        float d = diagonal * std::pow(2.f, (float)i);
        m_lodDist[i] = d * d;
    }
}

// ── Create patch array ────────────────────────────────────────────────────────
void TerrainLodNode::createPatches()
{
    m_patchCount = (m_size - 1) / m_calcPatchSize;
    const int total = m_patchCount * m_patchCount;
    m_patches.assign(total, Patch{});
    SDL_Log("[TerrainLodNode] Patches: %dx%d = %d", m_patchCount, m_patchCount, total);
}

// ── Compute per-patch AABB, centre, neighbour pointers ────────────────────────
void TerrainLodNode::calculatePatchData()
{
    if (!m_renderBuffer || m_patches.empty()) return;

    m_aabb = BoundingBox{};

    for (int px = 0; px < m_patchCount; ++px)
    for (int pz = 0; pz < m_patchCount; ++pz)
    {
        Patch &p = m_patches[(size_t)px * m_patchCount + pz];
        p.aabb    = BoundingBox{};

        const int xStart = pz * m_calcPatchSize;   // col (X) range: driven by pz to match getIndex(patchX=pz)
        const int zStart = px * m_calcPatchSize;   // row (Z) range: driven by px to match getIndex(patchZ=px)
        const int xEnd   = xStart + m_calcPatchSize;
        const int zEnd   = zStart + m_calcPatchSize;

        bool first = true;
        for (int x = xStart; x <= xEnd; ++x)
        for (int z = zStart; z <= zEnd; ++z)
        {
            const glm::vec3 &pos =
                m_renderBuffer->vertices[(size_t)z * m_size + x].position;  // row-major [z*size+x]
            if (first) { p.aabb.min = p.aabb.max = pos; first = false; }
            else        p.aabb.expand(pos);
        }

        p.center = p.aabb.center();

        // Assign neighbour pointers (for T-junction stitching)
        p.top    = (px > 0)                  ? &m_patches[(size_t)(px-1)*m_patchCount+pz] : nullptr;
        p.bottom = (px < m_patchCount - 1)   ? &m_patches[(size_t)(px+1)*m_patchCount+pz] : nullptr;
        p.left   = (pz > 0)                  ? &m_patches[(size_t)px*m_patchCount+(pz-1)] : nullptr;
        p.right  = (pz < m_patchCount - 1)   ? &m_patches[(size_t)px*m_patchCount+(pz+1)] : nullptr;

        m_aabb.expand(p.aabb.min);
        m_aabb.expand(p.aabb.max);
    }
}


bool TerrainLodNode::preRenderLODCalculations(const glm::vec3 &camPos,
                                              const glm::vec3 &camRot,
                                              const Frustum   &frustum)
{
    if (!m_forceRecalc)
    {
        glm::vec3 dPos   = camPos - m_oldCamPos;
        float     movedSq    = glm::dot(dPos, dPos);
        float     dotForward = glm::dot(camRot, m_oldCamForward);  // camRot is fwd unit vector
        if (movedSq < m_camMoveDelta * m_camMoveDelta &&
            dotForward > m_camRotDelta)
            return false;   // camera hasn't moved/rotated enough — skip recalc
    }

    m_oldCamPos     = camPos;
    m_oldCamForward = camRot;   // store new forward
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
        if (m_patches[patchIdx].currentLOD < 0) continue;

        const int step = 1 << m_patches[patchIdx].currentLOD;
        int x = 0, z = 0;

        while (z < m_calcPatchSize)
        {
            uint32_t i11 = getIndex(j, i, patchIdx, (uint32_t)x,        (uint32_t)z);
            uint32_t i21 = getIndex(j, i, patchIdx, (uint32_t)(x+step), (uint32_t)z);
            uint32_t i12 = getIndex(j, i, patchIdx, (uint32_t)x,        (uint32_t)(z+step));
            uint32_t i22 = getIndex(j, i, patchIdx, (uint32_t)(x+step), (uint32_t)(z+step));

            // CCW from above (+Y normal):  i11(x,z) → i12(x,z+s) → i22(x+s,z+s)
            indices[m_indicesToRender++] = i11;
            indices[m_indicesToRender++] = i12;
            indices[m_indicesToRender++] = i22;
            indices[m_indicesToRender++] = i11;
            indices[m_indicesToRender++] = i22;
            indices[m_indicesToRender++] = i21;

            x += step;
            if (x >= m_calcPatchSize) { x = 0; z += step; }
        }
    }

    m_renderBuffer->updateIndices(m_indicesToRender);
}

// ── T-junction-safe index lookup (direct port from Irrlicht ) ───
uint32_t TerrainLodNode::getIndex(int patchX, int patchZ, int patchIdx,
                                  uint32_t vX, uint32_t vZ) const
{
    const Patch &p = m_patches[patchIdx];
    const uint32_t calcSz = (uint32_t)m_calcPatchSize;

    // top border
    if (vZ == 0 && p.top && p.top->currentLOD >= 0 &&
        p.currentLOD < p.top->currentLOD &&
        (vX % (1u << p.top->currentLOD)) != 0)
        vX -= vX % (1u << p.top->currentLOD);

    // bottom border
    else if (vZ == calcSz && p.bottom && p.bottom->currentLOD >= 0 &&
             p.currentLOD < p.bottom->currentLOD &&
             (vX % (1u << p.bottom->currentLOD)) != 0)
        vX -= vX % (1u << p.bottom->currentLOD);

    // left border
    if (vX == 0 && p.left && p.left->currentLOD >= 0 &&
        p.currentLOD < p.left->currentLOD &&
        (vZ % (1u << p.left->currentLOD)) != 0)
        vZ -= vZ % (1u << p.left->currentLOD);

    // right border
    else if (vX == calcSz && p.right && p.right->currentLOD >= 0 &&
             p.currentLOD < p.right->currentLOD &&
             (vZ % (1u << p.right->currentLOD)) != 0)
        vZ -= vZ % (1u << p.right->currentLOD);

    if (vZ >= (uint32_t)m_patchSize) vZ = calcSz;
    if (vX >= (uint32_t)m_patchSize) vX = calcSz;

    return (vZ + (uint32_t)(m_calcPatchSize * patchZ)) * (uint32_t)m_size +
           (vX + (uint32_t)(m_calcPatchSize * patchX));
}

// ── setLODDistance ────────────────────────────────────────────────────────────
void TerrainLodNode::setLODDistance(int lod, float dist)
{
    if (lod >= 0 && lod < (int)m_lodDist.size())
        m_lodDist[lod] = dist * dist;
}

// ── Height / normal queries (world space) ────────────────────────────────────
float TerrainLodNode::sampleHeight(int x, int z) const
{
    x = std::max(0, std::min(x, m_size - 1));
    z = std::max(0, std::min(z, m_size - 1));
    return m_heightData[z * m_size + x];
}

float TerrainLodNode::getHeightAt(float worldX, float worldZ) const
{
    if (!m_heightData || !m_renderBuffer) return 0.f;

    // Map world → normalised [0,1]
    float lx = (worldX - position.x) / scale.x;
    float lz = (worldZ - position.z) / scale.z;
    lx = std::max(0.f, std::min(1.f, lx));
    lz = std::max(0.f, std::min(1.f, lz));

    float gx = lx * (float)(m_size - 1);
    float gz = lz * (float)(m_size - 1);
    int   ix = std::min((int)gx, m_size - 2);
    int   iz = std::min((int)gz, m_size - 2);
    float fx = gx - (float)ix;
    float fz = gz - (float)iz;

    // Bilinear from baked (world-space) Y values
    auto wy = [&](int x, int z) {
        return m_renderBuffer->vertices[(size_t)z * m_size + x].position.y;  // row-major
    };
    return wy(ix,iz)*(1-fx)*(1-fz) + wy(ix+1,iz)*fx*(1-fz) +
           wy(ix,iz+1)*(1-fx)*fz   + wy(ix+1,iz+1)*fx*fz;
}

glm::vec3 TerrainLodNode::calcNormal(int x, int z) const
{
    // For internal normal math (used only if needed elsewhere)
    float hL = sampleHeight(x-1, z) * m_heightScale * scale.y;
    float hR = sampleHeight(x+1, z) * m_heightScale * scale.y;
    float hD = sampleHeight(x, z-1) * m_heightScale * scale.y;
    float hU = sampleHeight(x, z+1) * m_heightScale * scale.y;
    float sx  = scale.x / (float)(m_size - 1);
    float sz  = scale.z / (float)(m_size - 1);
    return glm::normalize(glm::vec3((hL - hR) / (2.f * sx), 1.f, (hD - hU) / (2.f * sz)));
}

glm::vec3 TerrainLodNode::getNormalAt(float worldX, float worldZ) const
{
    if (!m_heightData || !m_renderBuffer) return {0,1,0};

    float lx = (worldX - position.x) / scale.x;
    float lz = (worldZ - position.z) / scale.z;
    float gx = std::max(0.f, std::min(1.f, lx)) * (float)(m_size - 1);
    float gz = std::max(0.f, std::min(1.f, lz)) * (float)(m_size - 1);
    int   ix = (int)std::round(gx);
    int   iz = (int)std::round(gz);
    ix = std::max(0, std::min(ix, m_size - 1));
    iz = std::max(0, std::min(iz, m_size - 1));
    return glm::normalize(m_renderBuffer->vertices[(size_t)iz * m_size + ix].normal);  // row-major
}

TerrainRaycastResult TerrainLodNode::raycast(const Ray &ray, float maxDist) const
{
    TerrainRaycastResult r;
    if (!m_heightData) return r;

    float cellSz = std::min(scale.x, scale.z) / (float)(m_size - 1);
    for (float t = 0.f; t <= maxDist; t += cellSz * 2.f)
    {
        glm::vec3 p = ray.origin + ray.direction * t;
        float     th = getHeightAt(p.x, p.z);
        if (t > 0.f && p.y <= th)
        {
            float lo = t - cellSz * 2.f, hi = t;
            for (int i = 0; i < 8; ++i)
            {
                float tm = (lo + hi) * 0.5f;
                glm::vec3 pm = ray.origin + ray.direction * tm;
                if (pm.y <= getHeightAt(pm.x, pm.z)) hi = tm;
                else                                  lo = tm;
            }
            float tf    = (lo + hi) * 0.5f;
            glm::vec3 h = ray.origin + ray.direction * tf;
            r.hit      = true;
            r.position = h;
            r.normal   = getNormalAt(h.x, h.z);
            r.distance = tf;
            return r;
        }
    }
    return r;
}

// ── Debug: draw patch AABBs coloured by LOD ─────────────────────────────────
void TerrainLodNode::debug(RenderBatch *batch) const
{
    if (!batch || m_patches.empty() || !debugDraw) return;

    // Overall terrain AABB — white
    batch->SetColor(255, 255, 255, 180);
    batch->Box(m_aabb);

    // Per-patch AABB coloured by LOD level (matches tmpo/Terrain.cpp colour scheme)
    const int count = (int)m_patches.size();
    for (int i = 0; i < count; ++i)
    {
        const Patch &p = m_patches[i];
        switch (p.currentLOD)
        {
            case  0: batch->SetColor(  0, 255,   0, 200); break;  // green   — max detail
            case  1: batch->SetColor(255, 255,   0, 200); break;  // yellow
            case  2: batch->SetColor(255, 165,   0, 200); break;  // orange
            case  3: batch->SetColor(128,   0, 128, 200); break;  // purple
            case  4: batch->SetColor(255,   0,   0, 200); break;  // red
            default:
                if (p.currentLOD > 4)
                    batch->SetColor(200,   0,   0, 200);           // dark red — very low detail
                else
                    batch->SetColor(100, 100, 100, 120);           // grey — frustum-culled
                break;
        }
        batch->Box(p.aabb);
    }
}

// ── gatherRenderItems ─────────────────────────────────────────────────────────
void TerrainLodNode::gatherRenderItems(RenderQueue &q, const FrameContext &ctx)
{
    if (!visible || !m_material || !m_renderBuffer || m_patches.empty()) return;

    const glm::vec3 camPos = ctx.camera->worldPosition();
    // Extract forward direction from view matrix (row 2 = -forward in GLM column-major)
    const glm::mat4 &view  = ctx.camera->view;
    const glm::vec3 camFwd = glm::normalize(glm::vec3(-view[0][2], -view[1][2], -view[2][2]));

    bool changed = preRenderLODCalculations(camPos, camFwd, ctx.frustum);
    if (changed)
        preRenderIndicesCalculations();

    if (m_indicesToRender == 0) return;

    RenderItem item;
    item.drawable   = m_renderBuffer;
    item.material   = m_material;
    item.model      = glm::mat4(1.f);   // vertices are already in world space
    item.worldAABB  = m_aabb;
    item.indexStart = 0;
    item.indexCount = m_indicesToRender;
    item.passMask   = RenderPassMask::Opaque;
    q.add(item);
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
