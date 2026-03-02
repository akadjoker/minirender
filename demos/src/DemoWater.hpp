#pragma once
#include "DemoBase.hpp"
#include "WaterNode.hpp"
#include "TerrainNode.hpp"
#include "DebugDraw.hpp"
#include "RenderState.hpp"
#include <glm/gtc/matrix_inverse.hpp>

// ============================================================
//  DemoWater — cena limpa: TerrainNode + caixas + água reflectiva
//  Sem CSM. Usa ForwardTechnique (Opaque+Transparent) + SkyPass.
// ============================================================
class DemoWater : public DemoBase
{
public:
    const char *name() override { return "Water Reflection+Refraction"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 20.f, 60.f});
        camera->lookAt({0.f, 4.f, 0.f});
        camera->farPlane  = 1000.f;
        camera->nearPlane = 0.1f;

        // ── Shaders ─────────────────────────────────────────
        litShader_   = shaders().load("simple_lit",
            "assets/shaders/simple_lit.vert", "assets/shaders/simple_lit.frag");
        waterShader_ = shaders().load("water",
            "assets/shaders/water.vert", "assets/shaders/water.frag");
        skyShader_   = shaders().load("sky",
            "assets/shaders/sky.vert", "assets/shaders/sky.frag");
        if (!litShader_ || !waterShader_ || !skyShader_)
            return false;

 
        // ── Texturas ──────────────────────────────────────────
        auto *texRock = textures().load("tex_rock", "assets/terrain-texture.jpg");
        auto *texWall = textures().load("tex_wall", "assets/wall.jpg");
        auto *white   = textures().getWhite();

        // ── Materiais ─────────────────────────────────────────
        auto *matTerrain = materials().create("wt_terrain");
        matTerrain->setShader(litShader_)
                  ->setTexture("u_albedo", texRock ? texRock : white);

        auto *matBox = materials().create("wt_box");
        matBox->setShader(litShader_)
              ->setTexture("u_albedo", texWall ? texWall : white);

        // ── Terreno bruteforce ────────────────────────────────
        terrain_ = new TerrainNode("terrain");
        if (!terrain_->loadFromHeightmap("assets/terrain-heightmap.png",
                                          300.f, 20.f, 300.f, 1.f, 1.f))
        {
            delete terrain_;
            terrain_ = nullptr;
        }
        else
        {
            terrain_->setMaterial(matTerrain);
            // centre the 100×100 terrain around world origin
            terrain_->setPosition({-50.f, 0.f, -50.f});
            scene.add(terrain_);
        }

        // ── Caixas em redor do lago ───────────────────────────
        buildIslandBoxes(matBox);

        // ── Água a Y=5 (nível do lago) ────────────────────────
        water_ = scene.createWaterNode("water");
        water_->rtWidth          = 512;
        water_->rtHeight         = 512;
        water_->reflectivity     = 0.6f;
        water_->distortStrength  = 0.025f;
        water_->waveHeight       = 0.15f;
        water_->waveLength       = 0.1f;    // bump UV scale
        water_->windForce        = 1.0f;
        water_->windDirection    = {1.f, 0.f};
        water_->foamIntensity    = 0.7f;
        water_->colorBlendFactor = 0.15f;
        water_->waterColor       = {0.04f, 0.12f, 0.45f, 1.f};
        water_->setPosition({0.f, 5.f, 0.f});
        if (!water_->init(120.f, 120.f, waterShader_))
            return false;

        // ── ForwardTechnique + Sky ────────────────────────────
        auto *fwd = new ForwardTechnique();
        fwd->addPass<SkyPass>()->shader = skyShader_;
        scene.addTechnique(fwd);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt * 0.12f;

        if (Input::IsKeyPressed(KEY_F1))
            showDebug_ = !showDebug_;

        if (Input::IsKeyPressed(KEY_F2))
            scene.debugRTGather = true; // prints once next renderToTarget call
    }

    void render() override
    {
        const glm::vec4 camPos     = glm::vec4(camera->position, 1.f);
        const glm::vec4 lightDir   = glm::vec4(glm::normalize(glm::vec3(0.6f, 1.f, 0.5f)), 0.f);
        const glm::vec4 lightColor = glm::vec4(1.f, 0.95f, 0.85f, 1.f);
        const glm::vec4 ambient    = glm::vec4(0.10f, 0.12f, 0.16f, 1.f);
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

        rs.useProgram(waterShader_->getId());
        waterShader_->setVec4("u_cameraPos",  camPos);
        waterShader_->setVec4("u_lightDir",   lightDir);
        waterShader_->setVec4("u_lightColor", lightColor);

        DemoBase::render();

        // ── Debug overlay: reflection | refraction | depth ────
        // Toggle with showDebug_ (press F1 in update if you wire it up)
        if (water_ && showDebug_)
        {
            const int vpW = camera->viewport.z;
            const int vpH = camera->viewport.w;
            auto &dd = DebugDraw::instance();
            const int qh = vpH / 5;          // quad height = 1/5 of screen
            const int qw = qh * 16 / 9;     // keep 16:9 aspect
            int xOff = 4;

            if (water_->debugReflTex())
                dd.drawQuad(xOff, 4, qw, qh, water_->debugReflTex()->id, vpW, vpH);
            xOff += qw + 4;

            if (water_->debugRefrTex())
                dd.drawQuad(xOff, 4, qw, qh, water_->debugRefrTex()->id, vpW, vpH);
            xOff += qw + 4;

            if (water_->debugRefrDepthTex())
                dd.drawQuad(xOff, 4, qw, qh, water_->debugRefrDepthTex()->id,
                            vpW, vpH, /*isDepth=*/true);
        }
    }

    void release() override { DemoBase::release(); }

private:
    TerrainNode *terrain_     = nullptr;
    WaterNode3D *water_       = nullptr;
    Shader      *litShader_   = nullptr;
    Shader      *waterShader_ = nullptr;
    Shader      *skyShader_   = nullptr;
    float        time_        = 1.0f;
    bool         showDebug_   = true;  // press F1 in update() to toggle

    void buildIslandBoxes(Material *mat)
    {
        struct BoxDef { glm::vec3 pos; glm::vec3 sz; };
        static const BoxDef kBoxes[] = {
            {{ -45.f, 5.f,  30.f}, { 5.f, 12.f, 5.f}},
            {{  50.f, 5.f,  20.f}, { 6.f,  8.f, 6.f}},
            {{ -30.f, 5.f, -45.f}, { 4.f, 16.f, 4.f}},
            {{  35.f, 5.f, -35.f}, { 7.f,  6.f, 7.f}},
            {{ -55.f, 5.f,  -5.f}, { 5.f, 20.f, 5.f}},
            {{  10.f, 5.f,  55.f}, { 8.f,  5.f, 8.f}},
            {{ -15.f, 5.f,  50.f}, { 4.f, 10.f, 4.f}},
            {{  60.f, 5.f, -10.f}, { 5.f, 14.f, 5.f}},
        };
        for (int i = 0; i < (int)(sizeof(kBoxes)/sizeof(kBoxes[0])); ++i)
        {
            auto *node = scene.createManualMeshNode("wbox_" + std::to_string(i));
            node->material = mat;
            buildBox(node, kBoxes[i].pos, kBoxes[i].sz.x,
                           kBoxes[i].sz.y, kBoxes[i].sz.z);
        }
    }

    void buildBox(ManualMeshNode *node, glm::vec3 c, float w, float h, float d)
    {
        float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;
        node->begin(GL_TRIANGLES);
        struct Face { glm::vec3 n; glm::vec3 v[4]; glm::vec2 uv[4]; };
        Face faces[] = {
            {{0,0,-1},{{-hw,-hh,-hd},{hw,-hh,-hd},{hw,hh,-hd},{-hw,hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,0, 1},{{hw,-hh,hd},{-hw,-hh,hd},{-hw,hh,hd},{hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{-1,0,0},{{-hw,-hh,hd},{-hw,-hh,-hd},{-hw,hh,-hd},{-hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{1,0,0},{{hw,-hh,-hd},{hw,-hh,hd},{hw,hh,hd},{hw,hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,1,0},{{-hw,hh,-hd},{hw,hh,-hd},{hw,hh,hd},{-hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,-1,0},{{-hw,-hh,hd},{hw,-hh,hd},{hw,-hh,-hd},{-hw,-hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
        };
        for (const auto &f : faces)
        {
            uint32_t base = (uint32_t)node->vertexCount();
            for (int i = 0; i < 4; ++i)
                node->normal(f.n.x, f.n.y, f.n.z)
                     .texCoord(f.uv[i].x, f.uv[i].y)
                     .position(c.x+f.v[i].x, c.y+f.v[i].y, c.z+f.v[i].z);
            node->triangle(base+2,base+1,base).triangle(base,base+3,base+2);
        }
        node->end();
    }
};

