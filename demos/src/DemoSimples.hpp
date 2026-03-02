#pragma once
#include "DemoBase.hpp"
// ============================================================
//  DemoSimples — shadow map simples com luz direcional
// ============================================================
class DemoSimples : public DemoBase
{
public:
    const char *name() override { return "Simple Shadow"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 15.f, 30.f});
        camera->lookAt({0.f, 0.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 20.f;

        // ── Shaders ──────────────────────────────────────────
        auto *depthShader = shaders().load("depth",
            "assets/shaders/depth.vert", "assets/shaders/depth.frag");
        auto *litShader   = shaders().load("lit_shadow",
            "assets/shaders/lit_shadow.vert", "assets/shaders/lit_shadow.frag");
        if (!depthShader || !litShader) { SDL_Log("[ERR] Failed to load shaders"); return false; }

        // ── Materials ─────────────────────────────────────────
        auto *white   = textures().getWhite();
        auto *texWall = textures().load("tex_wall", "assets/wall.jpg");

        auto *matGround = materials().create("sd_ground");
        matGround->setShader(litShader)->setTexture("u_albedo", texWall ? texWall : white);
        auto *matCube = materials().create("sd_cube");
        matCube->setShader(litShader)->setTexture("u_albedo", white);

        // ── Meshes ───────────────────────────────────────────
        Mesh *plane = meshes().create_plane("ground", 40.f, 40.f, 1);
        Mesh *cube  = meshes().create_cube ("cube",   2.f);

        auto *ground = scene.createMeshNode("ground", plane);
        ground->setMaterial("sd_ground");

        for (int i = 0; i < 5; i++)
        {
            auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
            node->setMaterial("sd_cube");
            node->setPosition({(float)(i - 2) * 5.f, 1.f, 0.f});
        }

        // ── Shadow simples ────────────────────────────────────
        scene.shadow.enabled     = true;
        scene.shadow.depthShader = depthShader;
        scene.shadow.lightDir    = glm::normalize(glm::vec3(1.f, 3.f, 1.f));
        scene.shadow.lightColor  = {1.f, 1.f, 0.95f};
        scene.shadow.numCascades = 1;   // 1 = shadow map simples
        scene.shadow.mapSize     = 2048;
        scene.shadow.bias        = 0.005f;

        return true;
    }

    void update(float dt) override { DemoBase::update(dt); }
    void render() override         { DemoBase::render(); }
    void release() override        { DemoBase::release(); }
};
