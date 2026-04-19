#pragma once

#include "EditableMesh.hpp"
#include "Node.hpp"

#include <string>
#include <vector>

enum class LevelEntityType
{
    PlayerStart,
    Light,
    Door,
    Elevator,
    Platform,
    Placement
};

enum class DoorType
{
    Slide,    // sliding door — moves along direction
    Turn,     // hinged door — rotates from edge
    Shutter   // double sliding — two halves slide apart
};

enum class LevelMeshPrimitive
{
    Unknown,
    Box,
    Room,
    Sector,
    RoomBoxesPart,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    Tube,
    Pyramid,
    DoorFrame,
    Terrain,
    Pillar,
    Plane,
    Wedge,
    Stairs,
    SpiralStairs,
    Text,
    Imported,
    Empty
};

enum class LevelMeshBlendMode
{
    Alpha = 0,
    Additive
};

struct LevelMeshObject
{
    std::string name = "Brush 01";
    LevelMeshPrimitive primitive = LevelMeshPrimitive::Box;
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEuler {0.0f, 0.0f, 0.0f};
    glm::vec3 scale {1.0f, 1.0f, 1.0f};
    glm::vec3 pivot {0.0f, 0.0f, 0.0f};
    bool visible = true;
    bool locked = false;
    bool blendEnabled = false;
    bool twoSided = false;
    LevelMeshBlendMode blendMode = LevelMeshBlendMode::Alpha;
    EditableMesh mesh = EditableMesh::MakeBox(glm::vec3(-64.0f, 0.0f, -64.0f), glm::vec3(64.0f, 128.0f, 64.0f));
    struct TerrainTextureLayer
    {
        std::string name = "Layer";
        std::string texturePath;
        float opacity = 1.0f;
        bool visible = true;
        int maskWidth = 0;
        int maskHeight = 0;
        std::vector<unsigned char> maskData;
    };
    std::vector<TerrainTextureLayer> terrainLayers;
};

// LightType is defined in Node.hpp: enum class LightType { Point, Directional, Spot };

struct LevelEntityObject
{
    std::string name = "PlayerStart";
    LevelEntityType type = LevelEntityType::PlayerStart;
    glm::vec3 position {0.0f, 0.0f, 0.0f};

    // Light properties
    LightType lightType = LightType::Point;
    glm::vec3 color {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float radius = 512.0f;

    // Directional / Spot / PlayerStart direction
    glm::vec3 direction {0.0f, -1.0f, 0.0f};
    float spotAngle = 45.0f;
    float spotSoftness = 0.1f;

    // Door properties
    DoorType doorType = DoorType::Slide;
    float doorDistance = 128.0f;   // slide distance (units) or turn angle (degrees)
    float doorSpeed = 64.0f;      // units/sec or degrees/sec
    bool doorStartOpen = false;
    int linkedMeshIndex = -1;     // which mesh this entity controls

    // Elevator / Platform properties
    glm::vec3 endPosition {0.0f, 128.0f, 0.0f};
    float moveSpeed = 64.0f;
    float waitTime = 2.0f;        // seconds to wait at each end

    // Placement properties
    int itemType = 0;
    float rotationY = 0.0f;
};

class LevelEditorScene
{
public:
    LevelEditorScene();

    void reset();

    const std::vector<LevelMeshObject>& meshObjects() const { return meshObjects_; }
    const std::vector<LevelEntityObject>& entities() const { return entities_; }
    const std::string& assetRoot() const { return assetRoot_; }
    const std::string& lightmapPath() const { return lightmapPath_; }
    const glm::vec3& creationPivotPosition() const { return creationPivotPosition_; }
    const glm::vec3& creationPivotRotation() const { return creationPivotRotation_; }

    std::vector<LevelMeshObject>& meshObjects() { return meshObjects_; }
    std::vector<LevelEntityObject>& entities() { return entities_; }
    std::string& assetRoot() { return assetRoot_; }
    std::string& lightmapPath() { return lightmapPath_; }
    glm::vec3& creationPivotPosition() { return creationPivotPosition_; }
    glm::vec3& creationPivotRotation() { return creationPivotRotation_; }

private:
    std::vector<LevelMeshObject> meshObjects_;
    std::vector<LevelEntityObject> entities_;
    std::string assetRoot_ = "assets";
    std::string lightmapPath_;
    glm::vec3 creationPivotPosition_ = glm::vec3(0.0f);
    glm::vec3 creationPivotRotation_ = glm::vec3(0.0f);
};
