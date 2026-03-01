#pragma once
#include "DemoBase.hpp"
#include "CascadeShadowMap.hpp"
#include "RenderState.hpp"
#include "Effects.hpp"

// ============================================================
//  DemoCascade — 4-cascade shadow maps
//  Shows large scene illuminated by a single directional sun
//  with hi-res close shadows fading to cheaper distant cascades.
// ============================================================
class DemoCascade : public DemoBase
{
public:
    const char *name() override { return "Cascade Shadow Maps"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 20.f, 60.f});
        camera->lookAt({0.f, 0.f, 0.f});
        camera->farPlane = 400.f;

        // ── Shaders ─────────────────────────────────────────
        auto *depthShader = shaders().load("csm_depth",
            "assets/shaders/csm_depth.vert","assets/shaders/csm_depth.frag");
        litShader_ = shaders().load("csm_lit",
            "assets/shaders/csm_lit.vert","assets/shaders/csm_lit.frag");
        skyShader_ = shaders().load("sky",
            "assets/shaders/sky.vert","assets/shaders/sky.frag");
        if (!depthShader || !litShader_ || !skyShader_) return false;

        // Wire the SceneBlock UBO (binding 0) for every shader that uses it.
        litShader_->bindBlock("SceneBlock", SceneUBO::BINDING);
        skyShader_->bindBlock("SceneBlock", SceneUBO::BINDING);

        glm::vec3 lightDir = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));

        // ── Scene-wide light data (uploaded via UBO, not per-material) ────────
        scene.sceneData.lightDir   = glm::vec4(-lightDir, 0.f);
        scene.sceneData.lightColor = glm::vec4(1.f, 1.f, 0.95f, 1.f);
        scene.sceneData.ambient    = glm::vec4(0.1f, 0.12f, 0.15f, 0.f);

        // ── Textures ─────────────────────────────────────────
        auto *texGround = textures().load("tex_ground","assets/wall.jpg");
        auto *texCube   = textures().load("tex_cube",  "assets/noise.jpg");
        auto *white     = textures().getWhite();

        // ── Materials (only per-object state: texture + toggle) ─────────────
        matGround_ = materials().create("csm_ground");
        matGround_->setShader(litShader_)
                  ->setTexture("u_albedo", texGround ? texGround : white)
                  ->setBool("u_showCascades", false);

        matCube_ = materials().create("csm_cube");
        matCube_->setShader(litShader_)
                ->setTexture("u_albedo", texCube ? texCube : white)
                ->setBool("u_showCascades", false);

        // ── Scene geometry ─────────────────────────────────────
        buildGround(matGround_);
        buildCubes(matCube_);

        // ── CSM technique ─────────────────────────────────────
        auto *tech = new CsmTechnique();
        if (!tech->initialize(2048))
        {
            delete tech;
            return false;
        }
        csm_ = tech;

        tech->litShader = litShader_;
        tech->getCsm()->setLightDirection(lightDir);
        tech->getCsm()->setShadowFarPlane(100.f);
        tech->getCsm()->setLambda(0.0f); 

        // Add depth passes for each cascade
        for (int i = 0; i < CSM_NUM_CASCADES; ++i)
        {
            auto *dp  = tech->addPass<CsmDepthPass>();
            dp->csm     = tech->getCsm();
            dp->cascade = i;
            dp->shader  = depthShader;
        }
        // Opaque lit pass
        auto *op = tech->addPass<OpaquePass>();
        op->shader = nullptr;  // defer to material shaders (csm_lit)

        // Transparent pass
        tech->addPass<TransparentPass>();

        // Sky rendered last — fills pixels with no geometry (depth = 1.0)
        auto *sky = tech->addPass<SkyPass>();
        sky->shader = skyShader_;

        scene.addTechnique(tech);
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt * 0.05f;
        float angle  = time_ * 0.05f;
        glm::vec3 dir = glm::normalize(glm::vec3(std::sin(angle), -0.8f, std::cos(angle)));
        csm_->getCsm()->setLightDirection(dir);

        // Update scene-wide light direction via UBO — ONE call, any number of objects.
        scene.sceneData.lightDir = glm::vec4(-dir, 0.f);

        // C = toggle cascade debug colours
        if (Input::IsKeyPressed(KEY_C))
        {
            showCascades_ = !showCascades_;
            if (matGround_) matGround_->setBool("u_showCascades", showCascades_);
            if (matCube_)   matCube_->setBool("u_showCascades",   showCascades_);
        }
    }

    void render()  override { DemoBase::render(); }
    void release() override { DemoBase::release(); }

private:
    CsmTechnique *csm_          = nullptr;
    Shader       *litShader_    = nullptr;
    Shader       *skyShader_    = nullptr;
    Material     *matGround_    = nullptr;
    Material     *matCube_      = nullptr;
    float         time_         = 0.f;
    bool          showCascades_ = false;

    void buildGround(Material *mat)
    {
        auto *node = scene.createManualMeshNode("csm_ground");
        node->material = mat;
        node->begin(GL_TRIANGLES);
        float s = 250.f;
        node->normal(0,1,0).texCoord(0,0).position(-s,0,-s);
        node->normal(0,1,0).texCoord(20,0).position(s,0,-s);
        node->normal(0,1,0).texCoord(20,20).position(s,0,s);
        node->normal(0,1,0).texCoord(0,20).position(-s,0,s);
        node->triangle(2,1,0).triangle(0,3,2);
        node->end();
    }

    void buildCubes(Material *mat)
    {
        for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 6; ++col)
        {
            float x = (col - 3) * 20.f;
            float z = (row - 3) * 20.f;
            float h = 2.f + (float)((row * 7 + col * 3) % 5) * 1.5f;

            auto *node = scene.createManualMeshNode(
                "cube_" + std::to_string(row) + "_" + std::to_string(col));
            node->material = mat;
            buildBox(node, {x, h*0.5f, z}, 2.f, h, 2.f);
        }
    }

    void buildBox(ManualMeshNode *node, glm::vec3 centre, float w, float h, float d)
    {
        float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;

        node->begin(GL_TRIANGLES);
        struct Face { glm::vec3 n; glm::vec3 verts[4]; glm::vec2 uvs[4]; };
        std::vector<Face> faces = {
            {{0,0,-1},{{-hw,-hh,-hd},{hw,-hh,-hd},{hw,hh,-hd},{-hw,hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,0, 1},{{hw,-hh,hd},{-hw,-hh,hd},{-hw,hh,hd},{hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{-1,0,0},{{-hw,-hh,hd},{-hw,-hh,-hd},{-hw,hh,-hd},{-hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{1,0,0}, {{hw,-hh,-hd},{hw,-hh,hd},{hw,hh,hd},{hw,hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,1,0}, {{-hw,hh,-hd},{hw,hh,-hd},{hw,hh,hd},{-hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,-1,0},{{-hw,-hh,hd},{hw,-hh,hd},{hw,-hh,-hd},{-hw,-hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
        };
        for (const auto &f : faces)
        {
            uint32_t base = (uint32_t)node->vertexCount();
            for (int i = 0; i < 4; ++i)
                node->normal(f.n.x,f.n.y,f.n.z)
                     .texCoord(f.uvs[i].x,f.uvs[i].y)
                     .position(centre.x+f.verts[i].x, centre.y+f.verts[i].y, centre.z+f.verts[i].z);
            node->triangle(base+2,base+1,base).triangle(base,base+3,base+2);
        }
        node->end();
    }
};
