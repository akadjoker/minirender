#pragma once
#include "DemoBase.hpp"
// ============================================================
//  DemoSimples — CSM (Cascade Shadow Maps) demo
// ============================================================
class DemoSimples : public DemoBase
{
public:
    const char *name() override { return "CSM Shadow"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 15.f, 30.f});
        camera->lookAt({0.f, 0.f, 0.f});
        camera->setViewPlanes(0.1f, 300.f);
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 20.f;

        // ── Shaders ──────────────────────────────────────────
        auto *depthShader = shaders().load("depth",
            "assets/shaders/depth.vert", "assets/shaders/depth.frag");
        auto *litShader   = shaders().load("csm_lit",
            "assets/shaders/csm_lit.vert", "assets/shaders/csm_lit.frag");
        if (!depthShader || !litShader)
        {
            SDL_Log("[ERR] Failed to load shaders");
            return false;
        }

        // ── Materials ─────────────────────────────────────────
        auto *white   = textures().getWhite();
        auto *texWall = textures().load("tex_wall", "assets/wall.jpg");

        auto *matGround = materials().create("sd_ground");
        matGround->setShader(litShader)->setTexture("u_albedo", texWall ? texWall : white);
        auto *matCube = materials().create("sd_cube");
        matCube->setShader(litShader)->setTexture("u_albedo", white);

        // ── Meshes ───────────────────────────────────────────
        Mesh *plane = meshes().create_plane("ground", 80.f, 80.f, 1);
        Mesh *cube  = meshes().create_cube ("cube",   2.f);

        auto *ground = scene.createMeshNode("ground", plane);
        ground->setMaterial("sd_ground");

        for (int i = 0; i < 9; i++)
        {
            auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
            node->setMaterial("sd_cube");
            node->setPosition({(float)(i - 4) * 8.f, 1.f, 0.f});
        }

        // ── CSM Shadow ────────────────────────────────────────
        scene.shadow.enabled     = true;
        scene.shadow.depthShader = depthShader;
        scene.shadow.lightDir    = glm::normalize(glm::vec3(1.f, 3.f, 1.f));
        scene.shadow.lightColor  = {1.f, 1.f, 0.95f};
        scene.shadow.numCascades = 3;
        scene.shadow.mapSize     = 1024;
        scene.shadow.bias        = 0.005f;
        scene.shadow.lambda      = 0.75f;  // mix log/uniform splits

        return true;
    }

    void update(float dt) override { DemoBase::update(dt); }
    void render() override         { DemoBase::render(); }
    void release() override        { DemoBase::release(); }
};
