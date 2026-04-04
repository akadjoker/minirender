#pragma once

#include "Node.hpp"
#include "Material.hpp"
#include "Mesh.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Q3BspMap
{
public:
    enum class EntityKind
    {
        Unknown,
        Worldspawn,
        Light,
        InfoPlayerStart,
        InfoPlayerDeathmatch,
        InfoPlayerIntermission,
        FuncDoor,
        FuncPlat,
        FuncTrain,
        FuncButton,
        TriggerMultiple,
        TriggerPush,
        TriggerTeleport,
        TargetPosition,
        TargetSpeaker,
        MiscModel,
    };

    struct Lump
    {
        int32_t offset = 0;
        int32_t length = 0;
    };

    struct MeshGroup
    {
        int textureIndex = -1;
        int lightmapIndex = -1;
        Mesh *mesh = nullptr;
        Material *material = nullptr;
        BoundingBox localAabb = {};
    };

    struct RawEntity
    {
        std::unordered_map<std::string, std::string> properties;

        const std::string *find(const std::string &key) const
        {
            auto it = properties.find(key);
            return (it != properties.end()) ? &it->second : nullptr;
        }

        std::string value(const std::string &key, const std::string &fallback = "") const
        {
            const std::string *v = find(key);
            return v ? *v : fallback;
        }
    };

    struct MapEntity
    {
        size_t rawEntityIndex = 0; // index into rawEntities()
        const RawEntity *raw = nullptr;
        EntityKind kind = EntityKind::Unknown;
        std::unordered_map<std::string, PropertyValue> properties;

        void setString(const std::string &key, const std::string &value);
        void setInt(const std::string &key, int value);
        void setFloat(const std::string &key, float value);
        void setVec3(const std::string &key, const glm::vec3 &value);

        const PropertyValue *findProperty(const std::string &key) const;
        bool hasProperty(const std::string &key) const;
        const std::string *findString(const std::string &key) const;
        int getInt(const std::string &key, int fallback = 0) const;
        float getFloat(const std::string &key, float fallback = 0.0f) const;
        bool getVec3(const std::string &key, glm::vec3 &out) const;
    };

    Q3BspMap() = default;
    ~Q3BspMap() { clear(); }

    bool load(const std::string &bspPath,
              const std::string &texturesBaseDir,
              Shader *shader,
              float scale = 0.03f,
              float lightmapBrightness = 8.0f);

    void clear();

    bool ready() const { return ready_; }

    int faceCount() const { return static_cast<int>(faces_.size()); }
    int vertexCount() const { return static_cast<int>(vertices_.size()); }
    int textureCount() const { return static_cast<int>(textures_.size()); }
    int lightmapCount() const { return static_cast<int>(lightmaps_.size()); }

    float scale() const { return scale_; }
    void setPatchTessellation(int tess) { patchTess_ = (tess < 1) ? 1 : tess; }
    int patchTessellation() const { return patchTess_; }
    float lightmapStrength() const { return lightmapMul_; }
    float lightmapGamma() const { return lightmapGamma_; }
    void setLightmapStrength(float mul);
    void setLightmapGamma(float gamma);

    int findLeaf(const glm::vec3 &q3Pos) const;
    int findLeafFromLocal(const glm::vec3 &localPos) const;

    std::vector<int> collectVisibleGroupIndices(const glm::vec3 &cameraLocalPos) const;

    const std::vector<MeshGroup> &groups() const { return groups_; }
    const std::vector<RawEntity> &rawEntities() const { return rawEntities_; }
    const std::vector<MapEntity> &entities() const { return entities_; }
    std::vector<const MapEntity *> entitiesByKind(EntityKind kind) const;
    std::vector<const RawEntity *> rawEntitiesByClassname(const std::string &classname) const;
    bool rawEntityOriginLocal(const RawEntity &e, glm::vec3 &outLocal) const;
    bool entityOriginLocal(const MapEntity &entity, glm::vec3 &outLocal) const;
    glm::vec3 q3ToLocal(const glm::vec3 &q3Pos) const;
    static EntityKind classifyClassname(const std::string &classname);

private:
    struct TextureEntry
    {
        std::string name;
        int32_t flags = 0;
        int32_t contents = 0;
        Texture *texture = nullptr;
    };

    struct BspVertex
    {
        glm::vec3 position = {0.0f, 0.0f, 0.0f};
        glm::vec3 normal = {0.0f, 1.0f, 0.0f};
        glm::vec2 uv = {0.0f, 0.0f};
        glm::vec2 lmUv = {0.0f, 0.0f};
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct Face
    {
        int32_t textureIdx = -1;
        int32_t effectIdx = -1;
        int32_t faceType = 0;
        int32_t firstVert = 0;
        int32_t numVerts = 0;
        int32_t firstMeshVert = 0;
        int32_t numMeshVerts = 0;
        int32_t lmIdx = -1;
        int32_t patchW = 0;
        int32_t patchH = 0;
    };

    struct Plane
    {
        glm::vec3 normal = {0.0f, 1.0f, 0.0f};
        float dist = 0.0f;
    };

    struct Node
    {
        int32_t plane = 0;
        int32_t front = 0;
        int32_t back = 0;
        glm::ivec3 mins = {0, 0, 0};
        glm::ivec3 maxs = {0, 0, 0};
    };

    struct Leaf
    {
        int32_t cluster = -1;
        int32_t area = 0;
        glm::ivec3 mins = {0, 0, 0};
        glm::ivec3 maxs = {0, 0, 0};
        int32_t firstLeafFace = 0;
        int32_t numLeafFaces = 0;
        int32_t firstLeafBrush = 0;
        int32_t numLeafBrushes = 0;
    };

    struct LightmapEntry
    {
        Texture *texture = nullptr;
    };

    bool parse(const std::vector<uint8_t> &bytes, float lightmapBrightness);
    bool readHeader(const std::vector<uint8_t> &bytes);
    bool readEntities(const std::vector<uint8_t> &bytes);
    bool readTextures(const std::vector<uint8_t> &bytes);
    bool readVertices(const std::vector<uint8_t> &bytes);
    bool readMeshVerts(const std::vector<uint8_t> &bytes);
    bool readFaces(const std::vector<uint8_t> &bytes);
    bool readLightmaps(const std::vector<uint8_t> &bytes, float brightness);
    bool readPlanes(const std::vector<uint8_t> &bytes);
    bool readNodes(const std::vector<uint8_t> &bytes);
    bool readLeaves(const std::vector<uint8_t> &bytes);
    bool readLeafFaces(const std::vector<uint8_t> &bytes);
    void rebuildObjects();

    void resolveTextures(const std::string &texturesBaseDir);
    bool buildGroups(Shader *shader);
    void applyLightmapParamsToMaterials();

    std::vector<Lump> lumps_;
    std::vector<RawEntity> rawEntities_;
    std::vector<MapEntity> entities_;
    std::vector<TextureEntry> textures_;
    std::vector<BspVertex> vertices_;
    std::vector<int32_t> meshVerts_;
    std::vector<Face> faces_;
    std::vector<LightmapEntry> lightmaps_;
    std::vector<Plane> planes_;
    std::vector<Node> nodes_;
    std::vector<Leaf> leaves_;
    std::vector<int32_t> leafFaces_;

    std::vector<MeshGroup> groups_;
    std::vector<int32_t> faceToGroup_;

    std::vector<std::string> ownedTextureNames_;
    std::vector<std::string> ownedMaterialNames_;
    std::vector<std::string> ownedMeshNames_;

    std::string mapKey_;
    float scale_ = 0.03f;
    int patchTess_ = 6;
    float lightmapMul_ = 2.0f;
    float lightmapGamma_ = 1.0f;
    bool ready_ = false;
};

class Q3BspNode : public Node3D
{
public:
    explicit Q3BspNode(Q3BspMap *m = nullptr) : map(m) {}

    Q3BspMap *map = nullptr;
    bool useBspTraversal = true;
    uint32_t passMask = RenderPassMask::Opaque;

    void gatherRenderItems(RenderQueue &queue, const FrameContext &ctx) override;
};
