#pragma once

#include "EditableMesh.hpp"

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
    EditableMesh mesh = EditableMesh::MakeBox(glm::vec3(-64.0f, 0.0f, -64.0f), glm::vec3(64.0f, 128.0f, 64.0f));
};

struct LevelEntityObject
{
    std::string name = "PlayerStart";
    LevelEntityType type = LevelEntityType::PlayerStart;
    glm::vec3 position {0.0f, 0.0f, 0.0f};
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
