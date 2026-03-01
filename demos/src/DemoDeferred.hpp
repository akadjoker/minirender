#pragma once
#include "DemoBase.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"
#include <glm/glm.hpp>
#include <SDL2/SDL.h>

// ============================================================
//  DemoDeferred — Deferred Rendering com múltiplas point lights
// ============================================================
class DemoDeferred : public DemoBase
{
public:
    const char *name() override { return "Deferred Shading"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 12.f, 28.f});
        camera->lookAt({0.f, 0.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 15.f;

        // ── Shaders ──────────────────────────────────────────
        Shader *gbufferShader = shaders().load("gbuffer",
                                               "assets/shaders/gbuffer.vert",
                                               "assets/shaders/gbuffer.frag");
        Shader *lightingShader = shaders().load("deferred_lighting",
                                               "assets/shaders/deferred_lighting.vert",
                                               "assets/shaders/deferred_lighting.frag");
        if (!gbufferShader || !lightingShader)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoDeferred] Failed to load shaders");
            return false;
        }

        // u_specular default — GBuffer shader expects it
        RenderState::instance().useProgram(gbufferShader->getId());
        gbufferShader->setFloat("u_specular", 0.5f);

        materials().setDefaults(gbufferShader, textures().getWhite());

        // ── Meshes ───────────────────────────────────────────
        Mesh *plane = meshes().create_plane("def_ground", 50.f, 50.f, 1);
        Mesh *cube  = meshes().create_cube ("def_cube",   2.f);
        materials().applyDefaults();

        scene.createMeshNode("ground", plane);

        for (int i = 0; i < 9; i++)
        {
            auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
            float angle = (float)i / 9.f * glm::two_pi<float>();
            node->setPosition({cosf(angle) * 10.f, 1.f, sinf(angle) * 10.f});
        }

        // ── Point lights ──────────────────────────────────────
        // 8 colourful lights orbiting the scene
        const glm::vec3 lightColors[8] = {
            {2.0f, 0.2f, 0.2f}, {0.2f, 2.0f, 0.2f},
            {0.2f, 0.2f, 2.0f}, {2.0f, 2.0f, 0.2f},
            {2.0f, 0.2f, 2.0f}, {0.2f, 2.0f, 2.0f},
            {2.0f, 1.0f, 0.2f}, {0.2f, 1.0f, 2.0f},
        };
        for (int i = 0; i < 8; i++)
        {
            auto *light = scene.createLight<PointLight>("light_" + std::to_string(i));
            light->color     = lightColors[i];
            light->intensity = 1.f;
            light->range     = 12.f;
            float angle      = (float)i / 8.f * glm::two_pi<float>();
            light->setPosition({cosf(angle) * 8.f, 2.f, sinf(angle) * 8.f});
            lights_[i] = light;
        }

        // ── Technique ────────────────────────────────────────
        tech = new DeferredTechnique();

        tech->geometryPass()->shader  = gbufferShader;
        tech->lightingPass()->shader  = lightingShader;
        // Directional fill light
        tech->lightingPass()->dirLightDir   = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));
        tech->lightingPass()->dirLightColor = glm::vec3(0.4f, 0.4f, 0.5f);

        scene.addTechnique(tech);
        return true;
    }

    void onResize(int w, int h) override
    {
        DemoBase::onResize(w, h);
        if (tech)
            tech->onResize((unsigned)w, (unsigned)h);
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt;

        // Rotate point lights around Y axis
        for (int i = 0; i < 8; i++)
        {
            if (!lights_[i]) continue;
            float angle = (float)i / 8.f * glm::two_pi<float>() + time_ * 0.5f;
            lights_[i]->setPosition({cosf(angle) * 8.f, 2.f + sinf(time_ + i) * 1.f,
                                     sinf(angle) * 8.f});
        }
    }

    void render() override { DemoBase::render(); }

    void release() override
    {
        delete tech;
        tech = nullptr;
        DemoBase::release();
    }

private:
    DeferredTechnique *tech = nullptr;
    PointLight        *lights_[8] = {};
    float              time_  = 0.f;
};
