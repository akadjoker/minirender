#pragma once
#include "DemoBase.hpp"
#include "WaterNode.hpp"
#include "TerrainNode.hpp"

 
class DemoTerrainLod : public DemoBase
{
public:
    const char *name() override { return "Terrain LOD"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 30.f, 60.f});
        camera->lookAt({0.f, 5.f, 0.f});
        camera->farPlane  = 1600.f;
        camera->nearPlane = 0.1f;

        // ── Shaders ─────────────────────────────────────────
        litShader_   = shaders().load("simple_lit",
            "assets/shaders/simple_lit.vert", "assets/shaders/simple_lit.frag");
        
        skyShader_   = shaders().load("sky",
            "assets/shaders/sky.vert", "assets/shaders/sky.frag");
        if (!litShader_  || !skyShader_)
            return false;

      
   

        // ── Texturas ──────────────────────────────────────────
        auto *texRock = textures().load("tex_rock", "assets/terrain-texture.jpg");
        auto *texWall = textures().load("tex_wall", "assets/wall.jpg");
        auto *white   = textures().getWhite();

        // ── Materiais ─────────────────────────────────────────
        auto *matTerrain = materials().create("wt_terrain");
        matTerrain->setShader(litShader_)->setTexture("u_albedo", texRock ? texRock : white);

        auto *matBox = materials().create("wt_box");
        matBox->setShader(litShader_)->setTexture("u_albedo", texWall ? texWall : white);

        // ── Terreno bruteforce ────────────────────────────────
        terrain_ = new TerrainLodNode("terrain");
        terrain_->setScale({1000.f, 100.f, 1000.f});
        if (!terrain_->loadFromHeightmap("assets/terrain-heightmap.png"))
        {
            delete terrain_;
            terrain_ = nullptr;
        }
        else
        {
            terrain_->setMaterial(matTerrain);
            terrain_->setPosition({-500.f,-100.f, -500.f});  // centre at origin
            scene.add(terrain_);
        }

      

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

           
    }

    void render()  override 
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

        DemoBase::render(); 
    }
    void release() override { DemoBase::release(); }

private:
    TerrainLodNode *terrain_     = nullptr;
 
    Shader      *litShader_   = nullptr;
 
    Shader      *skyShader_   = nullptr;
    float        time_        = 1.0f;
 
};

