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
    EditableMesh mesh = EditableMesh::MakeBox(glm::vec3(-64.0f, 0.0f, -64.0f), glm::vec3(64.0f, 128.0f, 64.0f));
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

    std::vector<LevelMeshObject>& meshObjects() { return meshObjects_; }
    std::vector<LevelEntityObject>& entities() { return entities_; }
    std::string& assetRoot() { return assetRoot_; }

private:
    std::vector<LevelMeshObject> meshObjects_;
    std::vector<LevelEntityObject> entities_;
    std::string assetRoot_ = "assets";
};
