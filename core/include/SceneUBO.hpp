#pragma once
#include "glad/glad.h"
#include <glm/glm.hpp>

// ============================================================
//  SceneData / SceneUBO
//  Per-frame scene uniform data uploaded ONCE per frame into a
//  Uniform Buffer Object at binding point 0.
//
//  Any shader that wants these values declares:
//    layout(std140) uniform SceneBlock { ... };   (same field order)
//  and (once, after loading) calls:
//    shader->bindBlock("SceneBlock", SceneUBO::BINDING);
//
//  The demo / Scene fills in sceneData fields (lightDir, etc.)
//  and Scene::renderCamera() fills view/proj/viewProj/cameraPos
//  automatically from the active camera before rendering.
// ============================================================

struct SceneData
{
    glm::mat4 view;         //   0 – 63   camera view matrix
    glm::mat4 proj;         //  64 – 127  camera projection matrix
    glm::mat4 viewProj;     // 128 – 191  view * projection
    glm::mat4 invViewProj;  // 192 – 255  inverse(viewProj) — for sky ray reconstruction
    glm::vec4 cameraPos;    // 256 – 271  xyz=world pos, w=1
    glm::vec4 lightDir;     // 272 – 287  xyz=direction TOWARD light (normalised), w=0
    glm::vec4 lightColor;   // 288 – 303  xyz=rgb, w=intensity
    glm::vec4 ambient;      // 304 – 319  xyz=color, w=0
};
static_assert(sizeof(SceneData) == 320, "SceneData std140 layout mismatch");

class SceneUBO
{
public:
    // All shaders bind to this point.
    static constexpr GLuint BINDING = 0;

    bool create();
    void destroy();

    /// Upload data and keep it bound at BINDING for this frame.
    void upload(const SceneData &data);

private:
    GLuint ubo_ = 0;
};
