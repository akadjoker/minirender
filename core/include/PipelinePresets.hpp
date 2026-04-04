#pragma once

#include "CascadeShadowMap.hpp"
#include "RenderPipeline.hpp"

class Scene;

namespace PipelinePresets
{
struct WorldForwardDesc
{
    Shader *depthShader = nullptr;
    Shader *skyShader = nullptr;
    Shader *litShader = nullptr;
    Shader *instancedDepthShader = nullptr;

    unsigned int shadowResolution = 2048;
    glm::vec3 lightDirection      = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));
    float     csmLambda           = 0.75f;
    float     shadowFarPlane      = 300.f;

    glm::vec3 skyTop              = {0.18f, 0.36f, 0.72f};
    glm::vec3 skyHorizon          = {0.62f, 0.78f, 0.90f};
    glm::vec3 groundColor         = {0.20f, 0.18f, 0.14f};
};

struct WorldForwardPipeline
{
    CsmTechnique *mainTechnique = nullptr;
};

WorldForwardPipeline createWorldForward(Scene &scene,
                                        const WorldForwardDesc &desc = {});

struct IndoorHybridDesc
{
    Shader *gbufferShader  = nullptr;
    Shader *lightingShader = nullptr;

    glm::vec4 clearColor   = {0.10f, 0.10f, 0.12f, 1.0f};
};

struct IndoorHybridPipeline
{
    DeferredTechnique *mainTechnique = nullptr;
};

IndoorHybridPipeline createIndoorHybrid(Scene &scene,
                                        const IndoorHybridDesc &desc = {});
}