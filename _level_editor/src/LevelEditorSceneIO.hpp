#pragma once

#include "LevelEditorScene.hpp"

#include <filesystem>
#include <string>

bool saveLevelEditorScene(const std::filesystem::path& path,
                          const LevelEditorScene& scene,
                          std::string& error);

bool loadLevelEditorScene(const std::filesystem::path& path,
                          LevelEditorScene& scene,
                          std::string& error);
