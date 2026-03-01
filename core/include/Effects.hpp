#pragma once
#include "Node.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "Math.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <functional>

// ============================================================
//  EffectVertex  (32 bytes)  — shared by Decal + LensFlare
// ============================================================
struct EffectVertex
{
    glm::vec3 position; // 12
    glm::vec2 uv;       //  8
    glm::vec4 color;    // 16
};

// ============================================================
//  EffectBuffer — dynamic quad buffer (pos/uv/color)
//  Similar to ParticleBuffer but using EffectVertex layout.
// ============================================================
struct EffectBuffer : public IDrawable
{
    std::vector<EffectVertex> vertices;
    std::vector<uint32_t>     indices;
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLenum mode = GL_TRIANGLES;
    BoundingBox aabb = {};

    void allocate(int maxQuads);  // pre-allocate VBO capacity
    void upload();                // full upload
    void free();

    // IDrawable
    void      draw()                                      const override;
    void      drawRange(uint32_t start, uint32_t count)   const override;
    BoundingBox getAABB()                                 const override { return aabb; }
    int       vertexCount()                               const override { return (int)vertices.size(); }
    int       indexCount()                                const override { return (int)indices.size(); }
};

// ============================================================
//  DecalNode
//  World-space projected quads oriented along surface normal.
// ============================================================
class DecalNode : public Node3D
{
public:
    struct Decal
    {
        glm::vec3 position;
        glm::vec3 normal    = {0.f, 1.f, 0.f};
        glm::vec2 size      = {1.f, 1.f};
        glm::vec4 color     = {1.f, 1.f, 1.f, 1.f};
        float     rotation  = 0.f;    // around normal axis
        float     lifetime  = 10.f;   // seconds; < 0 → infinite
        float     timeAlive = 0.f;
        float     fadeStart = 0.8f;   // start fading at 80% of lifetime
        bool      active    = true;
    };

    explicit DecalNode(int maxDecals = 500);
    ~DecalNode();

    /// Add a decal. lifetime < 0 → uses defaultLifetime. Returns slot index.
    int  addDecal(const glm::vec3& pos, const glm::vec3& normal,
                  const glm::vec2& size, const glm::vec4& color,
                  float lifetime = -1.f);

    /// Convenience — uses default size/color/lifetime.
    int  addDecal(const glm::vec3& pos, const glm::vec3& normal,
                  float lifetime = -1.f);

    void setDefaultLifetime (float t)          { defaultLifetime_ = t; }
    void setDefaultFadeStart(float f)          { defaultFadeStart_ = f; }
    void setDefaultSize     (const glm::vec2& s) { defaultSize_ = s; }

    void  removeDecal(int idx);
    void  removeAll();
    Decal *getDecal(int idx);
    int   decalCount()  const { return (int)decals_.size(); }
    int   activeCount() const;

    Material *material = nullptr;

    // ── Node overrides ──────────────────────────────────────
    void update(float dt) override;
    void gatherRenderItems(RenderQueue& q, const FrameContext& ctx) override;

private:
    std::vector<Decal> decals_;
    EffectBuffer       buffer_;
    int                maxDecals_;
    float              defaultLifetime_  = 10.f;
    float              defaultFadeStart_ = 0.8f;
    glm::vec2          defaultSize_      = {1.f, 1.f};
    bool               dirty_            = true;

    void rebuild();
};

// ============================================================
//  FlareElement — one element along the flare axis
// ============================================================
struct FlareElement
{
    float     position = 0.f;    // -1 = behind light ... 0 = at light ... 1 = screen centre
    float     size     = 0.05f;  // fraction of screen height
    glm::vec3 color    = {1.f, 1.f, 1.f};
    float     alpha    = 0.8f;
    bool      isSun    = false;  // marks the main sun sprite

    // Pixel-space clip rect in the atlas (x, y, w, h).
    // Set via setPixelRect(); converted to UVs at draw time using the actual texture size.
    glm::vec4 pixelRect = {0.f, 0.f, 0.f, 0.f};  // zero = use full texture

    FlareElement() = default;
    FlareElement(float pos, float sz, const glm::vec3& c, float a = 0.8f)
        : position(pos), size(sz), color(c), alpha(a) {}

    /// Set clip region in atlas pixel coordinates 
    void setPixelRect(float px, float py, float pw, float ph)
    {
        pixelRect = { px, py, pw, ph };
    }

    /// Convenience: convert pixelRect to normalised UV given actual texture size
    glm::vec4 uvRect(float texW, float texH) const
    {
        if (pixelRect.z <= 0.f || texW <= 0.f || texH <= 0.f)
            return {0.f, 0.f, 1.f, 1.f};
        return { pixelRect.x / texW,
                 pixelRect.y / texH,
                (pixelRect.x + pixelRect.z) / texW,
                (pixelRect.y + pixelRect.w) / texH };
    }

    /// Convenience: set from atlas grid cell (0-based col/row)
    void setAtlasCell(int col, int row, int cols, int rows, float texW, float texH)
    {
        float cw = texW / cols;  float ch = texH / rows;
        setPixelRect(col * cw, row * ch, cw, ch);
    }
};

// ============================================================
//  LensFlareNode
//  Screen-space lens flare along the sun ↔ screen-centre axis.
//  Submitted as transparent RenderItems with NDC-space geometry.
//  Requires a simple unlit shader (depth-test OFF).
// ============================================================
class LensFlareNode : public Node3D
{
public:
    LensFlareNode();
    ~LensFlareNode();

    // ── configuration ──────────────────────────────────────
    void setSunDirection(const glm::vec3& dir)  { sunDirection_ = glm::normalize(dir); }
    void setSunColor    (const glm::vec3& c)    { sunColor_ = c; }
    void setSunSize     (float s)               { sunSize_ = s; }
    void setEnabled     (bool e)                { enabled_ = e; }
    void setEdgeFade    (float limit)           { edgeFade_ = limit; }  // 0-1

    void addFlare  (const FlareElement& fe) { flares_.push_back(fe); }
    void clearFlares()                      { flares_.clear(); }

    /// Populate flare elements using pixel clip rects from the atlas.
    /// The actual texture size is read at draw time from the material's bound texture.
    void initDefaultFlares();

    // ── textures / shader ──────────────────────────────────
    Texture *sunTexture   = nullptr;   // sprite atlas — sun element
    Texture *flareTexture = nullptr;   // sprite atlas — flare ring/hex
    Material *material    = nullptr;   // if non-null overrides internal draw

    glm::vec3 getSunDirection() const { return sunDirection_; }
    bool      isEnabled()       const { return enabled_; }

    // ── Node overrides ──────────────────────────────────────
    void gatherRenderItems(RenderQueue& q, const FrameContext& ctx) override;

private:
    glm::vec3               sunDirection_ = {0.f, -1.f, 0.f};
    glm::vec3               sunColor_     = {1.f, 0.9f, 0.7f};
    float                   sunSize_      = 0.08f;
    float                   edgeFade_     = 0.15f;
    bool                    enabled_      = true;
    std::vector<FlareElement> flares_;

    EffectBuffer            buffer_;
    bool                    allocated_    = false;
    bool                    loggedOnce_   = false;

    glm::vec2 toNDC(const glm::vec4& clip) const;
    float     computeFade(const glm::vec2& ndc) const;
    void      buildGeometry(const glm::vec2& sunNDC, float fade, int vpW, int vpH);
};

// ============================================================
//  GrassNode
//  Static-batch of cross/tri-cross billboard quads for grass.
//  Wind animation is driven by u_time / u_windStrength in shader.
// ============================================================
class GrassNode : public Node3D
{
public:
    enum class GrassType { Single, Cross, TriCross };

    explicit GrassNode(GrassType type = GrassType::Cross);
    ~GrassNode();

    /// Add one grass clump.
    void addClump(const glm::vec3& position,
                  const glm::vec3& normal   = {0.f, 1.f, 0.f},
                  const glm::vec2& size     = {0.8f, 1.0f},
                  const glm::vec4& color    = {1.f, 1.f, 1.f, 1.f});

    /// Fill a rectangular patch with randomly-placed clumps (seed-reproducible).
    void fillArea(const glm::vec3& center, float width, float depth,
                  int count, float minSize = 0.6f, float maxSize = 1.2f,
                  unsigned int seed = 42);

    void setGrassType   (GrassType t)           { type_ = t; dirty_ = true; }
    void setWindStrength(float w)               { windStrength_ = w; }
    void setWindSpeed   (float s)               { windSpeed_ = s; }
    void setWindDirection(const glm::vec2& d)   { windDir_ = glm::normalize(d); }

    /// Upload geometry (call after all addClump() / fillArea()).
    void build();

    void clear();

    int     clumpCount() const { return (int)clumps_.size(); }
    bool    isBuilt()    const { return built_; }

    Material *material = nullptr;

    // ── Node overrides ──────────────────────────────────────
    void update(float dt) override;
    void gatherRenderItems(RenderQueue& q, const FrameContext& ctx) override;

private:
    struct ClumpInfo
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 size;
        glm::vec4 color;
    };

    GrassType              type_         = GrassType::Cross;
    std::vector<ClumpInfo> clumps_;
    MeshBuffer             buffer_;
    BoundingBox            aabb_         = {};
    float                  windStrength_ = 0.3f;
    float                  windSpeed_    = 1.2f;
    glm::vec2              windDir_      = {1.f, 0.f};
    float                  time_         = 0.f;
    bool                   dirty_        = true;
    bool                   built_        = false;

    void addQuad(const glm::vec3& center,
                 const glm::vec3& right, const glm::vec3& up,
                 const glm::vec2& size,  const glm::vec4& color);
};

// ============================================================
//  ManualMeshNode
//  Ogre-like manual geometry builder.
//  Supports both a streaming API and direct vertex/index access.
// ============================================================
class ManualMeshNode : public Node3D
{
public:
    ManualMeshNode();
    ~ManualMeshNode();

    // ── Streaming builder API ──────────────────────────────
    /// Start defining geometry. Call end() to finalise.
    void begin(GLenum primitiveType = GL_TRIANGLES, bool dynamic = false);

    /// Set current normal (used for next position()).
    ManualMeshNode& normal   (float x, float y, float z);
    ManualMeshNode& normal   (const glm::vec3& n);

    /// Set current UV (used for next position()).
    ManualMeshNode& texCoord (float u, float v);

    /// Set current colour / tangent handedness (w).
    ManualMeshNode& colour   (float r, float g, float b, float a = 1.f);
    ManualMeshNode& colour   (const glm::vec4& c);

    /// Emit one vertex at position, flushing current normal/uv/colour state.
    ManualMeshNode& position (float x, float y, float z);
    ManualMeshNode& position (const glm::vec3& p);

    /// Emit an index.
    ManualMeshNode& index    (uint32_t i);
    ManualMeshNode& triangle (uint32_t a, uint32_t b, uint32_t c);

    /// Finalise and upload to GPU.
    void end();

    // ── Direct access (bypass streaming API) ──────────────
    std::vector<Vertex>   &vertices() { return buffer_.vertices; }
    std::vector<uint32_t> &indices()  { return buffer_.indices;  }

    /// Re-upload after direct edits.
    void build();

    // ── Utilities ─────────────────────────────────────────
    void clear();

    /// Recompute flat normals from triangle list (overwrites existing normals).
    void computeNormals();

    /// Recompute AABB from current vertices.
    BoundingBox computeAABB() const;

    int vertexCount() const { return (int)buffer_.vertices.size(); }
    int indexCount()  const { return (int)buffer_.indices.size();  }

    Material *material = nullptr;

    // ── Node overrides ──────────────────────────────────────
    void gatherRenderItems(RenderQueue& q, const FrameContext& ctx) override;

private:
    MeshBuffer buffer_;
    bool       building_ = false;

    // Accumulated current-vertex state
    glm::vec3  curNormal_   = {0.f, 1.f, 0.f};
    glm::vec2  curUV_       = {0.f, 0.f};
    glm::vec4  curColour_   = {1.f, 1.f, 1.f, 1.f};
};
