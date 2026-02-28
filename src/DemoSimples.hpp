#pragma once
#include "DemoBase.hpp"
#include "ShadowMap.hpp"

// ============================================================
//  DemoSimples — shadow map com luz direccional
// ============================================================
class DemoSimples : public DemoBase
{
public:
    const char *name() override { return "Simple Demo"; }

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

   

    forwardTechnique = new ForwardTechnique();

    scene.addTechnique(forwardTechnique);


        Mesh *plane = meshes.create_plane("ground", 40.f, 40.f, 1);
        Mesh *cube = meshes.create_cube("cube", 2.f);

        // Assign default texture (u_albedo white) to all materials created above
       materials.applyDefaults();

        scene.createMeshNode("ground", plane);

        for (int i = 0; i < 5; i++)
        {
            auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
            node->setPosition({(float)(i - 2) * 5.f, 1.f, 0.f});
        }
  
 
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
        delete forwardTechnique;
        forwardTechnique = nullptr;
        DemoBase::release();
    }

private:
         ForwardTechnique *forwardTechnique;
};
