#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>

#include "Camera.hpp"

struct RectI
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(const glm::vec2 &p) const
    {
        return p.x >= (float)x && p.y >= (float)y &&
               p.x < (float)(x + w) && p.y < (float)(y + h);
    }
};

enum class EditorViewType
{
    Top = 0,
    Front = 1,
    Right = 2,
    Perspective = 3,
    Bottom = 4,
    Back = 5,
    Left = 6
};

enum class EditorLayoutMode
{
    TwoViews = 2,
    ThreeViews = 3,
    FourViews = 4
};

enum class EditorRenderingMode
{
    Solid,
    Wireframe,
    Textured
};

enum class EditorTool
{
    Select,
    Move,
    Scale,
    Rotate,
    Face,
    Brush
};

struct BrushVolume
{
    struct FaceUV
    {
        glm::vec2 offset = glm::vec2(0.0f);
        glm::vec2 scale = glm::vec2(1.0f);
        float rotation = 0.0f;
    };

    glm::vec3 mins = glm::vec3(0.0f);
    glm::vec3 maxs = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(0.47f, 0.82f, 1.0f);
    std::string name;
    bool hidden = false;
    std::string texturePath; // Default texture for all faces
    std::array<std::string, 6> faceTextures = {}; // Textures for each face: +X, -X, +Y, -Y, +Z, -Z
    std::array<FaceUV, 6> faceUV = {};
    glm::vec2 uvOffset = glm::vec2(0.0f);
    glm::vec2 uvScale = glm::vec2(1.0f);
    float uvRotation = 0.0f;
    bool dirty = false; // Whether the volume has been modified and needs to be reprocessed

    bool isValid() const
    {
        return (maxs.x - mins.x) > 1e-4f &&
               (maxs.y - mins.y) > 1e-4f &&
               (maxs.z - mins.z) > 1e-4f;
    }

    glm::vec3 center() const { return (mins + maxs) * 0.5f; }
    glm::vec3 size()   const { return maxs - mins; }
};

struct PendingBrush
{
    bool active = false;
    EditorViewType view = EditorViewType::Top;
    glm::vec3 start = glm::vec3(0.0f);
};

struct EditorView
{
    EditorViewType type = EditorViewType::Top;
    const char *label = "";
    RectI rect = {};
    Camera camera;
    glm::vec3 focus = glm::vec3(0.0f);
    glm::vec4 clearColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
    float orthoSize = 256.0f;
    float perspectiveDistance = 720.0f;
    float perspectiveYaw = 45.0f;
    float perspectivePitch = 28.0f;
};

struct AssetEntry
{
    std::string name;
    std::string path;
};
