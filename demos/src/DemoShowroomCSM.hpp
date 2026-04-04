#pragma once

#include "DemoBase.hpp"
#include "CascadeShadowMap.hpp"
#include "RenderState.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "meshoptimizer.h"
#include "imgui.h"

namespace
{
Mesh *buildShadowProxyMesh(const std::string &name, Mesh *src, float ratio)
{
    if (!src || src->buffer.indices.size() < 3 || src->buffer.vertices.empty())
        return nullptr;

    ratio = glm::clamp(ratio, 0.005f, 1.0f);

    std::vector<unsigned int> simplified(src->buffer.indices.size());
    size_t targetIndexCount = static_cast<size_t>(float(src->buffer.indices.size()) * ratio);
    targetIndexCount = std::max<size_t>(targetIndexCount - (targetIndexCount % 3), 24);
    targetIndexCount = std::min(targetIndexCount, src->buffer.indices.size());

    float resultError = 0.0f;
    size_t simplifiedCount = meshopt_simplifySloppy(
        simplified.data(),
        src->buffer.indices.data(),
        src->buffer.indices.size(),
        &src->buffer.vertices[0].position.x,
        src->buffer.vertices.size(),
        sizeof(Vertex),
        nullptr,
        targetIndexCount,
        0.10f,
        &resultError);

    if (simplifiedCount < 3)
        return nullptr;

    simplified.resize(simplifiedCount - (simplifiedCount % 3));
    if (simplified.size() < 3)
        return nullptr;

    auto &meshMgr = MeshManager::instance();
    if (meshMgr.has(name))
        meshMgr.unload(name);

    Mesh *proxy = meshMgr.create(name);
    if (!proxy)
        return nullptr;

    proxy->buffer.indices.assign(simplified.begin(), simplified.end());
    proxy->buffer.vertices.resize(src->buffer.vertices.size());
    size_t vertexCount = meshopt_optimizeVertexFetch(
        proxy->buffer.vertices.data(),
        proxy->buffer.indices.data(),
        proxy->buffer.indices.size(),
        src->buffer.vertices.data(),
        src->buffer.vertices.size(),
        sizeof(Vertex));
    proxy->buffer.vertices.resize(vertexCount);

    proxy->surfaces.clear();
    proxy->materials.clear();
    proxy->add_surface(0, static_cast<uint32_t>(proxy->buffer.indices.size()), 0, "shadow_proxy");
    proxy->upload();
    return proxy;
}
}

class DemoShowroomCSM : public DemoBase
{
public:
    const char *name() override { return "Showroom CSM"; }

    enum PreviewMode
    {
        PreviewOriginal = 0,
        PreviewProxy = 1,
        PreviewOverlay = 2,
    };

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

        Shader *depthShader = shaders().load("showroom_csm_depth",
                                             "assets/shaders/csm_depth.vert",
                                             "assets/shaders/csm_depth.frag");
        litShader_ = shaders().load("showroom_csm_lit",
                                     "assets/shaders/showroom_csm_lit.vert",
                                     "assets/shaders/showroom_csm_lit.frag");
        if (!depthShader || !litShader_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoShowroomCSM] Failed to load showroom CSM shaders");
            return false;
        }

        materials().setDefaults(litShader_, textures().getWhite());

        createLights();
        if (!createCar())
            return false;
        createShadowProxy();
        createFloor();
        createAccentSpheres();
        materials().applyDefaults();
        applySceneMaterialTweaks(litShader_);

        pipeline_ = new CsmTechnique();
        if (!pipeline_ || !pipeline_->initialize(1024))
        {
            delete pipeline_;
            pipeline_ = nullptr;
            return false;
        }

        pipeline_->litShader = litShader_;
        pipeline_->getCsm()->setLightDirection(lightDir_);
        pipeline_->getCsm()->setShadowFarPlane(shadowFar_);
        pipeline_->getCsm()->setLambda(lambda_);

        for (int i = 0; i < CSM_NUM_CASCADES; ++i)
        {
            auto *dp  = pipeline_->addPass<CsmDepthPass>();
            dp->csm     = pipeline_->getCsm();
            dp->cascade = i;
            dp->shader  = depthShader;
        }

        pipeline_->addPass<OpaquePass>();
        pipeline_->addPass<TransparentPass>();
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
            if (lightYaw_ >= 360.f)
                lightYaw_ -= 360.f;
        }

        float yawR   = glm::radians(lightYaw_);
        float pitchR = glm::radians(lightPitch_);
        lightDir_ = glm::normalize(glm::vec3(
            std::cos(pitchR) * std::sin(yawR),
            std::sin(pitchR),
            std::cos(pitchR) * std::cos(yawR)));

        if (sun_)
            sun_->lookDirection(lightDir_);

        if (pipeline_ && pipeline_->getCsm())
        {
            pipeline_->getCsm()->setLightDirection(lightDir_);
            pipeline_->getCsm()->setShadowFarPlane(shadowFar_);
            pipeline_->getCsm()->setLambda(lambda_);
        }

        syncMaterialParams();

        if (carNode_)
        {
            if (Input::IsKeyDown(KEY_LEFT))
                carNode_->yaw(70.f * dt);
            if (Input::IsKeyDown(KEY_RIGHT))
                carNode_->yaw(-70.f * dt);
            if (autoRotate_)
                carNode_->yaw(autoRotateSpeed_ * dt);
        }

        syncShadowProxy();

        for (OrbitSphere &orbit : spheres_)
        {
            if (!orbit.node)
                continue;
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
        litShader_ = nullptr;
        sun_ = nullptr;
        carMesh_ = nullptr;
        floorMesh_ = nullptr;
        carNode_ = nullptr;
        floorNode_ = nullptr;
        shadowProxyMesh_ = nullptr;
        shadowProxyNode_ = nullptr;
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
        sun_ = scene.createLight<DirectionalLight>("showroom_csm_sun");
        sun_->intensity = 1.35f;
        sun_->ambient   = glm::vec3(0.14f, 0.14f, 0.16f);
        sun_->lookDirection(lightDir_);

        PointLight *fill = scene.createLight<PointLight>("showroom_csm_fill");
        fill->setPosition({3.5f, 2.8f, 2.0f});
        fill->color     = {0.45f, 0.55f, 0.80f};
        fill->intensity = 3.0f;
        fill->range     = 14.0f;

        PointLight *rim = scene.createLight<PointLight>("showroom_csm_rim");
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

            carMesh_ = meshes().load("showroom_csm_car", candidate.path, candidate.textureDir);
            if (carMesh_)
                break;
        }

        if (!carMesh_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoShowroomCSM] No supported car model found");
            return false;
        }

        carNode_ = scene.createMeshNode("showroom_csm_car_node", carMesh_);
        if (!carNode_)
            return false;

        const glm::vec3 size = carMesh_->aabb.size();
        const float maxDim = std::max(size.x, std::max(size.y, size.z));
        const float scale = (maxDim > 1e-6f) ? (4.0f / maxDim) : 1.0f;

        carNode_->setScale(glm::vec3(scale));
        carNode_->setPosition({0.f, -carMesh_->aabb.min.y * scale + 0.02f, 0.f});
        carNode_->castShadow = enableCarShadow_;
        return true;
    }

    void createShadowProxy()
    {
        if (!carMesh_ || !carNode_)
            return;

        shadowProxyMesh_ = buildShadowProxyMesh("showroom_csm_car_shadow_proxy", carMesh_, shadowProxyRatio_);
        if (!shadowProxyMesh_)
            return;

        shadowProxyNode_ = scene.createMeshNode("showroom_csm_car_shadow_proxy_node", shadowProxyMesh_);
        if (!shadowProxyNode_)
            return;

        Material *proxyMat = materials().create("showroom_csm_shadow_proxy_mat");
        proxyMat->setShader(litShader_);
        proxyMat->setTexture("u_albedo", textures().getWhite());
        proxyMat->setVec3("u_albedoTint", {1.0f, 0.15f, 0.15f});
        proxyMat->setFloat("u_specularStrength", 0.0f);
        proxyMat->setInt("u_receiveShadow", 0);
        proxyMat->setBool("u_showCascades", false);
        proxyMat->setFloat("u_opacity", 0.28f);
        proxyMat->setDepthTest(true);
        proxyMat->setDepthWrite(false);
        proxyMat->setCullFace(false);
        proxyMat->setBlend(true);

        shadowProxyNode_->setMaterial(proxyMat->name);
        shadowProxyNode_->passMask = showShadowProxy_ ? RenderPassMask::Transparent : 0;
        shadowProxyNode_->receiveShadow = false;
        shadowProxyNode_->castShadow = enableCarShadow_ && useShadowProxy_;
        syncShadowProxy();
        if (enableCarShadow_ && useShadowProxy_)
            carNode_->castShadow = false;
    }

    void rebuildShadowProxy()
    {
        if (!carMesh_)
            return;

        shadowProxyMesh_ = buildShadowProxyMesh("showroom_csm_car_shadow_proxy", carMesh_, shadowProxyRatio_);
        if (!shadowProxyMesh_)
            return;

        if (shadowProxyNode_)
            shadowProxyNode_->mesh = shadowProxyMesh_;

        syncShadowProxy();
    }

    void syncShadowProxy()
    {
        if (!shadowProxyNode_ || !carNode_)
            return;

        Material *proxyMat = shadowProxyNode_->getMaterial();
        shadowProxyNode_->setPosition(carNode_->position);
        shadowProxyNode_->setRotation(carNode_->rotation);
        shadowProxyNode_->setScale(carNode_->scale);

        carNode_->visible = (previewMode_ != PreviewProxy);
        shadowProxyNode_->visible = true;

        if (proxyMat)
        {
            const bool overlayMode = (previewMode_ == PreviewOverlay) || showShadowProxy_;
            proxyMat->setFloat("u_opacity", overlayMode ? 0.28f : 1.0f);
            proxyMat->setBlend(overlayMode);
            proxyMat->setDepthWrite(!overlayMode);
            proxyMat->setCullFace(!overlayMode);
        }

        if (previewMode_ == PreviewProxy)
            shadowProxyNode_->passMask = RenderPassMask::Opaque;
        else if ((previewMode_ == PreviewOverlay) || showShadowProxy_)
            shadowProxyNode_->passMask = RenderPassMask::Transparent;
        else
            shadowProxyNode_->passMask = 0;

        shadowProxyNode_->castShadow = enableCarShadow_ && useShadowProxy_;
        carNode_->castShadow = enableCarShadow_ && !useShadowProxy_;
    }

    void createFloor()
    {
        floorMesh_ = meshes().create_plane("showroom_csm_floor", 45.f, 45.f, 70);
        floorNode_ = scene.createMeshNode("showroom_csm_floor_node", floorMesh_);
        if (!floorNode_)
            return;

        Material *floorMat = materials().create("showroom_csm_floor_mat");
        Texture *floorTex = textures().getWhite();
        if (fileExists("assets/powerplant/textures/tank_top.png"))
        {
            if (Texture *loaded = textures().load("showroom_csm_floor_tex",
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
        Mesh *sphereMesh = meshes().create_sphere("showroom_csm_sphere", 0.28f, 24);
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
            Material *mat = materials().create("showroom_csm_sphere_mat_" + std::to_string(i));
            mat->setTexture("u_albedo", textures().getWhite());
            mat->setVec3("u_albedoTint", colors[i]);
            mat->setFloat("u_specularStrength", 0.20f);
            mat->setInt("u_receiveShadow", 0);
            mat->setFloat("u_opacity", 1.0f);
            mat->setBool("u_showCascades", showCascades_);

            MeshNode *node = scene.createMeshNode("showroom_csm_sphere_" + std::to_string(i), sphereMesh);
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
            mat->setBool("u_showCascades", showCascades_);
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
        ImGui::SetNextWindowSize(ImVec2(360, 360), ImGuiCond_Once);

        ImGui::Begin("Showroom CSM");

        ImGui::SeparatorText("Car");
        ImGui::Checkbox("Auto rotate", &autoRotate_);
        if (ImGui::Checkbox("Enable car shadow", &enableCarShadow_))
            syncShadowProxy();
        if (ImGui::Checkbox("Use shadow proxy", &useShadowProxy_))
            syncShadowProxy();
        static const char *previewItems[] = {"Original", "Proxy", "Overlay"};
        if (ImGui::Combo("Preview", &previewMode_, previewItems, 3))
            syncShadowProxy();
        if (ImGui::Checkbox("Show shadow proxy", &showShadowProxy_))
            syncShadowProxy();
        ImGui::SliderFloat("Proxy ratio", &shadowProxyRatio_, 0.005f, 0.20f, "%.3f", ImGuiSliderFlags_NoInput);
        if (ImGui::Button("Rebuild proxy"))
            rebuildShadowProxy();
        if (carMesh_)
            ImGui::Text("Car tris: %d", carMesh_->indexCount() / 3);
        if (shadowProxyMesh_)
            ImGui::Text("Proxy tris: %d", shadowProxyMesh_->indexCount() / 3);
        ImGui::SliderFloat("Rotate speed", &autoRotateSpeed_, -120.0f, 120.0f, "%.1f deg/s");

        ImGui::SeparatorText("Sun");
        ImGui::Checkbox("Animate light", &animateLight_);
        ImGui::SliderFloat("Yaw", &lightYaw_, 0.f, 360.f, "%.1f deg");
        ImGui::SliderFloat("Pitch", &lightPitch_, -89.f, -5.f, "%.1f deg");
        ImGui::SliderFloat("Anim speed", &lightAnimSpeed_, 1.f, 40.f, "%.1f deg/s");

        ImGui::SeparatorText("CSM");
        ImGui::SliderFloat("Shadow far", &shadowFar_, 20.f, 120.f, "%.1f");
        ImGui::SliderFloat("Lambda", &lambda_, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Bias", &shadowBias_, 0.0002f, 0.02f, "%.4f");
        ImGui::SliderFloat("Softness", &shadowFilterScale_, 0.25f, 2.0f, "%.2f");
        ImGui::Checkbox("Show cascades", &showCascades_);
        ImGui::TextUnformatted("Shadow resolution: 1024 x 4 cascades");
        if (pipeline_ && pipeline_->getCsm())
        {
            ImGui::Text("Split 0: %.2f", pipeline_->getCsm()->cascadeSplits[0]);
            ImGui::Text("Split 1: %.2f", pipeline_->getCsm()->cascadeSplits[1]);
            ImGui::Text("Split 2: %.2f", pipeline_->getCsm()->cascadeSplits[2]);
            ImGui::Text("Split 3: %.2f", pipeline_->getCsm()->cascadeSplits[3]);
        }

        ImGui::End();
    }

    void syncMaterialParams()
    {
        for (Material *mat : carMesh_ ? carMesh_->materials : std::vector<Material *>{})
        {
            if (!mat) continue;
            mat->setFloat("u_shadowBias", shadowBias_);
            mat->setFloat("u_shadowFilterScale", shadowFilterScale_);
            mat->setBool("u_showCascades", showCascades_);
        }

        if (Material *mat = floorNode_ ? floorNode_->getMaterial() : nullptr)
        {
            mat->setFloat("u_shadowBias", shadowBias_);
            mat->setFloat("u_shadowFilterScale", shadowFilterScale_);
            mat->setBool("u_showCascades", showCascades_);
        }

        for (OrbitSphere &orbit : spheres_)
        {
            Material *mat = orbit.node ? orbit.node->getMaterial() : nullptr;
            if (!mat) continue;
            mat->setFloat("u_shadowBias", shadowBias_);
            mat->setFloat("u_shadowFilterScale", shadowFilterScale_);
            mat->setBool("u_showCascades", showCascades_);
        }
    }

    CsmTechnique *pipeline_ = nullptr;
    Shader *litShader_ = nullptr;
    DirectionalLight *sun_ = nullptr;
    Mesh *carMesh_ = nullptr;
    Mesh *floorMesh_ = nullptr;
    Mesh *shadowProxyMesh_ = nullptr;
    MeshNode *carNode_ = nullptr;
    MeshNode *floorNode_ = nullptr;
    MeshNode *shadowProxyNode_ = nullptr;
    std::vector<OrbitSphere> spheres_;
    glm::vec3 lightDir_ = glm::normalize(glm::vec3(-0.6f, -1.0f, -0.2f));
    float time_ = 0.f;
    bool autoRotate_ = true;
    bool enableCarShadow_ = true;
    bool useShadowProxy_ = true;
    bool showShadowProxy_ = false;
    int previewMode_ = PreviewOriginal;
    float autoRotateSpeed_ = 18.f;
    bool animateLight_ = false;
    float lightYaw_ = 114.4f;
    float lightPitch_ = -64.5f;
    float lightAnimSpeed_ = 10.f;
    float shadowFar_ = 80.f;
    float lambda_ = 0.60f;
    float shadowBias_ = 0.0081f;
    float shadowFilterScale_ = 0.61f;
    float shadowProxyRatio_ = 0.001f;
    bool showCascades_ = false;
};
