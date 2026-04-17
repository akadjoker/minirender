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
    Elevator
};

struct LevelMeshObject
{
    std::string name = "Brush 01";
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
    // Directional / Spot
    glm::vec3 direction {0.0f, -1.0f, 0.0f}; // normalized direction
    float spotAngle = 45.0f;                   // half-angle in degrees (spot only)
    float spotSoftness = 0.1f;                 // 0=hard edge, 1=very soft
};

class LevelEditorScene
{
public:
    LevelEditorScene();

    void reset();

    const std::vector<LevelMeshObject>& meshObjects() const { return meshObjects_; }
    const std::vector<LevelEntityObject>& entities() const { return entities_; }

    std::vector<LevelMeshObject>& meshObjects() { return meshObjects_; }
    std::vector<LevelEntityObject>& entities() { return entities_; }

private:
    std::vector<LevelMeshObject> meshObjects_;
    std::vector<LevelEntityObject> entities_;
};
