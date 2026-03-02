#pragma once
#include "Node.hpp"
#include "RenderPipeline.hpp"
#include "Pixmap.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

class RenderBatch;   // forward-declare so TerrainLodNode::debug can use it

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainRaycastResult
// ─────────────────────────────────────────────────────────────────────────────
struct TerrainRaycastResult
{
    bool      hit      = false;
    glm::vec3 position = {};
    glm::vec3 normal   = {0.f, 1.f, 0.f};
    float     distance = 0.f;
    int       gridX    = -1;
    int       gridZ    = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainPatchSize  — number of vertices along one side of a LOD patch
// ─────────────────────────────────────────────────────────────────────────────
enum class TerrainPatchSize : int
{
    Patch9   =   9,
    Patch17  =  17,
    Patch33  =  33,
    Patch65  =  65,
    Patch129 = 129,
};

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainNode
//  Basic heightmap terrain.  Geometry is split into rectangular blocks;
//  position / scale are inherited from Node3D.
// ─────────────────────────────────────────────────────────────────────────────
class TerrainNode : public Node3D
{
public:
    static constexpr int BLOCK_VERTS = 33; // vertices per block side

    explicit TerrainNode(const std::string &name = "Terrain");
    ~TerrainNode() override;

    // Load from an image file (grayscale or RGB — uses red channel).
    // scaleX/Z control world size; scaleY controls height amplitude.
    // texScaleU/V tile the base texture; detailScale tiles the detail texture.
    bool loadFromHeightmap(const std::string &path,
                           float scaleX, float scaleY, float scaleZ,
                           float texScaleU = 1.f, float texScaleV = 1.f,
                           float detailScale = 8.f);

    // Queries (world space using Node3D::position + scale)
    float          getHeightAt  (float worldX, float worldZ) const;
    glm::vec3      getNormalAt  (float worldX, float worldZ) const;
    TerrainRaycastResult raycast(const Ray &ray, float maxDist = 2000.f) const;

    BoundingBox getAABB() const { return m_aabb; }

    Material *getMaterial() const       { return m_material; }
    void      setMaterial(Material *m)  { m_material = m; }

    // ── Scene integration ────────────────────────────────────────────────────
    void gatherRenderItems(RenderQueue &q, const FrameContext &ctx) override;

private:
    float       *m_heightData = nullptr;
    int          m_mapW       = 0;
    int          m_mapH       = 0;
    glm::vec3    m_terrainScale = {1.f, 1.f, 1.f};
    float        m_texScaleU  = 1.f;
    float        m_texScaleV  = 1.f;

    std::vector<TerrainBuffer *> m_blocks;
    std::vector<BoundingBox>     m_blockAABBs;
    Material                    *m_material    = nullptr;
    BoundingBox                  m_aabb;
    float                        m_detailScale = 8.f;

    void         filterHeightMap();
    bool         generateBlock(TerrainBuffer *buf, BoundingBox &outAABB,
                               int blockOriginX, int blockOriginZ,
                               float texScaleU, float texScaleV,
                               float detailScale);
    float        sampleHeight(int x, int z) const;
    glm::vec3    calcNormal  (int x, int z) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainLodNode
//  GeoMipMap LOD terrain — single shared VBO, dynamic IBO rebuilt per frame.
//  Based on Irrlicht CTerrainSceneNode / user's TerrainLod (tmpo/Terrain.cpp).
// ─────────────────────────────────────────────────────────────────────────────
class TerrainLodNode : public Node3D
{
public:
    TerrainLodNode(const std::string &name   = "TerrainLod",
                   int                maxLOD = 4,
                   TerrainPatchSize   patchSz = TerrainPatchSize::Patch17,
                   float              detailScale = 8.f);
    ~TerrainLodNode() override;

    // Load heightmap.  Vertices are baked with current position+scale.
    bool loadFromHeightmap(const std::string &path,
                           float heightScale  = 1.f,
                           int   smoothFactor = 0);

    // Override setScale / setPosition to re-bake vertex positions.
    void setScale   (const glm::vec3 &s) { Node3D::setScale(s);    applyTransformation(); }
    void setScale   (float s)            { setScale(glm::vec3(s)); }
    void setPosition(const glm::vec3 &p) { Node3D::setPosition(p); applyTransformation(); }

    // Manually override a LOD distance threshold (world units).
    void setLODDistance(int lod, float dist);

    // Texture UV tiling.
    void setTextureScale(float s) { m_texScale = s; }

    // Camera movement threshold (world units) before LOD is recalculated.
    void setCameraMovementDelta(float d) { m_camMoveDelta = d; }
    // Minimum dot-product change in camera forward before LOD recalculates (0.9998 ≈ 1°).
    void setCameraRotationDelta(float dot) { m_camRotDelta = dot; }

    float          getHeightAt(float worldX, float worldZ) const;
    glm::vec3      getNormalAt(float worldX, float worldZ) const;
    TerrainRaycastResult raycast(const Ray &ray, float maxDist = 2000.f) const;

    // ── Terrain editing ─────────────────────────────────────────────────────
    // All functions modify m_heightData then rebake normals, patches and GPU.
    void smooth      (int iterations);                                      // global box smooth
    void smoothArea  (float worldX, float worldZ, float radius, int iterations); // local smooth
    void setHeight   (float worldX, float worldZ, float newHeight, float radius); // set absolute world-Y
    void modifyHeight(float worldX, float worldZ, float deltaHeight, float radius); // raise/lower (cosine falloff)
    void flatten     (float worldX, float worldZ, float targetHeight, float radius, float strength); // blend toward target

    BoundingBox getAABB()            const { return m_aabb; }
    Material   *getMaterial()        const { return m_material; }
    void        setMaterial(Material *m)   { m_material = m; }

    // Draw patch AABBs via RenderBatch (colour-coded by LOD level).
    // Call from Scene::debug or manually after render().
    void debug(RenderBatch *batch) const;

    // ── Scene integration ────────────────────────────────────────────────────
    void gatherRenderItems(RenderQueue &q, const FrameContext &ctx) override;

private:
    struct Patch
    {
        BoundingBox aabb    = {};
        glm::vec3   center  = {};
        int         currentLOD = 0;
        // Neighbour pointers for T-junction stitching
        Patch *top    = nullptr;
        Patch *bottom = nullptr;
        Patch *left   = nullptr;
        Patch *right  = nullptr;
    };

    // Terrain data
    float       *m_heightData   = nullptr;   // raw [0,1] height samples
    int          m_size         = 0;         // heightmap side (square)
    int          m_patchSize    = 17;        // vertices per patch side
    int          m_calcPatchSize= 16;        // quads per patch side (patchSize-1)
    int          m_patchCount   = 0;         // patches per dimension
    int          m_maxLOD       = 4;
    float        m_heightScale  = 1.f;
    float        m_texScale     = 1.f;
    float        m_detailScale  = 8.f;

    // GPU buffer — single VBO (static) + dynamic IBO
    TerrainBuffer *m_renderBuffer    = nullptr;
    uint32_t       m_indicesToRender = 0;

    // Source vertices (normalised, no translation) for re-baking on scale/pos change
    std::vector<TerrainVertex> m_sourceVerts;

    std::vector<Patch>  m_patches;
    std::vector<float>  m_lodDist;     // squared LOD distance thresholds
    BoundingBox         m_aabb;
    Material           *m_material = nullptr;

    // Camera change tracking
    glm::vec3 m_oldCamPos     = {};
    glm::vec3 m_oldCamForward = { 0.f, 0.f, -1.f };  // forward stored as unit vector
    float     m_camMoveDelta  = 10.f;
    float     m_camRotDelta   = 0.9998f;   // dot-product threshold (≈ 1° change)
    bool      m_forceRecalc  = true;

    // Load-time helpers
    void rebakeFromHeightData();          // sync sourceVerts from m_heightData then applyTransformation
    void calculateNormals   ();
    void applyTransformation();                         // re-bakes from m_sourceVerts
    void calculateDistanceThresholds(bool scaleChanged = false);
    void createPatches      ();
    void calculatePatchData ();

    // Per-frame helpers
    bool preRenderLODCalculations   (const glm::vec3 &camPos, const glm::vec3 &camRot, const Frustum &frustum);
    void preRenderIndicesCalculations();

    // T-junction-safe index lookup (ported from Irrlicht )
    uint32_t getIndex(int patchX, int patchZ, int patchIdx, uint32_t vX, uint32_t vZ) const;

    // Helpers
    float     sampleHeight(int x, int z) const;
    glm::vec3 calcNormal  (int x, int z) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  TiledTerrainNode
//  Flat terrain whose surface is composed of axis-aligned tiles drawn from a
//  texture atlas.  Suitable for top-down worlds, dungeon floors, etc.
// ─────────────────────────────────────────────────────────────────────────────
class TiledTerrainNode : public Node3D
{
public:
    // tilesInTextureSide  — e.g. 8 for an 8×8 atlas
    // patchLength         — world units per patch
    // tilesPerPatch       — tile count along each patch side
    // defaultTile         — tile drawn for out-of-bounds coordinates
    TiledTerrainNode(int         tilesInTextureSide,
                     float       patchLength,
                     int         tilesPerPatch,
                     uint8_t     defaultTile = 0,
                     const std::string &name = "TiledTerrain");
    ~TiledTerrainNode() override;

    // Load tile layout from an image (one pixel = one tile, red channel = tile ID)
    void loadTilemap(Pixmap *img);
    void loadTilemap(uint32_t w, uint32_t h, const uint8_t *data);

    void    setTileAt(uint32_t x, uint32_t z, uint8_t tile);
    uint8_t getTileAt(uint32_t x, uint32_t z) const;

    uint32_t mapWidth () const { return m_mapW; }
    uint32_t mapHeight() const { return m_mapH; }
    float    patchSize() const { return m_patchLen; }

    Material *getMaterial() const      { return m_material; }
    void      setMaterial(Material *m) { m_material = m; }

    // ── Scene integration ────────────────────────────────────────────────────
    void gatherRenderItems(RenderQueue &q, const FrameContext &ctx) override;

private:
    struct PatchBuf
    {
        TerrainBuffer *mesh   = nullptr;
        BoundingBox    aabb   = {};
        int            patchX = 0;
        int            patchZ = 0;
    };

    uint32_t  m_mapW        = 0;
    uint32_t  m_mapH        = 0;
    uint8_t  *m_tileMap     = nullptr;
    int       m_tilesInSide = 8;
    float     m_patchLen    = 1.f;
    int       m_tilesPerPatch = 1;
    uint8_t   m_defaultTile = 0;
    Material *m_material    = nullptr;

    std::vector<PatchBuf> m_patches;

    void    rebuildPatches   ();
    void    buildPatch       (PatchBuf &pb);
    uint8_t getTileWrapped   (int x, int z) const;
    void    getTileUVs       (uint8_t tile, float step, float uvs[4][2]) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  InfiniteTerrainNode
//  Infinite terrain that tiles a base heightmap.
//  Patches around the camera are created/cached on demand; distant ones are
//  evicted automatically.  Multiple LOD levels are supported.
// ─────────────────────────────────────────────────────────────────────────────
class InfiniteTerrainNode : public Node3D
{
public:
    static constexpr int MAX_CACHED = 512;
    static constexpr int MAX_LOD    = 4;

    explicit InfiniteTerrainNode(const std::string &name = "InfiniteTerrain");
    ~InfiniteTerrainNode() override;

    // Load the base heightmap (tiled seamlessly at the edges).
    // heightScale controls vertical amplitude.
    bool loadBaseHeightmap(const std::string &path, float heightScale = 100.f);

    // patchesVisible — half-side of the visible grid (total = 2n+1 patches)
    // verticesPerPatch — must be 2^k+1 (e.g. 33, 65)
    // patchWorldSize — world units per patch
    void configure(int patchesVisible, int verticesPerPatch, float patchWorldSize);

    float     getHeightAt(float worldX, float worldZ) const;
    glm::vec3 getNormalAt(float worldX, float worldZ) const;

    Material *getMaterial() const      { return m_material; }
    void      setMaterial(Material *m) { m_material = m; }

    // ── Scene integration ────────────────────────────────────────────────────
    void gatherRenderItems(RenderQueue &q, const FrameContext &ctx) override;

private:
    struct PatchMesh
    {
        TerrainBuffer *meshes[MAX_LOD] = {};
        BoundingBox    aabb            = {};
        uint32_t       lastFrame       = 0;

        ~PatchMesh()
        {
            for (auto *m : meshes)
                delete m;
        }
    };

    struct BaseData
    {
        float    *heights    = nullptr;
        uint32_t  width      = 0;
        uint32_t  height     = 0;
        float     scale      = 100.f;
    } m_base;

    int      m_visibleHalf  = 4;    // half-extent of the visible patch grid
    int      m_vertsPerPatch = 33;
    float    m_patchWorld    = 64.f;
    uint32_t m_frame         = 0;

    std::unordered_map<long long, PatchMesh *> m_cache;
    Material *m_material = nullptr;

    long long  patchKey     (int px, int pz) const;
    PatchMesh *getOrCreate  (int px, int pz, int lod);
    void       buildMesh    (int px, int pz, int lod, PatchMesh *patch);
    float      sampleBase   (float u, float v) const;
    glm::vec3  calcNormal   (float u, float v) const;
    int        calcLOD      (float distSq) const;
    void       evictOld     ();
};
