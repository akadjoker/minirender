#pragma once
#include "Mesh.hpp"
#include "Node.hpp"
#include "BinaryStream.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

// ============================================================
//  .mrlvl format constants
// ============================================================
constexpr uint32_t MRLV_MAGIC   = 0x564C524Du; // "MRLV"
constexpr uint32_t MRLV_VERSION = 1;
constexpr uint32_t CHUNK_ENTS   = 0x454E5453u; // "ENTS"

// ============================================================
//  Entity type tag (for file format only)
// ============================================================
enum class LevelEntityType : uint32_t
{
    PlayerStart  = 0,
    Light        = 1,
    Door         = 2,
    Elevator     = 3,
    Platform     = 4,
    Placement    = 5,
    Trigger      = 6,
    Teleporter   = 7,
    SoundEmitter = 8,
};

enum class LevelDoorType : uint32_t
{
    Slide   = 0,
    Turn    = 1,
    Shutter = 2,
};

// ============================================================
//  Entity structs — plain data, no inheritance, no virtual
// ============================================================
struct LevelPlayerStart
{
    std::string name;
    glm::vec3   position  {0.0f};
    glm::vec3   direction {0.0f, 0.0f, 1.0f};
};

struct LevelLight
{
    std::string name;
    glm::vec3   position  {0.0f};
    LightType   lightType = LightType::Point;
    glm::vec3   color     {1.0f};
    float       intensity = 1.0f;
    float       radius    = 512.0f;
    glm::vec3   direction {0.0f, -1.0f, 0.0f};
    float       spotAngle    = 45.0f;
    float       spotSoftness = 0.1f;
};

struct LevelDoor
{
    std::string   name;
    glm::vec3     position  {0.0f};
    LevelDoorType doorType  = LevelDoorType::Slide;
    glm::vec3     direction {1.0f, 0.0f, 0.0f};
    float         distance  = 128.0f;
    float         speed     = 64.0f;
    bool          startOpen = false;
    int           linkedMesh = -1;
};

struct LevelElevator
{
    std::string name;
    glm::vec3   position    {0.0f};
    glm::vec3   endPosition {0.0f, 128.0f, 0.0f};
    float       speed      = 64.0f;
    float       waitTime   = 2.0f;
    int         linkedMesh = -1;
};

struct LevelPlatform
{
    std::string name;
    glm::vec3   position    {0.0f};
    glm::vec3   endPosition {0.0f, 128.0f, 0.0f};
    float       speed      = 64.0f;
    float       waitTime   = 2.0f;
    int         linkedMesh = -1;
};

struct LevelPlacement
{
    std::string name;
    glm::vec3   position {0.0f};
    int         itemType  = 0;
    float       rotationY = 0.0f;
};

struct LevelTrigger
{
    std::string name;
    glm::vec3   position {0.0f};
    float       radius   = 64.0f;
    std::string targetName;
};

struct LevelTeleporter
{
    std::string name;
    glm::vec3   position {0.0f};
    glm::vec3   target   {0.0f};
};

struct LevelSoundEmitter
{
    std::string name;
    glm::vec3   position {0.0f};
    std::string soundPath;
    float       radius  = 256.0f;
    float       volume  = 1.0f;
    bool        looping = true;
};

// ============================================================
//  LevelVertex — 56 bytes, optimized for static level geometry
//
//  Unlike Vertex (48 bytes), this has a dedicated lightmapUv
//  channel instead of overloading tangent for lightmap coords.
//
//  Attribute layout:
//    0  position     vec3
//    1  normal       vec3
//    2  tangent      vec4
//    3  uv           vec2   (albedo / diffuse)
//    4  lightmapUv   vec2   (lightmap atlas)
// ============================================================
struct LevelVertex
{
    glm::vec3 position;     // 12
    glm::vec3 normal;       // 12
    glm::vec4 tangent;      // 16  (w = handedness)
    glm::vec2 uv;           //  8
    glm::vec2 lightmapUv;   //  8  = 56 total
};

// ============================================================
//  LevelMeshBuffer — GPU buffer for LevelVertex
// ============================================================
struct LevelMeshBuffer
{
    std::vector<LevelVertex> vertices;
    std::vector<uint32_t>    indices;
    GLuint vao = 0, vbo = 0, ibo = 0;

    void upload();
    void free();

    void draw() const;
    void drawRange(uint32_t start, uint32_t count) const;

    int vertexCount() const { return (int)vertices.size(); }
    int indexCount()  const { return (int)indices.size(); }

    LevelMeshBuffer() = default;
    LevelMeshBuffer(const LevelMeshBuffer&) = delete;
    LevelMeshBuffer& operator=(const LevelMeshBuffer&) = delete;
    ~LevelMeshBuffer() { free(); }
};

// ============================================================
//  LevelMesh — complete level mesh with surfaces + lightmap
// ============================================================
class LevelMesh
{
public:
    std::string name;
    LevelMeshBuffer buffer;
    BoundingBox aabb = {};
    std::vector<Surface>   surfaces;
    std::vector<Material*> materials;   // owned by LevelMesh

    struct EmbeddedLightmap {
        int width = 0, height = 0, channels = 3;
        std::vector<uint8_t> pixels;
        bool empty() const { return pixels.empty(); }
    };
    std::vector<EmbeddedLightmap> lightmaps;
    EmbeddedLightmap lightmap; // legacy single-page access

    void upload();          // upload buffer + compute aabb
    void computeAABB();

    void draw() const { buffer.draw(); }
    void drawRange(uint32_t s, uint32_t c) const { buffer.drawRange(s, c); }

    // Draw a single surface
    void drawSurface(int surfaceIndex) const;

    ~LevelMesh();

    // Picking (same algorithm as Mesh)
    PickResult pick(const Ray& worldRay, const glm::mat4& model = glm::mat4(1.f)) const;

    void add_surface(uint32_t start, uint32_t count, int matIndex, int lightmapIndex = -1);
};

// ============================================================
//  LevelData — result of loading a .mrlvl
// ============================================================
struct LevelData
{
    LevelMesh mesh;

    // Entities — one vector per type, cache-friendly iteration
    std::vector<LevelPlayerStart>  playerStarts;
    std::vector<LevelLight>        lights;
    std::vector<LevelDoor>         doors;
    std::vector<LevelElevator>     elevators;
    std::vector<LevelPlatform>     platforms;
    std::vector<LevelPlacement>    placements;
    std::vector<LevelTrigger>      triggers;
    std::vector<LevelTeleporter>   teleporters;
    std::vector<LevelSoundEmitter> soundEmitters;

    int entityCount() const
    {
        return (int)(playerStarts.size() + lights.size() + doors.size() +
                     elevators.size() + platforms.size() + placements.size() +
                     triggers.size() + teleporters.size() + soundEmitters.size());
    }
};

// ============================================================
//  LevelReader — .mrlvl → LevelData
// ============================================================
class LevelReader
{
public:
    std::string textureDir;

    bool load(const std::string& path, LevelData* out);
};
