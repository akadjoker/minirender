#pragma once
#include "DemoBase.hpp"
// ============================================================
//  DemoSponza
// ============================================================
class DemoSponza : public DemoBase
{
public:
    const char *name() override { return "Sponza Demo"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 15.f, 30.f});
        camera->lookAt({0.f, 0.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 20.f;
  
 
      Shader *shader = shaders().load("unlit",
                                  "assets/shaders/unlit.vert",
                                  "assets/shaders/unlit.frag");
    if (!shader)
      {
        SDL_Log("[ERR] Failed to load unlit shader");
        return false;
      }

    // Definir shader e textura fallback UMA VEZ — o loader aplica automaticamente
    auto &materials = MaterialManager::instance();
    materials.setDefaults(shader, TextureManager::instance().getWhite());

    // ── Meshes ───────────────────────────────────────────
    auto &meshes = MeshManager::instance();

    // Sponza — texturas na mesma pasta que o .obj (deteção automática)
    Mesh *sponza = meshes.load("sponza", "assets/obj/sponza.obj", "assets/textures");
    if (!sponza)
    {
        SDL_Log("[ERR] Falhou ao carregar sponza.obj");
        return -1;
    }



     MeshNode *sponzaNode = scene.createMeshNode("sponza", sponza);
    sponzaNode->passMask = RenderPassMask::Opaque;

 
 
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);

        
    }

    void render() override
    {
        DemoBase::render();
    }

    void release() override
    {
        DemoBase::release();
    }

private:

};
