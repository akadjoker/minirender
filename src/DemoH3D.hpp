#pragma once
#include "DemoBase.hpp"
#include "ShadowMap.hpp"
#include "RenderState.hpp"

// ============================================================
//  DemoH3D — carrega sponza.h3d (formato binário próprio)
// ============================================================
class DemoH3D : public DemoBase
{
public:
    const char *name() override { return "H3D Loader"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 8.f, 25.f});
        camera->lookAt({0.f, 4.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 15.f;

        // ── Shaders ──────────────────────────────────────────
        depthShader = shaders().load("depth",
                                     "assets/shaders/depth.vert",
                                     "assets/shaders/depth.frag");
        litShader = shaders().load("lit_shadow",
                                   "assets/shaders/lit_shadow.vert",
                                   "assets/shaders/lit_shadow.frag");
        if (!depthShader || !litShader)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoH3D] Failed to load shaders");
            return false;
        }

        RenderState::instance().useProgram(litShader->getId());
        litShader->setVec3 ("u_lightDir",   glm::normalize(glm::vec3(1.f, 2.f, 1.f)));
        litShader->setVec3 ("u_lightColor", {1.f, 1.f, 1.f});
        litShader->setFloat("u_shadowBias", 0.005f);
        litShader->setInt  ("u_shadowMap",  1);

        materials().setDefaults(litShader, textures().getWhite());

        // ── Carregar sponza.h3d ───────────────────────────────
        // texture_dir aponta para a pasta onde estão as texturas
        Mesh *sponza = meshes().load("sponza",
                                     "assets/models/sponza.h3d",
                                     "assets/textures");
        if (!sponza)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoH3D] Failed to load sponza.h3d");
            return false;
        }

        materials().applyDefaults();

        auto *node = scene.createMeshNode("sponza", sponza);
        node->setScale({0.05f, 0.05f, 0.05f}); // sponza costuma ser enorme

        // ── Technique ────────────────────────────────────────
        tech = new ShadowTechnique();
        tech->litShader             = litShader;
        tech->shadowPass()->shader  = depthShader;
        tech->shadowPass()->lightDir  = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));
        tech->shadowPass()->orthoSize = 60.f;
        tech->shadowPass()->lightDist = 120.f;

        scene.addTechnique(tech);
        return true;
    }

    void update(float dt) override { DemoBase::update(dt); }
    void render() override         { DemoBase::render(); }

    void release() override
    {
        delete tech;
        tech = nullptr;
        DemoBase::release();
    }

private:
    ShadowTechnique *tech        = nullptr;
    Shader          *depthShader = nullptr;
    Shader          *litShader   = nullptr;
};
