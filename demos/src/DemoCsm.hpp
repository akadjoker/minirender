#pragma once
#include "DemoBase.hpp"
#include "Input.hpp"
// ============================================================
//  DemoCsm — Cascade Shadow Maps
//  numCascades=4 corresponde a #define NUM_CASCADES 4 no shader
//  Tecla C — toggle cores por cascata (debug)
// ============================================================
class DemoCsm : public DemoBase
{
public:
    const char *name() override { return "CSM Shadow"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 40.f, 80.f});
        camera->lookAt({0.f, 0.f, 0.f});
        camera->setViewPlanes(0.5f, 500.f);
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 30.f;

        // ── Shaders ──────────────────────────────────────────
        auto *depthShader = shaders().load("depth",
            "assets/shaders/depth.vert", "assets/shaders/depth.frag");
        auto *csmShader   = shaders().load("csm_lit",
            "assets/shaders/csm_lit.vert", "assets/shaders/csm_lit.frag");
        if (!depthShader || !csmShader) { SDL_Log("[ERR] CSM shaders"); return false; }

        // ── Materials ─────────────────────────────────────────
        auto *white   = textures().getWhite();
        auto *texWall = textures().load("tex_wall", "assets/wall.jpg");

        auto *matGround = materials().create("csm_ground");
        matGround->setShader(csmShader)->setTexture("u_albedo", texWall ? texWall : white);

        auto *matCube = materials().create("csm_cube");
        matCube->setShader(csmShader)->setTexture("u_albedo", white);

        // ── Meshes ───────────────────────────────────────────
        // Plano gigante para cobrir as 4 cascatas
        Mesh *plane = meshes().create_plane("csm_ground_mesh", 600.f, 600.f, 1);
        Mesh *cube  = meshes().create_cube ("csm_cube_mesh",   3.f);

        auto *ground = scene.createMeshNode("ground", plane);
        ground->setMaterial("csm_ground");

        // Cubos a distâncias crescentes para cobrir todas as cascatas
        const float zpos[] = { 5.f, 15.f, 30.f, 50.f, 80.f,
                                120.f, 170.f, 230.f,
                                300.f, 380.f, 460.f, 500.f };
        for (int i = 0; i < 12; i++)
        {
            auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
            node->setMaterial("csm_cube");
            node->setPosition({(float)(i % 3 - 1) * 10.f, 1.5f, -zpos[i]});
        }

        // ── CSM Shadow ────────────────────────────────────────
        scene.shadow.enabled      = true;
        scene.shadow.depthShader  = depthShader;
        scene.shadow.lightDir     = glm::normalize(glm::vec3(1.f, 3.f, 1.f));
        scene.shadow.lightColor   = {1.f, 1.f, 0.95f};
        scene.shadow.numCascades  = 4;      // coincide com #define NUM_CASCADES 4 no shader
        scene.shadow.mapSize      = 1024;
        scene.shadow.bias         = 0.005f;
        scene.shadow.lambda       = 0.75f;
        scene.shadow.showCascades = true;   // vermelho=0 verde=1 azul=2 amarelo=3

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        if (Input::IsKeyPressed(KEY_C))
            scene.shadow.showCascades = !scene.shadow.showCascades;
    }

    void render() override  { DemoBase::render(); }
    void release() override { DemoBase::release(); }
};
