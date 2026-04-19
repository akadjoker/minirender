#include "LevelEditorScene.hpp"

LevelEditorScene::LevelEditorScene()
{
    reset();
}

void LevelEditorScene::reset()
{
    meshObjects_.clear();
    entities_.clear();
    assetRoot_ = "assets";
    lightmapPath_.clear();
    creationPivotPosition_ = glm::vec3(0.0f);
    creationPivotRotation_ = glm::vec3(0.0f);

    meshObjects_.push_back(LevelMeshObject{});

    LevelEntityObject playerStart;
    playerStart.name = "Player Start";
    playerStart.type = LevelEntityType::PlayerStart;
    playerStart.position = glm::vec3(0.0f, 16.0f, 0.0f);
    entities_.push_back(playerStart);
}
