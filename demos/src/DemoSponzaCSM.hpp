#pragma once
#include "DemoBase.hpp"
#include "CascadeShadowMap.hpp"
#include "RenderState.hpp"
#include "Batch.hpp"
#include "imgui.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
//  DemoSponzaCSM — Sponza mesh com Cascade Shadow Maps
//  [C] toggle cascade colour debug
//  [F] toggle cascade frustum wireframe
// ============================================================
class DemoSponzaCSM : public DemoBase
{
public:
    const char *name() override { return "Sponza CSM"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 80.f, 0.f});
        camera->lookAt({200.f, 30.f, 0.f});
        camera->farPlane  = 1200.f;
        camera->nearPlane = 0.5f;
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 40.f;

        // ── Shaders ─────────────────────────────────────────
        auto *depthShader = shaders().load("csm_depth",
            "assets/shaders/csm_depth.vert", "assets/shaders/csm_depth.frag");
        litShader_ = shaders().load("csm_lit",
            "assets/shaders/csm_lit.vert",   "assets/shaders/csm_lit.frag");
        skyShader_ = shaders().load("sky",
            "assets/shaders/sky.vert",        "assets/shaders/sky.frag");
        if (!depthShader || !litShader_ || !skyShader_) return false;

        // ── Sponza mesh ──────────────────────────────────────
        auto &meshMgr = MeshManager::instance();
        auto &matMgr  = MaterialManager::instance();

        // Set default shader + white fallback so every sponza sub-mesh
        // gets csm_lit with its own diffuse texture automatically.
        matMgr.setDefaults(litShader_, textures().getWhite());

        Mesh *sponza = meshMgr.load("sponza", "assets/obj/sponza.obj", "assets/textures");
        if (!sponza)
        {
            SDL_Log("[DemoSponzaCSM] Failed to load sponza.obj");
            return false;
        }

        // Override every sub-mesh material to use csm_lit and enable cascade toggle
        for (auto *mat : sponza->materials)
        {
            if (mat)
            {
                mat->setShader(litShader_);
                mat->setBool("u_showCascades", false);
            }
        }
        sponzaMat_ = nullptr; // materials managed per sub-mesh

        auto *sponzaNode = scene.createMeshNode("sponza", sponza);
        sponzaNode->passMask = RenderPassMask::Opaque;
        sponzaNode->setScale({0.03f, 0.03f, 0.03f});

        // ── CSM technique ────────────────────────────────────
        auto *tech = new CsmTechnique();
        if (!tech->initialize(2048)) { delete tech; return false; }
        csm_ = tech;

        tech->litShader = litShader_;
        tech->getCsm()->setLightDirection(lightDir_);
        tech->getCsm()->setShadowFarPlane(1200.f);
        tech->getCsm()->setLambda(0.7f);

        for (int i = 0; i < CSM_NUM_CASCADES; ++i)
        {
            auto *dp  = tech->addPass<CsmDepthPass>();
            dp->csm     = tech->getCsm();
            dp->cascade = i;
            dp->shader  = depthShader;
        }
        tech->addPass<OpaquePass>();
        tech->addPass<TransparentPass>();
        tech->addPass<SkyPass>()->shader = skyShader_;

        scene.addTechnique(tech);
        debugBatch_.Init();

        // ── Scene directional light ──────────────────────────
        // sendLights() in RenderPipeline reads this each frame;
        // without it the pipeline uses a hardcoded ambient (0.08) that
        // overrides whatever we set manually on litShader_.
        sun_ = scene.createLight<DirectionalLight>("sun");
        sun_->color     = glm::vec3(lightColor_);
        sun_->intensity = 1.f;
        sun_->ambient   = glm::vec3(ambient_);
        sun_->lookAt(lightDir_);   // forward()=lightDir_(down) → sendLights: u_lightDir=-forward()=toward-light ✓

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt;

        if (animLight_)
        {
            lightYaw_ += dt * 8.f; // degrees/s
            if (lightYaw_ > 360.f) lightYaw_ -= 360.f;
        }

        float yawR   = glm::radians(lightYaw_);
        float pitchR = glm::radians(lightPitch_);
        lightDir_ = glm::normalize(glm::vec3(
            std::cos(pitchR) * std::sin(yawR),
            std::sin(pitchR),
            std::cos(pitchR) * std::cos(yawR)));
        csm_->getCsm()->setLightDirection(lightDir_);

        // Keep scene light in sync so sendLights() uses our values
        if (sun_)
        {
            sun_->color   = glm::vec3(lightColor_);
            sun_->ambient = glm::vec3(ambient_);
            sun_->lookAt(lightDir_);
        }

        if (Input::IsKeyPressed(KEY_C))
        {
            showCascades_ = !showCascades_;
            auto *m = MeshManager::instance().get("sponza");
            if (m) for (auto *mat : m->materials)
                if (mat) mat->setBool("u_showCascades", showCascades_);
        }
        if (Input::IsKeyPressed(KEY_F))
            showFrustums_ = !showFrustums_;
    }

    void render() override
    {
        const glm::vec4 lightDirV = glm::vec4(-lightDir_, 0.f);
        auto &rs = RenderState::instance();

        rs.useProgram(litShader_->getId());
        litShader_->setVec4("u_lightDir",   lightDirV);
        litShader_->setVec4("u_lightColor", lightColor_);
        litShader_->setVec4("u_ambient",    ambient_);

        rs.useProgram(skyShader_->getId());
        skyShader_->setMat4("u_invViewProj", glm::inverse(camera->viewProjection));
        skyShader_->setVec4("u_cameraPos",   glm::vec4(camera->position, 1.f));
        skyShader_->setVec4("u_lightDir",    lightDirV);
        skyShader_->setVec4("u_lightColor",  lightColor_);

        DemoBase::render();

        if (showFrustums_)
            drawCascadeDebug();

        onImGui();
    }

    void release() override
    {
        debugBatch_.Release();
        DemoBase::release();
    }

private:
    CsmTechnique      *csm_          = nullptr;
    DirectionalLight  *sun_          = nullptr;
    Shader            *litShader_    = nullptr;
    Shader       *skyShader_    = nullptr;
    Material     *sponzaMat_    = nullptr;
    RenderBatch   debugBatch_;
    float         time_         = 0.f;
    bool          showCascades_ = false;
    bool          showFrustums_ = false;
    bool          animLight_    = false;
    float         lightYaw_     = 0.f;    // manual angle when anim off
    float         lightPitch_   = -45.f;  // degrees down
    float         shadowFar_    = 600.f;
    float         lambda_       = 0.7f;
    glm::vec4     lightColor_   = {1.f, 0.98f, 0.9f, 1.f};
    glm::vec4     ambient_      = {0.12f, 0.13f, 0.16f, 1.f};
    glm::vec3     lightDir_     = glm::normalize(glm::vec3(-1.f, -1.5f, -0.5f));

    void onImGui()
    {
        auto *csm = csm_->getCsm();

        ImGui::SetNextWindowPos({10, 100}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({320, 340}, ImGuiCond_Once);
        ImGui::Begin("Shadow Settings");

        // ─ Light direction ────────────────────────────────
        ImGui::SeparatorText("Sun Direction");
        ImGui::Checkbox("Animate", &animLight_);
        ImGui::BeginDisabled(animLight_);
        ImGui::SliderFloat("Yaw",   &lightYaw_,   0.f,   360.f, "%.1f deg");
        ImGui::EndDisabled();
        ImGui::SliderFloat("Pitch", &lightPitch_, -89.f, -5.f,  "%.1f deg");

        // ─ Light colour ────────────────────────────────
        ImGui::SeparatorText("Light");
        ImGui::ColorEdit3("Sun colour",     &lightColor_.x);
        ImGui::ColorEdit3("Ambient colour", &ambient_.x);

        // ─ CSM settings ─────────────────────────────
        ImGui::SeparatorText("Cascade Shadow Maps");
        if (ImGui::SliderFloat("Shadow far", &shadowFar_, 50.f, 1200.f))
            csm->setShadowFarPlane(shadowFar_);
        if (ImGui::SliderFloat("Lambda",     &lambda_,    0.f,  1.f, "%.2f"))
            csm->setLambda(lambda_);

        ImGui::Spacing();
        if (ImGui::Checkbox("Show cascade colours [C]", &showCascades_))
        {
            auto *m = MeshManager::instance().get("sponza");
            if (m) for (auto *mat : m->materials)
                if (mat) mat->setBool("u_showCascades", showCascades_);
        }
        ImGui::Checkbox("Show frustums [F]", &showFrustums_);

        // ─ Cascade splits info ─────────────────────────
        ImGui::SeparatorText("Split distances (view units)");
        for (int i = 0; i < CSM_NUM_CASCADES; ++i)
            ImGui::Text("  [%d]  %.1f", i, csm->cascadeSplits[i]);

        ImGui::End();
    }

    void drawCascadeDebug()
    {
        auto *csm = csm_->getCsm();
        static const u8 cols[CSM_NUM_CASCADES][3] = {
            {255, 80,  80},
            {80,  255, 80},
            {80,  80,  255},
            {255, 220, 40},
        };
        auto &rs = RenderState::instance();
        rs.setDepthTest(true);
        rs.setBlend(false);
        debugBatch_.SetMatrix(camera->viewProjection);

        float prevSplit = camera->nearPlane;
        for (int ci = 0; ci < CSM_NUM_CASCADES; ++ci)
        {
            float splitFar = csm->cascadeSplits[ci];
            glm::mat4 sliceProj = glm::perspective(
                glm::radians(camera->fov), camera->aspect(), prevSplit, splitFar);
            glm::mat4 inv = glm::inverse(sliceProj * camera->getView());

            glm::vec3 c[8]; int k = 0;
            for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
            {
                glm::vec4 p = inv * glm::vec4(x*2.f-1.f, y*2.f-1.f, z*2.f-1.f, 1.f);
                c[k++] = glm::vec3(p) / p.w;
            }
            debugBatch_.SetColor(cols[ci][0], cols[ci][1], cols[ci][2], 255);
            debugBatch_.Line3D(c[0],c[2]); debugBatch_.Line3D(c[2],c[6]);
            debugBatch_.Line3D(c[6],c[4]); debugBatch_.Line3D(c[4],c[0]);
            debugBatch_.Line3D(c[1],c[3]); debugBatch_.Line3D(c[3],c[7]);
            debugBatch_.Line3D(c[7],c[5]); debugBatch_.Line3D(c[5],c[1]);
            debugBatch_.Line3D(c[0],c[1]); debugBatch_.Line3D(c[2],c[3]);
            debugBatch_.Line3D(c[4],c[5]); debugBatch_.Line3D(c[6],c[7]);

            debugBatch_.SetColor(cols[ci][0]/2, cols[ci][1]/2, cols[ci][2]/2, 180);
            glm::mat4 invLS = glm::inverse(csm->lightSpaceMatrices[ci]);
            glm::vec3 lc[8]; k = 0;
            for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
            for (int zz = 0; zz < 2; ++zz)
            {
                glm::vec4 p = invLS * glm::vec4(x*2.f-1.f, y*2.f-1.f, zz*2.f-1.f, 1.f);
                lc[k++] = glm::vec3(p) / p.w;
            }
            debugBatch_.Line3D(lc[0],lc[2]); debugBatch_.Line3D(lc[2],lc[6]);
            debugBatch_.Line3D(lc[6],lc[4]); debugBatch_.Line3D(lc[4],lc[0]);
            debugBatch_.Line3D(lc[1],lc[3]); debugBatch_.Line3D(lc[3],lc[7]);
            debugBatch_.Line3D(lc[7],lc[5]); debugBatch_.Line3D(lc[5],lc[1]);
            debugBatch_.Line3D(lc[0],lc[1]); debugBatch_.Line3D(lc[2],lc[3]);
            debugBatch_.Line3D(lc[4],lc[5]); debugBatch_.Line3D(lc[6],lc[7]);
            prevSplit = splitFar;
        }
        debugBatch_.Render();
    }
};
