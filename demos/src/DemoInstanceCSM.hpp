#pragma once
#include "DemoBase.hpp"
#include "CascadeShadowMap.hpp"
#include "RenderState.hpp"
#include "Node.hpp"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// ============================================================
//  InstancedNode — submits a single instanced RenderItem to
//  the main queue AND the shadow queue each frame.
//  The InstanceBuffer must already be attached to the mesh VAO.
// ============================================================
class InstancedNode : public Node3D
{
public:
    MeshBuffer    *buffer   = nullptr;
    InstanceBuffer *instBuf = nullptr;
    Material      *material = nullptr;

    InstancedNode() { type = NodeType::ManualMesh; }

    void gatherRenderItems(RenderQueue &q, const FrameContext &) override
    {
        if (!buffer || !instBuf || !material || instBuf->count() == 0) return;

        RenderItem item;
        item.drawable      = buffer;
        item.material      = material;
        item.model         = glm::mat4(1.f);   // ignored by instanced.vert
        item.instanceCount = instBuf->count();
        item.passMask      = RenderPassMask::Opaque;
        q.add(item);
    }
};

// ============================================================
//  DemoInstanceCSM — GPU instancing + Cascade Shadow Maps
// ============================================================
class DemoInstanceCSM : public DemoBase
{
public:
    const char *name() override { return "Instance CSM"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 120.f, 200.f});
        camera->lookAt({0.f, 0.f, 0.f});
        camera->farPlane  = 1200.f;
        camera->nearPlane = 0.5f;
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 60.f;

        // ── Shaders ──────────────────────────────────────────
        depthShader_ = shaders().load("csm_depth",
            "assets/shaders/csm_depth.vert",
            "assets/shaders/csm_depth.frag");
        depthInstShader_ = shaders().load("csm_depth_inst",
            "assets/shaders/csm_depth_instanced.vert",
            "assets/shaders/csm_depth.frag");
        litShader_ = shaders().load("csm_lit",
            "assets/shaders/csm_lit.vert",
            "assets/shaders/csm_lit.frag");
        instShader_ = shaders().load("instanced_csm",
            "assets/shaders/instanced.vert",
            "assets/shaders/csm_lit.frag");
        skyShader_ = shaders().load("sky",
            "assets/shaders/sky.vert",
            "assets/shaders/sky.frag");
        if (!depthShader_ || !depthInstShader_ || !litShader_ || !instShader_ || !skyShader_)
            return false;

        // ── Box mesh ──────────────────────────────────────────
        cubeMesh_ = MeshManager::instance().create_cube("inst_box", 4.f);
        if (!cubeMesh_) return false;

        // ── Material ─────────────────────────────────────────
        auto *white = textures().getWhite();
        auto *brickTex = textures().load("inst_brick", "assets/wall.jpg");
        mat_ = materials().create("inst_mat");
        mat_->setShader(instShader_)
             ->setTexture("u_albedo", brickTex ? brickTex : white)
             ->setBool("u_showCascades", false);

        // ── Instance buffer ───────────────────────────────────
        instBuf_.resize(gridX_ * gridZ_);
        instBuf_.upload();
        cubeMesh_->buffer.attachInstances(&instBuf_);
        updateInstances();

        // ── Scene node ────────────────────────────────────────
        instNode_ = new InstancedNode();
        instNode_->buffer   = &cubeMesh_->buffer;
        instNode_->instBuf  = &instBuf_;
        instNode_->material = mat_;
        scene.add(instNode_);

        // ── Ground plane ─────────────────────────────────────
        auto &matMgr = MaterialManager::instance();
        matMgr.setDefaults(litShader_, white);
        ground_ = MeshManager::instance().create_plane("inst_ground", 800.f, 800.f);
        if (ground_)
        {
            ground_->materials[0]->setShader(litShader_)->setTexture("u_albedo", white);
            auto *gn = scene.createMeshNode("ground", ground_);
            gn->setPosition({0.f, -1.f, 0.f});
        }

        // ── Sun light ─────────────────────────────────────────
        sun_ = scene.createLight<DirectionalLight>("sun");
        sun_->color   = glm::vec3(lightColor_);
        sun_->ambient = glm::vec3(ambient_);
        sun_->lookAt(lightDir_);

        // ── CSM technique ─────────────────────────────────────
        auto *tech = new CsmTechnique();
        if (!tech->initialize(2048)) { delete tech; return false; }
        csm_ = tech;
        tech->litShader = litShader_;
        tech->getCsm()->setLightDirection(lightDir_);
        tech->getCsm()->setShadowFarPlane(shadowFar_);
        tech->getCsm()->setLambda(lambda_);

        for (int i = 0; i < CSM_NUM_CASCADES; ++i)
        {
            auto *dp          = tech->addPass<CsmDepthPass>();
            dp->csm           = tech->getCsm();
            dp->cascade       = i;
            dp->shader        = depthShader_;
            dp->instancedShader = depthInstShader_;
        }
        tech->addPass<OpaquePass>();
        tech->addPass<SkyPass>()->shader = skyShader_;
        scene.addTechnique(tech);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt;

        if (animLight_)
        {
            lightYaw_ += dt * 15.f;
            if (lightYaw_ > 360.f) lightYaw_ -= 360.f;
        }

        float yawR   = glm::radians(lightYaw_);
        float pitchR = glm::radians(lightPitch_);
        lightDir_ = glm::normalize(glm::vec3(
            std::cos(pitchR) * std::sin(yawR),
            std::sin(pitchR),
            std::cos(pitchR) * std::cos(yawR)));
        csm_->getCsm()->setLightDirection(lightDir_);

        if (sun_)
        {
            sun_->color   = glm::vec3(lightColor_);
            sun_->ambient = glm::vec3(ambient_);
            sun_->lookAt(lightDir_);
        }

        if (animInstances_)
        {
            updateInstances();
            instBuf_.update();
        }
    }

    void render() override
    {
        const glm::vec4 lightDirV = glm::vec4(-lightDir_, 0.f);
        auto &rs = RenderState::instance();

        rs.useProgram(litShader_->getId());
        litShader_->setVec4("u_lightDir",   lightDirV);
        litShader_->setVec4("u_lightColor", lightColor_);
        litShader_->setVec4("u_ambient",    ambient_);

        rs.useProgram(instShader_->getId());
        instShader_->setVec4("u_lightDir",   lightDirV);
        instShader_->setVec4("u_lightColor", lightColor_);
        instShader_->setVec4("u_ambient",    ambient_);

        rs.useProgram(skyShader_->getId());
        skyShader_->setMat4("u_invViewProj", glm::inverse(camera->viewProjection));
        skyShader_->setVec4("u_cameraPos",   glm::vec4(camera->position, 1.f));
        skyShader_->setVec4("u_lightDir",    lightDirV);
        skyShader_->setVec4("u_lightColor",  lightColor_);

        // Bind CSM to instanced lit shader too
        auto *csmRaw = csm_->getCsm();
        rs.useProgram(instShader_->getId());
        csmRaw->bindToShader(instShader_, 1);

        DemoBase::render();
        onImGui();
    }

private:
    // ── Grid rebuild ──────────────────────────────────────────
    void updateInstances()
    {
        const int N    = gridX_ * gridZ_;
        const float dx = spacing_;
        const float dz = spacing_;
        const float ox = -(gridX_ - 1) * dx * 0.5f;
        const float oz = -(gridZ_ - 1) * dz * 0.5f;

        if (instBuf_.count() != N)
        {
            instBuf_.resize(N);
            cubeMesh_->buffer.attachInstances(&instBuf_);
        }

        int idx = 0;
        for (int iz = 0; iz < gridZ_; ++iz)
        for (int ix = 0; ix < gridX_; ++ix)
        {
            float x = ox + ix * dx;
            float z = oz + iz * dz;
            float y = 2.f + std::sin(time_ * 1.5f + ix * 0.7f + iz * 0.5f) * 6.f;

            glm::mat4 m = glm::translate(glm::mat4(1.f), {x, y, z});
            if (animInstances_)
            {
                float angle = time_ * 0.8f + (ix + iz) * 0.3f;
                m = glm::rotate(m, angle, glm::vec3(0.3f, 1.f, 0.2f));
            }
            m = glm::scale(m, glm::vec3(boxScale_));
            instBuf_.set(idx++, m);
        }
    }

    void onImGui()
    {
        auto *csm = csm_->getCsm();

        ImGui::SetNextWindowPos({10, 100}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({320, 400}, ImGuiCond_Once);
        ImGui::Begin("Instance CSM");

        ImGui::SeparatorText("Instances");
        bool gridChanged = false;
        gridChanged |= ImGui::SliderInt("Grid X", &gridX_, 1, 100);
        gridChanged |= ImGui::SliderInt("Grid Z", &gridZ_, 1, 100);
        gridChanged |= ImGui::SliderFloat("Spacing", &spacing_, 5.f, 40.f);
        gridChanged |= ImGui::SliderFloat("Box scale", &boxScale_, 0.5f, 4.f);
        if (gridChanged) { updateInstances(); instBuf_.upload(); }

        ImGui::Checkbox("Animate instances", &animInstances_);
        ImGui::Text("Count: %d", gridX_ * gridZ_);

        ImGui::SeparatorText("Sun Direction");
        ImGui::Checkbox("Animate light", &animLight_);
        ImGui::BeginDisabled(animLight_);
        ImGui::SliderFloat("Yaw",   &lightYaw_,   0.f,   360.f, "%.1f deg");
        ImGui::EndDisabled();
        ImGui::SliderFloat("Pitch", &lightPitch_, -89.f, -5.f,  "%.1f deg");

        ImGui::SeparatorText("Light");
        ImGui::ColorEdit3("Sun",     &lightColor_.x);
        ImGui::ColorEdit3("Ambient", &ambient_.x);

        ImGui::SeparatorText("Cascade Shadow Maps");
        if (ImGui::SliderFloat("Shadow far", &shadowFar_, 50.f, 1200.f))
            csm->setShadowFarPlane(shadowFar_);
        if (ImGui::SliderFloat("Lambda",     &lambda_,    0.f,  1.f, "%.2f"))
            csm->setLambda(lambda_);

        ImGui::SeparatorText("Splits");
        for (int i = 0; i < CSM_NUM_CASCADES; ++i)
            ImGui::Text("  [%d]  %.1f", i, csm->cascadeSplits[i]);

        ImGui::End();
    }

    // ── Fields ───────────────────────────────────────────────
    CsmTechnique    *csm_            = nullptr;
    DirectionalLight *sun_           = nullptr;
    Shader          *depthShader_    = nullptr;
    Shader          *depthInstShader_= nullptr;
    Shader          *litShader_      = nullptr;
    Shader          *instShader_     = nullptr;
    Shader          *skyShader_      = nullptr;
    Mesh            *cubeMesh_       = nullptr;
    Mesh            *ground_         = nullptr;
    Material        *mat_            = nullptr;
    InstancedNode   *instNode_       = nullptr;
    InstanceBuffer   instBuf_;
    float            time_           = 0.f;

    int   gridX_         = 20;
    int   gridZ_         = 20;
    float spacing_       = 14.f;
    float boxScale_      = 1.f;
    bool  animInstances_ = true;
    bool  animLight_     = false;
    float lightYaw_      = 0.f;
    float lightPitch_    = -45.f;
    float shadowFar_     = 600.f;
    float lambda_        = 0.55f;
    glm::vec4 lightColor_ = {1.f, 0.95f, 0.8f, 1.f};
    glm::vec4 ambient_    = {0.12f, 0.13f, 0.16f, 1.f};
    glm::vec3 lightDir_   = glm::normalize(glm::vec3(-1.f, -1.5f, -0.5f));
};
