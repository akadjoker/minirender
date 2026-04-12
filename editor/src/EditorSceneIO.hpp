#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "EditorData.hpp"

struct EditorSettings
{
    std::string assetRoot = "assets";
    std::string currentTexturePath = "assets/wall.jpg";
    glm::vec3 focus = glm::vec3(0.0f);
    float gridStep = 32.0f;
    float snapSize = 16.0f;
    EditorLayoutMode layoutMode = EditorLayoutMode::FourViews;
    float defaultBrushThickness = 128.0f;
    float defaultBrushHeight = 128.0f;
    bool showGrid = true;
    bool showAxes = true;
    bool snapEnabled = true;
    float sidebarTopHeight = 520.0f;
    int assetPanelHeight = 200;
    int sidebarWidth = 340;
    bool assetViewAsGrid = false;
    EditorRenderingMode renderingMode = EditorRenderingMode::Solid;
    bool enableTransparency = false;
    float transparency = 1.0f;
    bool textureLock = true;

    struct ViewSettings
    {
        float orthoSize = 256.0f;
        float perspectiveDistance = 720.0f;
        float perspectiveYaw = 45.0f;
        float perspectivePitch = 28.0f;
    };

    std::array<ViewSettings, 4> views;
};

void saveEditorSettings(const EditorSettings &settings, const std::string &filename = "editor_settings.json");
bool loadEditorSettings(EditorSettings &settings, const std::string &filename = "editor_settings.json");
std::filesystem::path ensureSceneExtension(const std::filesystem::path &path);
std::string resolveTexturePathForLoad(const std::string &rawPath);
bool saveEditorScene(const std::filesystem::path &path,
                     const std::vector<BrushVolume> &brushes,
                     const glm::vec3 &focus,
                     const std::string &currentTexturePath,
                     std::string &error);
bool saveEditorScene(const std::filesystem::path &path,
                     const std::vector<EditorEntity> &entities,
                     const glm::vec3 &focus,
                     const std::string &currentTexturePath,
                     std::string &error);
bool loadEditorScene(const std::filesystem::path &path,
                     std::vector<BrushVolume> &brushes,
                     glm::vec3 &focus,
                     std::string &currentTexturePath,
                     std::string &error);
bool loadEditorScene(const std::filesystem::path &path,
                     std::vector<EditorEntity> &entities,
                     glm::vec3 &focus,
                     std::string &currentTexturePath,
                     std::string &error);
