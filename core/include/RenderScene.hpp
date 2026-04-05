#pragma once

#include "Math.hpp"
#include "Mesh.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Camera;
class Light;
class Material;
class RenderableNode;

struct SkySettings
{
    bool enabled = false;
    glm::vec3 top = {0.28f, 0.48f, 0.82f};
    glm::vec3 horizon = {0.70f, 0.82f, 0.96f};
    glm::vec3 ground = {0.18f, 0.20f, 0.24f};
};

struct RenderObject
{
    RenderableNode *owner = nullptr;
    IDrawable *drawable = nullptr;
    Material *material = nullptr;
    glm::mat4 model = glm::mat4(1.0f);
    BoundingBox worldBounds;
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    bool castShadow = true;
    bool receiveShadow = true;
    float depth = 0.0f;
};

struct RenderScene
{
    Camera *camera = nullptr;
    glm::ivec4 viewport = {0, 0, 0, 0};
    bool clearColor = true;
    glm::vec4 clearColorValue = {0.1f, 0.1f, 0.1f, 1.0f};
    bool clearDepth = true;
    SkySettings sky;

    std::vector<const Light *> lights;
    std::vector<RenderObject> opaque;
    std::vector<RenderObject> water;
    std::vector<RenderObject> transparent;

    void clear()
    {
        camera = nullptr;
        viewport = {0, 0, 0, 0};
        clearColor = true;
        clearColorValue = {0.1f, 0.1f, 0.1f, 1.0f};
        clearDepth = true;
        sky = SkySettings{};
        lights.clear();
        opaque.clear();
        water.clear();
        transparent.clear();
    }
};
