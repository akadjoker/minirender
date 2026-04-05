#pragma once

#include "Demo.hpp"
#include "Animator.hpp"
#include "Input.hpp"
#include "MeshLoader.hpp"
#include "imgui.h"

class DemoAnimation final : public Demo
{
public:
    const char *name() const override { return "animation"; }

    void applySunAngles()
    {
        if (!sun_)
            return;
        sun_->setEulerAngles({sunElevation_, sunAzimuth_, 0.f});
    }

    void build(Scene &scene, Device &device) override
    {
        createMainCamera(scene, device, {0.f, 3.f, 8.f}, {0.f, 2.f, 0.f});
        configureFreeCamera(8.f, 0.16f, 2.5f);
        camera()->farPlane = 200.f;
        camera()->clearColorVal = {0.10f, 0.12f, 0.16f, 1.0f};

        sun_ = scene.createLight<DirectionalLight>("sun");
        sun_->color = {1.0f, 0.96f, 0.90f};
        sun_->intensity = 1.25f;
        sun_->ambient = {0.10f, 0.11f, 0.14f};
        sun_->castShadow = true;
        applySunAngles();

        auto &meshes = MeshManager::instance();
        auto &materials = MaterialManager::instance();
        auto &textures = TextureManager::instance();

        Mesh *groundMesh = meshes.create_plane("anim_ground_mesh", 20.f, 20.f, 1);
        materials.createTextured("anim_ground_mat",
                                 textures.getPattern(),
                                 {0.72f, 0.72f, 0.74f, 1.0f});
        MeshNode *ground = scene.createMeshNode("ground", groundMesh);
        ground->setMaterial("anim_ground_mat");

        mesh_ = AnimatedMeshManager::instance().load("sinbad_anim_mesh",
                                                     "assets/models/sinbad/sinbad.h3d",
                                                     "assets/models/sinbad");
        if (!mesh_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoAnimation] Failed to load sinbad.h3d");
            return;
        }

        textures.setFlipVertical(true);
        Texture *bodyTex = textures.load("sinbad_body_anim", "assets/models/sinbad/sinbad_body.tga");
        Texture *clothesTex = textures.load("sinbad_clothes_anim", "assets/models/sinbad/sinbad_clothes.tga");
        Texture *swordTex = textures.load("sinbad_sword_anim", "assets/models/sinbad/sinbad_sword.tga");
        textures.setFlipVertical(false);

        Material *matBody = materials.createSkinned("sinbad_body_anim",
                                                    bodyTex ? bodyTex : textures.getWhite());
        Material *matClothes = materials.createSkinned("sinbad_clothes_anim",
                                                       clothesTex ? clothesTex : textures.getWhite());
        Material *matSword = materials.createSkinned("sinbad_sword_anim",
                                                     swordTex ? swordTex : textures.getWhite());

        mesh_->materials.clear();
        mesh_->materials.push_back(matBody);
        mesh_->materials.push_back(matClothes);
        mesh_->materials.push_back(matSword);

        if (mesh_->surfaces.size() >= 7)
        {
            mesh_->surfaces[0].material_index = 0;
            mesh_->surfaces[1].material_index = 0;
            mesh_->surfaces[2].material_index = 1;
            mesh_->surfaces[3].material_index = 0;
            mesh_->surfaces[4].material_index = 2;
            mesh_->surfaces[5].material_index = 1;
            mesh_->surfaces[6].material_index = 1;
        }

        node_ = scene.createAnimatedMeshNode("sinbad", mesh_);
        node_->setPosition({0.f, 1.5f, 0.f});
        node_->setScale(0.5f);

        AnimationLayer *layer = node_->animator ? node_->animator->addLayer() : nullptr;
        if (!layer)
            return;

        layer->loadAnimation("IdleBase", "assets/models/sinbad/sinbad_IdleBase.anim");
        layer->loadAnimation("RunBase", "assets/models/sinbad/sinbad_RunBase.anim");
        layer->loadAnimation("Dance", "assets/models/sinbad/sinbad_Dance.anim");
        layer->play("IdleBase", PlayMode::Loop, 0.0f);
        currentAnim_ = "IdleBase";
    }

    void update(float dt) override
    {
        (void)dt;

        if (!node_ || !node_->animator)
            return;

        AnimationLayer *layer = node_->animator->getLayer(0);
        if (!layer)
            return;

        if (Input::IsKeyPressed(KEY_ONE))
        {
            layer->crossFade("IdleBase", 0.20f);
            currentAnim_ = "IdleBase";
        }
        if (Input::IsKeyPressed(KEY_TWO))
        {
            layer->crossFade("RunBase", 0.18f);
            currentAnim_ = "RunBase";
        }
        if (Input::IsKeyPressed(KEY_THREE))
        {
            layer->crossFade("Dance", 0.28f);
            currentAnim_ = "Dance";
        }
    }

    void drawImGui(Renderer &renderer) override
    {
        if (!sun_)
            return;

        ImGui::Begin("Animation Demo");
        ImGui::Text("Animacoes");
        ImGui::Text("1 Idle  2 Run  3 Dance");
        ImGui::Text("Atual: %s", currentAnim_.c_str());
        ImGui::Separator();

        bool sunChanged = false;
        sunChanged |= ImGui::SliderFloat("Sun Elevation", &sunElevation_, -89.0f, 89.0f);
        sunChanged |= ImGui::SliderFloat("Sun Azimuth", &sunAzimuth_, -180.0f, 180.0f);
        if (sunChanged)
            applySunAngles();

        ImGui::Checkbox("Cast Shadow", &sun_->castShadow);
        ImGui::ColorEdit3("Sun Color", &sun_->color.x);
        ImGui::SliderFloat("Sun Intensity", &sun_->intensity, 0.0f, 4.0f);
        ImGui::ColorEdit3("Ambient", &sun_->ambient.x);
        float shadowBias = renderer.shadowBias();
        if (ImGui::SliderFloat("Shadow Bias", &shadowBias, 0.0001f, 0.02f, "%.4f", ImGuiSliderFlags_Logarithmic))
            renderer.setShadowBias(shadowBias);

        if (node_)
        {
            ImGui::Separator();
            ImGui::Text("Sinbad");
            ImGui::Checkbox("Mesh Cast Shadow", &node_->castShadow);
            ImGui::Checkbox("Mesh Receive Shadow", &node_->receiveShadow);
        }

        FreeCameraController *controller = freeCamera();
        if (controller)
        {
            ImGui::Separator();
            ImGui::Text("Camera");
            ImGui::SliderFloat("Move Speed", &controller->moveSpeed, 1.0f, 80.0f);
            ImGui::SliderFloat("Mouse Sens.", &controller->mouseSensitivity, 0.02f, 0.5f);
            ImGui::SliderFloat("Sprint", &controller->sprintMultiplier, 1.0f, 8.0f);
        }

        ImGui::End();
    }

private:
    AnimatedMesh *mesh_ = nullptr;
    AnimatedMeshNode *node_ = nullptr;
    DirectionalLight *sun_ = nullptr;
    float sunElevation_ = 45.f;
    float sunAzimuth_ = -30.f;
    std::string currentAnim_;
};
