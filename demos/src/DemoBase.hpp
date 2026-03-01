#pragma once
#include "Demo.hpp"
#include "Scene.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "RenderPipeline.hpp"
#include <glm/glm.hpp>

 
class DemoBase : public Demo
{
public:
  
    bool init() override
    {
        // Câmara default
        camera = scene.createCamera("main");
        camera->fov       = 60.f;
        camera->nearPlane = 0.1f;
        camera->farPlane  = 1000.f;
        camera->setPosition({0.f, 5.f, 20.f});
        camera->clearColor    = true;
        camera->clearColorVal = {0.1f, 0.1f, 0.15f, 1.f};
        camera->clearDepth    = true;
        camera->setController(new FreeCameraController());

        return true;
    }

    // ── Chamado pela subclasse no seu update() ───────────────
    void update(float dt) override
    {
        camera->update(dt);
        scene.update(dt);
    }

    // ── Chamado pela subclasse no seu render() ───────────────
    void render() override
    {
        scene.render();
    }

    // ── Chamado pela subclasse no seu release() ──────────────
    void release() override
    {
        scene.release();

    }

    void onResize(int w, int h) override
    {
        if (camera)
        {
            camera->setAspect(w, h);
            camera->setViewport(0, 0, w, h);
        }
    }



     Scene& getScene() override  { return scene; }

protected:
    Scene   scene;
    Camera *camera = nullptr;  // owned by scene

    // Atalhos para managers
    ShaderManager   &shaders()   { return ShaderManager::instance(); }
    MeshManager     &meshes()    { return MeshManager::instance(); }
    MaterialManager &materials() { return MaterialManager::instance(); }
    TextureManager  &textures()  { return TextureManager::instance(); }
};
