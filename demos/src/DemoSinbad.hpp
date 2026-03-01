#pragma once
#include "Core.hpp"
#include "Input.hpp"
#include "DemoBase.hpp"
#include "MeshLoader.hpp"
#include "Animator.hpp"
#include "RenderState.hpp"

// ============================================================
//  DemoSinbad — skinned mesh + animation layers
// ============================================================
class DemoSinbad : public DemoBase
{
public:
    const char *name() override { return "Sinbad"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 3.f, 8.f});
        camera->lookAt({0.f, 2.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 5.f;

        // ── Shaders ──────────────────────────────────────────
        skinnedShader = shaders().load("skinned",
                                       "assets/shaders/skinned.vert",
                                       "assets/shaders/skinned.frag");
        if (!skinnedShader)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoSinbad] skinned shader failed");
            return false;
        }

        RenderState::instance().useProgram(skinnedShader->getId());
        skinnedShader->setVec3 ("u_lightDir",   glm::normalize(glm::vec3(1.f, 2.f, 1.f)));
        skinnedShader->setVec3 ("u_lightColor", {1.f, 0.95f, 0.85f});
        skinnedShader->setFloat("u_shadowBias", 0.005f);
        skinnedShader->setInt  ("u_shadowMap",  1);

        materials().setDefaults(skinnedShader, textures().getWhite());

        // ── Carregar sinbad.h3d ───────────────────────────────
        sinbadMesh = new AnimatedMesh();
        sinbadMesh->name = "sinbad";
        {
            MeshReader r;
            r.textureDir = "assets/models/sinbad";
            if (!r.load("assets/models/sinbad/sinbad.h3d", sinbadMesh))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoSinbad] Failed to load sinbad.h3d");
                return false;
            }
        }
        SDL_Log("[DemoSinbad] sinbad  verts=%zu  bones=%zu  surfs=%zu  mats=%zu",
                sinbadMesh->buffer.vertices.size(),
                sinbadMesh->bones.size(),
                sinbadMesh->surfaces.size(),
                sinbadMesh->materials.size());

        // ── Texturas — o .h3d não as embebe, atribuímos manualmente ──
        // Todas as 7 surfaces usam matIndex=0 (DefaultMaterial)
        // Criamos materiais separados e reatribuímos as surfaces
        {
            auto &texMgr = textures();
            auto &matMgr = materials();

            Texture *bodyTex    = texMgr.load("sinbad_body",    "assets/models/sinbad/sinbad_body.tga");
            Texture *clothesTex = texMgr.load("sinbad_clothes", "assets/models/sinbad/sinbad_clothes.tga");
            Texture *swordTex   = texMgr.load("sinbad_sword",   "assets/models/sinbad/sinbad_sword.tga");

            Material *matBody    = matMgr.create("sinbad_body");
            Material *matClothes = matMgr.create("sinbad_clothes");
            Material *matSword   = matMgr.create("sinbad_sword");
            matBody   ->setTexture("u_albedo", bodyTex    ? bodyTex    : texMgr.getWhite());
            matClothes->setTexture("u_albedo", clothesTex ? clothesTex : texMgr.getWhite());
            matSword  ->setTexture("u_albedo", swordTex   ? swordTex   : texMgr.getWhite());
            matBody   ->setVec3("u_color", {1.f, 1.f, 1.f});
            matClothes->setVec3("u_color", {1.f, 1.f, 1.f});
            matSword  ->setVec3("u_color", {1.f, 1.f, 1.f});

            sinbadMesh->materials.clear();
            sinbadMesh->materials.push_back(matBody);    // index 0
            sinbadMesh->materials.push_back(matClothes); // index 1
            sinbadMesh->materials.push_back(matSword);   // index 2

            // Surfaces (verts): 0=288, 1=1888, 2=583, 3=302, 4=688, 5=252, 6=1533
            // sinbad.h3d = character only, no sword geometry (sword.h3d is separate)
            // surf4 is NOT sword — it's another clothes/body part
            // Original SetBufferMaterial was 1-based: body=1, clothes=2, sword=3
            // buf0→1=body, buf1→1=body, buf2→2=clothes, buf3→1=body,
            // buf4→3=sword(wrong-no sword in char), buf5→2=clothes, buf6→2=clothes
            // Fix: surf4 → clothes (1) since sinbad.h3d has no sword mesh
            
        
            sinbadMesh->surfaces[0].material_index = 0;
            sinbadMesh->surfaces[1].material_index = 0;
            sinbadMesh->surfaces[2].material_index = 1;
            sinbadMesh->surfaces[3].material_index = 0;
            sinbadMesh->surfaces[4].material_index = 2;
            sinbadMesh->surfaces[5].material_index = 1;
            sinbadMesh->surfaces[6].material_index = 1;

        }

        materials().applyDefaults();

        // ── Nó na cena ───────────────────────────────────────
        sinbadNode = scene.createAnimatedMeshNode("sinbad", sinbadMesh);
        sinbadNode->setPosition({0.f, 0.f, 0.f});

        // ── Layer 0: corpo inteiro (locomotion) ──────────────
        AnimationLayer *lowerLayer = sinbadNode->animator->addLayer();

        const char *locoAnims[] = {
            "IdleBase", "RunBase", "Dance",
            "JumpStart", "JumpLoop", "JumpEnd",
            "HandsClosed", "HandsRelaxed",
        };
        for (const char *a : locoAnims)
        {
            std::string path = std::string("assets/models/sinbad/sinbad_") + a + ".anim";
            if (lowerLayer->loadAnimation(a, path))
                SDL_Log("[DemoSinbad] Layer0: %s", a);
            else
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[DemoSinbad] Missing: %s", a);
        }
        lowerLayer->play("IdleBase", PlayMode::Loop);
        currentAnim = "IdleBase";

        // ── Layer 1: upper body (ataques) ─────────────────────
        // Bone mask: Stomach [47] + todos os descendentes
        // → tronco, braços, cabeça — locomotion das pernas continua no layer 0
        sinbadNode->animator->addLayer();
        sinbadNode->animator->setLayerMaskFromBone(1, "Stomach");
        AnimationLayer *upperLayer = sinbadNode->animator->getLayer(1);

        const char *attackAnims[] = {
            "IdleTop",
            "DrawSwords", "SliceVertical", "SliceHorizontal",
        };
        for (const char *a : attackAnims)
        {
            std::string path = std::string("assets/models/sinbad/sinbad_") + a + ".anim";
            if (upperLayer->loadAnimation(a, path))
                SDL_Log("[DemoSinbad] Layer1: %s", a);
            else
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[DemoSinbad] Missing: %s", a);
        }
        // Upper layer: IdleTop em loop — ataques fazem triggerAction com blend
        upperLayer->defaultBlendTime = 0.2f;
        upperLayer->play("IdleTop", PlayMode::Loop);

        // ── Forward pass simples (sem shadow) ─────────────────
        tech_ = new ForwardTechnique();
        tech_->opaque()->shader = skinnedShader;
        scene.addTechnique(tech_);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);

      

        auto *lowerLayer = sinbadNode->animator->getLayer(0); // corpo inteiro
        auto *upperLayer = sinbadNode->animator->getLayer(1); // upper body
        if (!lowerLayer) return;

        // ── Locomotion (layer 0 — full body) ─────────────────
        if (Input::IsKeyDown(KEY_ONE)) { lowerLayer->crossFade("IdleBase", 0.3f); currentAnim = "IdleBase"; lowerLayer->setSpeed(1.0f); }
        if (Input::IsKeyDown(KEY_TWO)) { lowerLayer->crossFade("RunBase",  0.2f); currentAnim = "RunBase";  lowerLayer->setSpeed(1.0f); }
        if (Input::IsKeyDown(KEY_THREE)) { lowerLayer->crossFade("Dance",    0.3f); currentAnim = "Dance";    lowerLayer->setSpeed(1.0f); }

        // ── Ataques (layer 1 — upper body apenas) ────────────
        // As pernas continuam a correr/idle enquanto os braços atacam!
        if (upperLayer)
        {
            // triggerAction = blend in do ataque + volta ao IdleTop com blend out
            if (Input::IsKeyDown(KEY_FOUR)) { upperLayer->setSpeed(0.2f);  upperLayer->triggerAction("DrawSwords"); }
            if (Input::IsKeyDown(KEY_FIVE)) { upperLayer->setSpeed(0.5f);  upperLayer->triggerAction("SliceVertical"); }
            if (Input::IsKeyDown(KEY_SIX)) { upperLayer->setSpeed(0.8f);  upperLayer->triggerAction("SliceHorizontal"); }
        }

        // ── Salto — full body (layer 0) ───────────────────────
        if (Input::IsKeyDown(KEY_SPACE))
        {
            lowerLayer->setSpeed(1.0f);
            lowerLayer->playOneShot("JumpStart", "JumpLoop");
        }
        if (Input::IsKeyDown(KEY_R) && lowerLayer->isPlaying("JumpLoop"))
            lowerLayer->playOneShot("JumpEnd", currentAnim);

        // ── T — cicla mapeamento de materiais para debug ──────
        if (Input::IsKeyDown(KEY_T))
        {
            texVariant = (texVariant + 1) % 4;
            // verts: surf0=288 surf1=1888 surf2=583 surf3=302 surf4=688 surf5=252 surf6=1533
            // 0=body  1=clothes  2=sword
            static const int variants[4][7] = {
                { 0, 0, 1, 0, 1, 1, 1 }, // A: surf4=clothes (likely correct)
                { 0, 0, 1, 0, 2, 1, 1 }, // B: surf4=sword (original mapping)
                { 0, 0, 1, 1, 1, 1, 1 }, // C: surf3+4=clothes
                { 0, 0, 1, 1, 2, 1, 1 }, // D: surf3=clothes surf4=sword
            };
            const char *varNames[] = {"A: surf4=clothes","B: surf4=sword","C: surf3+4=clothes","D: surf3=clothes,surf4=sword"};
            SDL_Log("[Sinbad] Tex variant %s", varNames[texVariant]);
            for (int si = 0; si < (int)sinbadMesh->surfaces.size() && si < 7; si++)
                sinbadMesh->surfaces[si].material_index = variants[texVariant][si];
        }

        // Reset speed do layer 0 quando salto terminar
        const bool jumpPlaying = lowerLayer->isPlaying("JumpStart") ||
                                 lowerLayer->isPlaying("JumpEnd");
        if (!jumpPlaying && lowerLayer->getSpeed() != 1.0f)
            lowerLayer->setSpeed(1.0f);
    }

    void render() override { DemoBase::render(); }

    void release() override
    {
        delete tech_;     tech_     = nullptr;
        delete sinbadMesh; sinbadMesh = nullptr;
        DemoBase::release();
    }

private:
    AnimatedMesh     *sinbadMesh    = nullptr;
    AnimatedMeshNode *sinbadNode    = nullptr;
    Shader           *skinnedShader = nullptr;
    ForwardTechnique *tech_         = nullptr;
    std::string       currentAnim;
    int               texVariant  = 0;
};
