#pragma once
#include "DemoBase.hpp"
// ============================================================
//  DemoCsm — Cascade Shadow Maps (3 cascatas)
// ============================================================
class DemoCsm : public DemoBase
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
        auto *csmShader   = shaders().load("csm_lit",
            "assets/shaders/csm_lit.vert", "assets/shaders/csm_lit.frag");
        if (!depthShader || !csmShader) { SDL_Log("[ERR] Failed to load shaders"); return false; }

        // ── Materials ─────────────────────────────────────────
        auto *white   = textures().getWhite();
        auto *texWall = textures().load("tex_wall", "assets/wall.jpg");

        auto *matGround = materials().create("csm_ground");
        matGround->setShader(csmShader)->setTexture("u_albedo", texWall ? texWall : white);
        auto *matCube = materials().create("csm_cube");
        matCube->setShader(csmShader)->setTexture("u_albedo", white);

        // ── Meshes ───────────────────────────────────────────
        Mesh *plane = meshes().create_plane("ground", 80.f, 80.f, 1);
        Mesh *cube  = meshes().create_cube ("cube",   2.f);

        auto *ground = scene.createMeshNode("ground", plane);
        ground->setMaterial("csm_ground");

        for (int i = 0; i < 9; i++)
        {
            auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
            node->setMaterial("csm_cube");
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
        scene.shadow.lambda      = 0.75f;

        return true;
    }

    void update(float dt) override { DemoBase::update(dt); }
    void render() override         { DemoBase::render(); }
    void release() override        { DemoBase::release(); }
};
