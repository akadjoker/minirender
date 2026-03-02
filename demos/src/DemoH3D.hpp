#pragma once
#include "DemoBase.hpp"
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
        litShader = shaders().load("lit_shadow",
                                   "assets/shaders/lit_shadow.vert",
                                   "assets/shaders/lit_shadow.frag");
        if (!litShader)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoH3D] Failed to load shaders");
            return false;
        }

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


        return true;
    }

    void update(float dt) override { DemoBase::update(dt); }
    void render() override         { DemoBase::render(); }

    void release() override
    {
        DemoBase::release();
    }

private:
    Shader          *litShader   = nullptr;
};
