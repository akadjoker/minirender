#pragma once
#include "DemoBase.hpp"
#include "Effects.hpp"
#include "ParticleNode.hpp"
#include "RenderState.hpp"

// ============================================================
//  DemoEffects — decals, lens flare, grass, manual mesh
// ============================================================
class DemoEffects : public DemoBase
{
public:
    const char *name() override { return "Effects"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 8.f, 22.f});
        camera->lookAt({0.f, 2.f, 0.f});

        // ── Shaders ─────────────────────────────────────────
        auto *grassShader = shaders().load("grass",
            "assets/shaders/grass.vert", "assets/shaders/grass.frag");
        auto *decalShader = shaders().load("decal",
            "assets/shaders/decal.vert", "assets/shaders/decal.frag");
        auto *flareShader = shaders().load("lensflare",
            "assets/shaders/lensflare.vert", "assets/shaders/lensflare.frag");
        auto *litShader   = shaders().load("lit_shadow",
            "assets/shaders/lit_shadow.vert", "assets/shaders/lit_shadow.frag");
        if (!grassShader || !decalShader || !flareShader || !litShader) return false;

        // ── Textures ─────────────────────────────────────────
        auto *texGrass   = textures().load("tex_grass",   "assets/grass1.png");
        auto *texGround  = textures().load("tex_ground",  "assets/wall.jpg");
        auto *texDecal   = textures().load("tex_decal",   "assets/decal.png");
        auto *texFlare   = textures().load("tex_flare",   "assets/flares.png");

        // ── Ground plane ─────────────────────────────────────
        {
            Material *matGround = materials().create("mat_ground");
            matGround->setShader(litShader)
                     ->setTexture("u_albedo", texGround)
                     ->setVec3("u_lightDir",  glm::normalize(glm::vec3(1,2,1)))
                     ->setVec3("u_lightColor",{1.f, 1.f, 0.95f})
                     ->setFloat("u_shadowBias", 0.005f);

            auto *node = new ManualMeshNode();
            node->name = "ground";
            node->material = matGround;
            node->begin(GL_TRIANGLES);
            float s = 20.f;
            node->normal(0,1,0).texCoord(0,0).position(-s,0,-s);
            node->normal(0,1,0).texCoord(4,0).position( s,0,-s);
            node->normal(0,1,0).texCoord(4,4).position( s,0, s);
            node->normal(0,1,0).texCoord(0,4).position(-s,0, s);
            node->triangle(2,1,0).triangle(0,3,2);
            node->end();
            scene.add(node);
        }

        // ── Grass patch ───────────────────────────────────────
        {
            Material *matGrass = materials().create("mat_grass");
            matGrass->setShader(grassShader)
                    ->setTexture("u_albedo", texGrass ? texGrass : textures().getWhite())
                    ->setCullFace(false)  // alpha-cutout — no blending needed
                    ->setVec3("u_lightDir",    glm::normalize(glm::vec3(1,2,1)))
                    ->setVec3("u_lightColor",  {1.f, 1.f, 0.9f})
                    ->setVec3("u_ambientColor",{0.2f,0.25f,0.15f});

            auto *grassNode = new GrassNode(GrassNode::GrassType::Cross);
            grassNode->name = "grass";
            grassNode->material = matGrass;
            grassNode->setWindStrength(0.25f);
            grassNode->setWindSpeed(1.5f);
            grassNode->fillArea({0,0,0}, 16.f, 16.f, 600, 0.5f, 1.2f);
            scene.add(grassNode);
            grass_ = grassNode;
        }

        // ── Decals ────────────────────────────────────────────
        {
            Material *matDecal = materials().create("mat_decal");
            matDecal->setShader(decalShader)
                    ->setTexture("u_albedo", texDecal ? texDecal : textures().getWhite())
                    ->setBlend(true)->setDepthWrite(false);

            decals_ = new DecalNode(200);
            decals_->name = "decals";
            decals_->material = matDecal;
            // Scatter some test decals on the ground
            for (int i = 0; i < 8; ++i)
            {
                float x = (float)(i - 4) * 2.5f;
                decals_->addDecal({x, 0.01f, 0.f}, {0,1,0}, {1.2f,1.2f}, {1,1,1,0.9f}, 30.f);
            }
            scene.add(decals_);
        }

        // ── Lens flare ────────────────────────────────────────
        {
            Material *matFlare = materials().create("mat_flare");
            matFlare->setShader(flareShader)
                    ->setTexture("u_albedo", texFlare ? texFlare : textures().getWhite())
                    ->setBlend(true)->setBlendFunc(GL_SRC_ALPHA, GL_ONE)   // additive
                    ->setDepthTest(false)->setDepthWrite(false);

            auto *flare = new LensFlareNode();
            flare->name = "lensflare";
            flare->sunTexture   = texFlare;   // single atlas for all elements
            flare->flareTexture = texFlare;
            flare->material     = matFlare;
            flare->setSunDirection({-0.4f, 0.8f, -0.4f});  // above horizon, upper-left
            flare->setSunColor({1.f, 0.95f, 0.7f});
            flare->initDefaultFlares();
            scene.add(flare);
        }

        // ── Manual mesh: a simple pyramid ────────────────────
        {
            Material *matPyramid = materials().create("mat_pyramid");
            matPyramid->setShader(litShader)->setTexture("u_albedo", textures().getPattern());

            auto *pyr = new ManualMeshNode();
            pyr->name = "pyramid";
            pyr->material = matPyramid;
            pyr->begin(GL_TRIANGLES);
            // Base
            pyr->normal(0,-1,0).texCoord(0,0).position(-1,0,-1);
            pyr->normal(0,-1,0).texCoord(1,0).position( 1,0,-1);
            pyr->normal(0,-1,0).texCoord(1,1).position( 1,0, 1);
            pyr->normal(0,-1,0).texCoord(0,1).position(-1,0, 1);
            pyr->triangle(0,1,2).triangle(2,3,0);
            // Apex faces
            glm::vec3 apex(0,2.5f,0);
            auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 tip)
            {
                glm::vec3 n = glm::normalize(glm::cross(b-a, tip-a));
                uint32_t base = (uint32_t)pyr->vertexCount();
                pyr->normal(n.x,n.y,n.z).texCoord(0,0).position(a.x,a.y,a.z);
                pyr->normal(n.x,n.y,n.z).texCoord(1,0).position(b.x,b.y,b.z);
                pyr->normal(n.x,n.y,n.z).texCoord(0.5f,1).position(tip.x,tip.y,tip.z);
                pyr->triangle(base+2, base+1, base);
            };
            addFace({-1,0,-1},{1,0,-1},apex);
            addFace({1,0,-1},{1,0,1},apex);
            addFace({1,0,1},{-1,0,1},apex);
            addFace({-1,0,1},{-1,0,-1},apex);
            pyr->end();
            pyr->setPosition({5, 0, -3});
            scene.add(pyr);
        }

        // ── Simple forward technique ─────────────────────────
        auto *fwd = new ForwardTechnique();
        fwd->opaque()->shader = litShader;
        scene.addTechnique(fwd);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt;

        // Add impact decal on SPACE
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_SPACE] && decalTimer_ <= 0.f)
        {
            float x = std::sin(time_ * 0.7f) * 8.f;
            float z = std::cos(time_ * 0.5f) * 8.f;
            decals_->addDecal({x, 0.01f, z}, {0,1,0}, {0.8f,0.8f}, {1,0.2f,0.2f,1.f}, 8.f);
            decalTimer_ = 0.15f;
        }
        if (decalTimer_ > 0.f) decalTimer_ -= dt;
    }

    void render() override { DemoBase::render(); }

    void release() override { DemoBase::release(); }

private:
    GrassNode  *grass_    = nullptr;
    DecalNode  *decals_   = nullptr;
    float       time_     = 0.f;
    float       decalTimer_ = 0.f;
};
