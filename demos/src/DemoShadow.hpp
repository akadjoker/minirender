#pragma once
#include "DemoBase.hpp"
#include "ShadowMap.hpp"
#include "RenderState.hpp"

// ============================================================
//  DemoShadow — shadow map com luz direccional
// ============================================================
class DemoShadow : public DemoBase
{
public:
    const char *name() override { return "Shadow Map"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 15.f, 30.f});
        camera->lookAt({0.f, 0.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 20.f;

        // ── Shaders ──────────────────────────────────────────
        depthShader = shaders().load("depth",
                                     "assets/shaders/depth.vert",
                                     "assets/shaders/depth.frag");
        litShader = shaders().load("lit_shadow",
                                   "assets/shaders/lit_shadow.vert",
                                   "assets/shaders/lit_shadow.frag");
        if (!depthShader || !litShader)
        {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                            "[DemoShadow] Failed to load shaders");
            return false;
        }

        RenderState::instance().useProgram(litShader->getId());
        litShader->setVec3("u_lightDir", glm::normalize(glm::vec3(1.f, 2.f, 1.f)));
        litShader->setVec3("u_lightColor", {1.f, 1.f, 1.f});
        litShader->setFloat("u_shadowBias", 0.005f);
        litShader->setInt("u_shadowMap", 1); // texture unit 1

        materials().setDefaults(litShader, textures().getWhite());


        // Carregar texturas
        Texture *texGround = textures().load("tex_ground", "assets/wall.jpg");
        Texture *texCube   = textures().load("tex_cube",   "assets/noise.jpg");

        
        // ── Meshes ───────────────────────────────────────────
        Mesh *plane = meshes().create_plane("ground", 40.f, 40.f, 1);
        Mesh *cube = meshes().create_cube("cube", 2.f);
        
        // Assign default texture (u_albedo white) to all materials created above
        materials().applyDefaults();
        // Aplicar ao material do plano (nome = "ground")
        materials().get("ground")->setTexture("u_albedo", texGround);
        // Aplicar ao material do cubo (nome = "cube") — partilhado pelos 5 cubos
        materials().get("cube")->setTexture("u_albedo", texCube);
        
        scene.createMeshNode("ground", plane);

        for (int i = 0; i < 5; i++)
        {
            auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
            node->setPosition({(float)(i - 2) * 5.f, 1.f, 0.f});
        }

        // ── Technique ────────────────────────────────────────
        tech = new ShadowTechnique();
        tech->litShader = litShader;
        tech->shadowPass()->shader = depthShader;
        tech->shadowPass()->lightDir = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));
        tech->shadowPass()->orthoSize = 50.f;
        tech->shadowPass()->lightDist = 100.f;

        // u_lightSpace é passado automaticamente pelo ShadowTechnique::render()
        // mas o litShader precisa de saber — passamos via material uniform
        // (ou podes expor via shader uniform directamente antes de cada frame)

        scene.addTechnique(tech);
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
    }

    void render() override
    {
        DemoBase::render();
    }

    void release() override
    {
        delete tech;
        tech = nullptr;
        DemoBase::release();
    }

private:
    ShadowTechnique *tech = nullptr;
    Shader *depthShader = nullptr;
    Shader *litShader = nullptr;
};
