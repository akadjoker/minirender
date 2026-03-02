#pragma once
#include "DemoBase.hpp"
#include "TerrainNode.hpp"
#include "Input.hpp"
#include <cstdlib>
#include <cmath>

// ============================================================
//  DemoPerformance
//  Stress-test: terrain LOD + N mesh instances spread across it.
//  [+] / [=]  — add 50 boxes         [-]   — remove 50 boxes
//  [D]        — toggle terrain debug boxes
//  [F]        — toggle frustum culling label (compare FPS!)
//  FPS / DC / Tris are shown in the main overlay.
// ============================================================
class DemoPerformance : public DemoBase
{
public:
    const char *name() override { return "Performance Test"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({ 0.f, 80.f, 120.f });
        camera->lookAt({ 0.f, 0.f, 0.f });
        camera->farPlane  = 2000.f;
        camera->nearPlane = 0.1f;
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 80.f;

        // ── Shaders ─────────────────────────────────────────
        litShader_ = shaders().load("simple_lit",
            "assets/shaders/simple_lit.vert",
            "assets/shaders/simple_lit.frag");
        skyShader_ = shaders().load("sky",
            "assets/shaders/sky.vert",
            "assets/shaders/sky.frag");
        if (!litShader_ || !skyShader_)
            return false;

        // ── Materials ────────────────────────────────────────
        auto *white   = textures().getWhite();
        auto *texRock = textures().load("tex_rock", "assets/terrain-texture.jpg");
        auto *texWall = textures().load("tex_wall",  "assets/wall.jpg");

        auto *matTerrain = materials().create("perf_terrain");
        matTerrain->setShader(litShader_)->setTexture("u_albedo", texRock ? texRock : white);

        auto *matBox = materials().create("perf_box");
        matBox->setShader(litShader_)->setTexture("u_albedo", texWall ? texWall : white);

        // ── Terrain LOD ──────────────────────────────────────
        terrain_ = new TerrainLodNode("terrain");
        terrain_->setScale({ 1000.f, 100.f, 1000.f });
        if (terrain_->loadFromHeightmap("assets/terrain-heightmap.png"))
        {
            terrain_->setMaterial(matTerrain);
            terrain_->setPosition({ -500.f, -100.f, -500.f });
            scene.add(terrain_);
        }
        else
        {
            delete terrain_;
            terrain_ = nullptr;
        }

        // Shared cube mesh — N nodes all point to the same geometry.
        // One draw call per node (tests driver/CPU overhead).
        cubeMesh_ = meshes().create_cube("perf_cube", 4.f);
        // Assign the box material to the cube mesh material list
        if (cubeMesh_)
            cubeMesh_->materials.assign(1, matBox);

        // Technique
        auto *fwd = new ForwardTechnique();
        fwd->addPass<SkyPass>()->shader = skyShader_;
        scene.addTechnique(fwd);

        // Spawn initial batch
        spawnBatch(BATCH_SIZE);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);

        // Add / remove batches (held = keeps adding, use key-repeat naturally)
        if (Input::IsKeyDown(KEY_P))   // [=] / [+]
            spawnBatch(BATCH_SIZE);

        if (Input::IsKeyPressed(KEY_L))   // [-]
            removeBatch(BATCH_SIZE);

        // Toggle debug boxes once per press
        static bool dWasDown = false;
        bool dDown = Input::IsKeyDown(KEY_D);
        if (dDown && !dWasDown)
        {
            debugBoxes_ = !debugBoxes_;
            if (terrain_) terrain_->debugDraw = debugBoxes_;
        }
        dWasDown = dDown;

        static bool fWasDown = false;
        bool fDown = Input::IsKeyDown(KEY_F);
        if (fDown && !fWasDown)
        {
            frustumEnabled_ = !frustumEnabled_;
            // Setting a huge move delta effectively disables the LOD recalc skip
            // so we don't confuse it with frustum culling toggle.
            // To truly disable frustum you'd need a flag in TerrainLodNode — this
            // just shows the perf difference with the early-return on/off.
            if (terrain_)
                terrain_->setCameraMovementDelta(frustumEnabled_ ? 10.f : 0.001f);
        }
        fWasDown = fDown;
    }

    void render() override
    {
        const glm::vec4 camPos     = { camera->position, 1.f };
        const glm::vec4 lightDir   = { glm::normalize(glm::vec3(0.6f, 1.f, 0.5f)), 0.f };
        const glm::vec4 lightColor = { 1.f, 0.95f, 0.85f, 1.f };
        const glm::vec4 ambient    = { 0.10f, 0.12f, 0.16f, 1.f };

        auto &rs = RenderState::instance();

        rs.useProgram(litShader_->getId());
        litShader_->setVec4("u_cameraPos",  camPos);
        litShader_->setVec4("u_lightDir",   lightDir);
        litShader_->setVec4("u_lightColor", lightColor);
        litShader_->setVec4("u_ambient",    ambient);

        rs.useProgram(skyShader_->getId());
        skyShader_->setMat4("u_invViewProj", glm::inverse(camera->viewProjection));
        skyShader_->setVec4("u_cameraPos",  camPos);
        skyShader_->setVec4("u_lightDir",   lightDir);
        skyShader_->setVec4("u_lightColor", lightColor);

        DemoBase::render();
    }

    void release() override
    {
        terrain_  = nullptr;   // owned by scene
        nodes_.clear();        // owned by scene
        DemoBase::release();
    }

    bool showDebug()     const { return debugBoxes_; }
    int  instanceCount() const { return (int)nodes_.size(); }

private:
    static constexpr int BATCH_SIZE = 50;

    void spawnBatch(int n)
    {
        if (!cubeMesh_) return;
        const float range = 450.f;
        SDL_Log("Spawning batch of %d boxes (total %d)", n, (int)nodes_.size() + n);

        for (int i = 0; i < n; ++i)
        {
            float wx = ((float)std::rand() / (float)RAND_MAX) * range * 2.f - range;
            float wz = ((float)std::rand() / (float)RAND_MAX) * range * 2.f - range;
            float wy = terrain_ ? terrain_->getHeightAt(wx, wz) + 2.f : 2.f;

            auto *node = scene.createMeshNode(
                "box_" + std::to_string(nodes_.size()), cubeMesh_);
            node->setPosition({ wx, wy, wz });
            node->passMask = RenderPassMask::Opaque;
            nodes_.push_back(node);
        }
    }

    void removeBatch(int n)
    {
        for (int i = 0; i < n && !nodes_.empty(); ++i)
        {
            scene.markForRemoval(nodes_.back());
            nodes_.pop_back();
        }
    }

    TerrainLodNode        *terrain_   = nullptr;
    Mesh                  *cubeMesh_  = nullptr;
    Shader                *litShader_ = nullptr;
    Shader                *skyShader_ = nullptr;
    std::vector<MeshNode *> nodes_;
    bool debugBoxes_     = false;
    bool frustumEnabled_ = true;
};

