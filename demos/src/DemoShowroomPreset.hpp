#pragma once

#include "DemoBase.hpp"
#include "RenderState.hpp"
#include "ShadowMap.hpp"

#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"

class ShowroomShadowPass : public ShadowPass
{
public:
    glm::vec3 center = {0.f, 1.f, 0.f};

    void execute(const FrameContext &ctx, RenderQueue &queue) const override
    {
        if (!shadowMap || !shader)
            return;

        auto &rs = RenderState::instance();

        glm::vec3 dir = glm::normalize(lightDir);
        glm::vec3 up  = (std::fabs(dir.y) > 0.99f) ? glm::vec3(1.f, 0.f, 0.f)
                                                   : glm::vec3(0.f, 1.f, 0.f);

        glm::vec3 target = center;
        glm::vec3 eye    = target - dir * lightDist;
        glm::mat4 lightV = glm::lookAt(eye, target, up);

        if (shadowMap->width > 0)
        {
            const float texelWorld = (orthoSize * 2.0f) / float(shadowMap->width);
            glm::vec4 centerLS4 = lightV * glm::vec4(target, 1.0f);
            glm::vec3 centerLS(centerLS4);
            centerLS.x = std::floor(centerLS.x / texelWorld + 0.5f) * texelWorld;
            centerLS.y = std::floor(centerLS.y / texelWorld + 0.5f) * texelWorld;
            glm::vec3 snappedCenter = glm::vec3(glm::inverse(lightV) * glm::vec4(centerLS, 1.0f));
            eye    = snappedCenter - dir * lightDist;
            target = snappedCenter;
            lightV = glm::lookAt(eye, target, up);
        }

        glm::mat4 lightP = glm::ortho(-orthoSize, orthoSize,
                                      -orthoSize, orthoSize,
                                      nearPlane, farPlane);

        lightSpaceMatrix = lightP * lightV;

        shadowMap->bind();
        rs.setDepthTest(true);
        rs.setDepthWrite(true);
        rs.setCull(true);
        rs.setBlend(false);

        rs.useProgram(shader->getId());
        shader->setMat4("u_lightSpace", lightSpaceMatrix);

        for (const auto &item : queue.getOpaque())
        {
            if (!item.drawable)
                continue;
            shader->setMat4("u_model", item.model);
            if (item.indexCount > 0)
                item.drawable->drawRange(item.indexStart, item.indexCount);
            else
                item.drawable->draw();
        }

        shadowMap->unbind();
    }
};

class ShowroomShadowTechnique : public Technique
{
public:
    ShowroomShadowTechnique()
    {
        name = "ShowroomForwardShadow";

        shadowMap_ = new ShadowMap();
        if (!shadowMap_->initialize(2048, 2048))
        {
            delete shadowMap_;
            shadowMap_ = nullptr;
            return;
        }

        shadowPass_ = addPass<ShowroomShadowPass>();
        shadowPass_->shadowMap = shadowMap_;
        opaquePass_ = addPass<OpaquePass>();
        transparentPass_ = addPass<TransparentPass>();
    }

    ~ShowroomShadowTechnique() override
    {
        delete shadowMap_;
    }

    void render(const FrameContext &ctx, RenderQueue &queue) const override
    {
        if (!shadowPass_ || !opaquePass_ || !transparentPass_ || !shadowMap_)
            return;

        shadowPass_->execute(ctx, queue);
        RenderState::instance().bindTexture(1, GL_TEXTURE_2D, shadowMap_->texture);

        if (litShader)
        {
            RenderState::instance().useProgram(litShader->getId());
            litShader->setMat4("u_lightSpace", shadowPass_->lightSpaceMatrix);
        }

        opaquePass_->execute(ctx, queue);
        transparentPass_->execute(ctx, queue);
    }

    ShowroomShadowPass *shadowPass() const { return shadowPass_; }

    Shader *litShader = nullptr;

private:
    ShadowMap *shadowMap_ = nullptr;
    ShowroomShadowPass *shadowPass_ = nullptr;
    OpaquePass *opaquePass_ = nullptr;
    TransparentPass *transparentPass_ = nullptr;
};

class DemoShowroomPreset : public DemoBase
{
public:
    const char *name() override { return "Showroom Preset"; }

    bool init() override
    {
        if (!DemoBase::init())
            return false;

        camera->setPosition({0.f, 2.4f, 8.5f});
        camera->lookAt({0.f, 1.1f, 0.f});
        camera->nearPlane = 0.05f;
        camera->farPlane  = 120.f;

        if (auto *ctrl = static_cast<FreeCameraController *>(camera->getController()))
            ctrl->moveSpeed = 12.f;

        Shader *depthShader = shaders().load("showroom_shadow_depth",
                                             "assets/shaders/depth.vert",
                                             "assets/shaders/depth.frag");
        Shader *litShader   = shaders().load("showroom_shadow_lit",
                                             "assets/shaders/showroom_shadow_lit.vert",
                                             "assets/shaders/showroom_shadow_lit.frag");
        if (!depthShader || !litShader)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoShowroomPreset] Failed to load showroom shadow shaders");
            return false;
        }

        RenderState::instance().useProgram(litShader->getId());
        litShader->setInt("u_shadowMap", 1);

        materials().setDefaults(litShader, textures().getWhite());

        createLights();
        if (!createCar())
            return false;
        createFloor();
        createAccentSpheres();
        materials().applyDefaults();
        applySceneMaterialTweaks(litShader);

        pipeline_ = new ShowroomShadowTechnique();
        if (!pipeline_)
            return false;

        pipeline_->litShader = litShader;
        pipeline_->shadowPass()->shader = depthShader;
        pipeline_->shadowPass()->lightDir = lightDir_;
        pipeline_->shadowPass()->lightDist = lightDistance_;
        pipeline_->shadowPass()->orthoSize = shadowExtent_;
        pipeline_->shadowPass()->nearPlane = shadowNear_;
        pipeline_->shadowPass()->farPlane = shadowFar_;
        pipeline_->shadowPass()->center = shadowCenter_;
        scene.addTechnique(pipeline_);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt;

        if (animateLight_)
        {
            lightYaw_ += dt * lightAnimSpeed_;
            if (lightYaw_ >= 360.f) lightYaw_ -= 360.f;
        }

        float yawR   = glm::radians(lightYaw_);
        float pitchR = glm::radians(lightPitch_);
        lightDir_ = glm::normalize(glm::vec3(
            std::cos(pitchR) * std::sin(yawR),
            std::sin(pitchR),
            std::cos(pitchR) * std::cos(yawR)));

        if (sun_)
            sun_->lookDirection(lightDir_);

        if (pipeline_ && pipeline_->shadowPass())
        {
            pipeline_->shadowPass()->lightDir = lightDir_;
            pipeline_->shadowPass()->lightDist = lightDistance_;
            pipeline_->shadowPass()->orthoSize = shadowExtent_;
            pipeline_->shadowPass()->nearPlane = shadowNear_;
            pipeline_->shadowPass()->farPlane = shadowFar_;
            pipeline_->shadowPass()->center = shadowCenter_;
        }

        syncMaterialParams();

        if (carNode_)
        {
            shadowCenter_ = carNode_->worldPosition() + glm::vec3(0.f, shadowFocusHeight_, 0.f);
            if (Input::IsKeyDown(KEY_LEFT))
                carNode_->yaw(70.f * dt);
            if (Input::IsKeyDown(KEY_RIGHT))
                carNode_->yaw(-70.f * dt);
            if (autoRotate_)
                carNode_->yaw(autoRotateSpeed_ * dt);
        }

        for (OrbitSphere &orbit : spheres_)
        {
            if (!orbit.node) continue;
            orbit.angle += orbit.speed * dt;
            orbit.node->setPosition({
                orbit.radius * std::cos(orbit.angle),
                orbit.height,
                orbit.radius * std::sin(orbit.angle)
            });
        }
    }

    void render() override
    {
        DemoBase::render();
        onImGui();
    }

    void release() override
    {
        pipeline_ = nullptr;
        carNode_  = nullptr;
        floorNode_ = nullptr;
        carMesh_ = nullptr;
        floorMesh_ = nullptr;
        spheres_.clear();
        DemoBase::release();
    }

private:
    struct ModelCandidate
    {
        const char *path;
        const char *textureDir;
    };

    struct OrbitSphere
    {
        MeshNode *node = nullptr;
        float radius = 2.f;
        float height = 1.f;
        float speed  = 1.f;
        float angle  = 0.f;
    };

    bool fileExists(const std::string &path) const
    {
        SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) return false;
        SDL_RWclose(rw);
        return true;
    }

    void createLights()
    {
        sun_ = scene.createLight<DirectionalLight>("sun");
        sun_->intensity = 1.35f;
        sun_->ambient   = glm::vec3(0.14f, 0.14f, 0.16f);
        sun_->lookDirection(lightDir_);

        PointLight *fill = scene.createLight<PointLight>("fill");
        fill->setPosition({3.5f, 2.8f, 2.0f});
        fill->color     = {0.45f, 0.55f, 0.80f};
        fill->intensity = 3.0f;
        fill->range     = 14.0f;

        PointLight *rim = scene.createLight<PointLight>("rim");
        rim->setPosition({-3.0f, 1.4f, -2.5f});
        rim->color      = {1.0f, 0.55f, 0.25f};
        rim->intensity  = 2.2f;
        rim->range      = 10.0f;
    }

    bool createCar()
    {
        const ModelCandidate candidates[] = {
            {"assets/3ds/bmw/model.3ds", "assets/3ds/bmw/textures"},
            {"assets/3ds/aston/aston.3ds", "assets/3ds/aston/textures"},
            {"assets/3ds/aston/aston.3ds", "assets/3ds/aston"},
        };

        for (const ModelCandidate &candidate : candidates)
        {
            if (!fileExists(candidate.path))
                continue;

            carMesh_ = meshes().load("showroom_preset_car", candidate.path, candidate.textureDir);
            if (carMesh_)
                break;
        }

        if (!carMesh_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoShowroomPreset] No supported car model found");
            return false;
        }

        carNode_ = scene.createMeshNode("showroom_car", carMesh_);
        if (!carNode_)
            return false;

        const glm::vec3 size = carMesh_->aabb.size();
        const float maxDim = std::max(size.x, std::max(size.y, size.z));
        const float scale  = (maxDim > 1e-6f) ? (4.0f / maxDim) : 1.0f;

        carNode_->setScale(glm::vec3(scale));
        carNode_->setPosition({0.f, -carMesh_->aabb.min.y * scale + 0.02f, 0.f});
        return true;
    }

    void createFloor()
    {
        floorMesh_ = meshes().create_plane("showroom_preset_floor", 45.f, 45.f, 70);
        floorNode_ = scene.createMeshNode("showroom_floor", floorMesh_);
        if (!floorNode_)
            return;

        Material *floorMat = materials().create("showroom_preset_floor_mat");
        Texture *floorTex = textures().getWhite();
        if (fileExists("assets/powerplant/textures/tank_top.png"))
        {
            if (Texture *loaded = textures().load("showroom_preset_floor_tex",
                                                  "assets/powerplant/textures/tank_top.png"))
            {
                floorTex = loaded;
            }
        }

        floorMat->setTexture("u_albedo", floorTex);
        floorMat->setFloat("u_opacity", 1.0f);
        floorMat->setDepthTest(true);
        floorMat->setDepthWrite(true);
        floorMat->setCullFace(true);
        floorNode_->setMaterial(floorMat->name);
    }

    void createAccentSpheres()
    {
        Mesh *sphereMesh = meshes().create_sphere("showroom_preset_sphere", 0.28f, 24);
        if (!sphereMesh)
            return;

        static const glm::vec3 colors[] = {
            {1.0f, 0.25f, 0.05f},
            {0.1f, 0.45f, 1.0f},
            {0.2f, 0.9f, 0.2f},
            {0.9f, 0.85f, 0.1f}
        };

        for (int i = 0; i < 4; ++i)
        {
            Material *mat = materials().create("showroom_preset_sphere_mat_" + std::to_string(i));
            mat->setTexture("u_albedo", textures().getWhite());
            mat->setVec3("u_albedoTint", colors[i]);
            mat->setFloat("u_specularStrength", 0.20f);
            mat->setInt("u_receiveShadow", 0);
            mat->setFloat("u_opacity", 1.0f);

            MeshNode *node = scene.createMeshNode("showroom_sphere_" + std::to_string(i), sphereMesh);
            if (!node) continue;
            node->setMaterial(mat->name);

            OrbitSphere orbit;
            orbit.node   = node;
            orbit.radius = 1.8f + float(i) * 0.55f;
            orbit.height = 0.35f + float(i % 2) * 1.1f;
            orbit.speed  = 0.6f + float(i) * 0.22f;
            orbit.angle  = float(i) * 1.57f;
            spheres_.push_back(orbit);
        }
    }

    void applySceneMaterialTweaks(Shader *litShader)
    {
        if (!litShader)
            return;

        auto applyToMaterial = [this, litShader](Material *mat,
                                           bool preserveTint,
                                           const glm::vec3 &tint,
                                           float receiveShadow,
                                           bool doubleSided,
                                           float specStrength)
        {
            if (!mat) return;
            mat->setShader(litShader);
            if (!preserveTint)
                mat->setVec3("u_albedoTint", tint);
            mat->setFloat("u_specularStrength", specStrength);
            mat->setInt("u_receiveShadow", receiveShadow > 0.5f ? 1 : 0);
            const float defaultOpacity = mat->blend ? 0.35f : 1.0f;
            mat->setFloat("u_opacity", mat->getFloat("u_opacity", defaultOpacity));
            mat->setFloat("u_shadowBias", shadowBias_);
            mat->setFloat("u_shadowFilterScale", shadowFilterScale_);
            mat->setCullFace(!doubleSided);
            if (mat->blend)
                mat->setDepthWrite(false);
        };

        if (carMesh_)
        {
            for (Material *mat : carMesh_->materials)
                applyToMaterial(mat, true, {1.f, 1.f, 1.f}, 1.f, true, 0.30f);
        }

        if (floorNode_)
            applyToMaterial(floorNode_->getMaterial(), false, {0.24f, 0.25f, 0.28f}, 1.f, false, 0.10f);

        for (OrbitSphere &orbit : spheres_)
            applyToMaterial(orbit.node ? orbit.node->getMaterial() : nullptr,
                            true, {1.f, 1.f, 1.f}, 0.f, false, 0.20f);
    }

    void onImGui()
    {
        ImGui::SetNextWindowPos(ImVec2(10, 100), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(340, 320), ImGuiCond_Once);

        ImGui::Begin("Showroom Preset");

        ImGui::SeparatorText("Car");
        ImGui::Checkbox("Auto rotate", &autoRotate_);
        ImGui::SliderFloat("Rotate speed", &autoRotateSpeed_, -120.0f, 120.0f, "%.1f deg/s");

        ImGui::SeparatorText("Sun");
        ImGui::Checkbox("Animate light", &animateLight_);
        ImGui::SliderFloat("Yaw", &lightYaw_, 0.f, 360.f, "%.1f deg");
        ImGui::SliderFloat("Pitch", &lightPitch_, -89.f, -5.f, "%.1f deg");
        ImGui::SliderFloat("Anim speed", &lightAnimSpeed_, 1.f, 40.f, "%.1f deg/s");

        ImGui::SeparatorText("Shadows");
        ImGui::SliderFloat("Light distance", &lightDistance_, 8.f, 40.f, "%.1f");
        ImGui::SliderFloat("Shadow extent", &shadowExtent_, 2.f, 12.f, "%.1f");
        ImGui::SliderFloat("Shadow near", &shadowNear_, 0.1f, 10.f, "%.2f");
        ImGui::SliderFloat("Shadow far", &shadowFar_, 10.f, 80.f, "%.1f");
        ImGui::SliderFloat("Focus height", &shadowFocusHeight_, 0.2f, 3.0f, "%.2f");
        ImGui::SliderFloat("Bias", &shadowBias_, 0.0002f, 0.02f, "%.4f");
        ImGui::SliderFloat("Softness", &shadowFilterScale_, 0.5f, 3.0f, "%.2f");
        ImGui::TextUnformatted("Shadow resolution: 2048");
        ImGui::TextUnformatted("Tip: aperta Shadow extent ate caber so o carro e o chao perto.");

        ImGui::End();
    }

    void syncMaterialParams()
    {
        for (Material *mat : carMesh_ ? carMesh_->materials : std::vector<Material *>{})
        {
            if (!mat) continue;
            mat->setFloat("u_shadowBias", shadowBias_);
            mat->setFloat("u_shadowFilterScale", shadowFilterScale_);
        }

        if (Material *mat = floorNode_ ? floorNode_->getMaterial() : nullptr)
        {
            mat->setFloat("u_shadowBias", shadowBias_);
            mat->setFloat("u_shadowFilterScale", shadowFilterScale_);
        }

        for (OrbitSphere &orbit : spheres_)
        {
            Material *mat = orbit.node ? orbit.node->getMaterial() : nullptr;
            if (!mat) continue;
            mat->setFloat("u_shadowBias", shadowBias_);
            mat->setFloat("u_shadowFilterScale", shadowFilterScale_);
        }
    }

    ShowroomShadowTechnique *pipeline_ = nullptr;
    DirectionalLight *sun_ = nullptr;
    Mesh *carMesh_ = nullptr;
    Mesh *floorMesh_ = nullptr;
    MeshNode *carNode_ = nullptr;
    MeshNode *floorNode_ = nullptr;
    std::vector<OrbitSphere> spheres_;
    glm::vec3 lightDir_ = glm::normalize(glm::vec3(-0.6f, -1.0f, -0.2f));
    float time_ = 0.f;
    bool autoRotate_ = true;
    float autoRotateSpeed_ = 18.f;
    bool animateLight_ = false;
    float lightYaw_ = 210.f;
    float lightPitch_ = -52.f;
    float lightAnimSpeed_ = 10.f;
    float lightDistance_ = 18.f;
    float shadowExtent_ = 5.5f;
    float shadowNear_ = 0.5f;
    float shadowFar_ = 30.f;
    float shadowFocusHeight_ = 1.2f;
    float shadowBias_ = 0.0035f;
    float shadowFilterScale_ = 1.35f;
    glm::vec3 shadowCenter_ = {0.f, 1.2f, 0.f};
};