#pragma once

#include "Animator.hpp"
#include "DemoBase.hpp"
#include "Input.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"
#include "imgui.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class DemoGLTFAnimated : public DemoBase
{
public:
    const char *name() override { return "GLTF Animated"; }

    bool init() override
    {
        if (!DemoBase::init())
            return false;

        camera->setPosition({0.f, 2.2f, 6.5f});
        camera->lookAt({0.f, 1.5f, 0.f});
        if (auto *ctrl = static_cast<FreeCameraController *>(camera->getController()))
            ctrl->moveSpeed = 8.0f;

        shader_ = shaders().load("skinned",
                                 "assets/shaders/skinned.vert",
                                 "assets/shaders/skinned.frag");
        if (!shader_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoGLTFAnimated] failed to load skinned shader");
            return false;
        }

        RenderState::instance().useProgram(shader_->getId());
        shader_->setVec3("u_lightDir", glm::normalize(glm::vec3(1.f, 2.f, 1.f)));
        shader_->setVec3("u_lightColor", glm::vec3(1.f, 0.95f, 0.85f));
        shader_->setFloat("u_shadowBias", 0.005f);
        shader_->setInt("u_shadowMap", 1);
        materials().setDefaults(shader_, textures().getWhite());

        if (!loadCharacter())
            return false;

        tech_ = new ForwardTechnique();
        tech_->opaque()->shader = shader_;
        scene.addTechnique(tech_);
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        if (!node_ || !node_->animator || animNames_.empty())
            return;

        if (Input::IsKeyPressed(KEY_ONE))
            playRelative(-1);
        if (Input::IsKeyPressed(KEY_TWO))
            playRelative(+1);
    }

    void render() override
    {
        DemoBase::render();
        onImGui();
    }

    void release() override
    {
        mesh_ = nullptr;
        node_ = nullptr;
        tech_ = nullptr;
        shader_ = nullptr;
        animNames_.clear();
        loadedPath_.clear();
        currentAnim_ = 0;
        DemoBase::release();
    }

private:
    struct Candidate
    {
        const char *path;
        const char *textureDir;
    };

    bool loadCharacter()
    {
        std::vector<Animation *> importedAnims;

        const Candidate candidates[] = {
         //   {"assets/gltf/greenman.glb", "assets/gltf/"},
            {"assets/gltf/robot.glb", "assets/gltf/"}
        };

        for (int i = 0; i < 2; ++i)
        {
            importedAnims.clear();

            mesh_ = meshes().load_gltf_animated("gltf_actor", candidates[i].path, candidates[i].textureDir, &importedAnims);
            if (!mesh_)
                continue;

            if (importedAnims.empty())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[DemoGLTFAnimated] '%s' loaded but has no animations", candidates[i].path);
                mesh_ = nullptr;
                continue;
            }

            loadedPath_ = candidates[i].path;
            break;
        }

        if (!mesh_ || importedAnims.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoGLTFAnimated] failed to load an animated GLTF candidate");
            return false;
        }

        node_ = scene.createAnimatedMeshNode("gltf_actor", mesh_);
        if (!node_ || !node_->animator)
            return false;

        const glm::vec3 size = mesh_->aabb.size();
        const float maxDim = std::max(size.x, std::max(size.y, size.z));
        const float scale = (maxDim > 1e-5f) ? (2.6f / maxDim) : 1.0f;
        node_->setScale(glm::vec3(scale));
        node_->setPosition({0.f, -mesh_->aabb.min.y * scale, 0.f});

        AnimationLayer *layer = node_->animator->addLayer();
        for (size_t i = 0; i < importedAnims.size(); ++i)
        {
            Animation *a = importedAnims[i];
            if (!a)
                continue;

            if (a->name.empty())
                a->name = "anim_" + std::to_string(i);

            animNames_.push_back(a->name);
            layer->addAnimation(a->name, a); // layer owns Animation*
        }

        if (animNames_.empty())
            return false;

        currentAnim_ = 0;
        layer->play(animNames_[currentAnim_], PlayMode::Loop, 0.12f);
        SDL_Log("[DemoGLTFAnimated] loaded: %s", loadedPath_.c_str());
        SDL_Log("[DemoGLTFAnimated] playing: %s", animNames_[currentAnim_].c_str());
        return true;
    }

    void playIndex(size_t idx)
    {
        if (!node_ || !node_->animator || idx >= animNames_.size())
            return;

        AnimationLayer *layer = node_->animator->getLayer(0);
        if (!layer)
            return;

        currentAnim_ = idx;
        layer->play(animNames_[currentAnim_], PlayMode::Loop, 0.12f);
        SDL_Log("[DemoGLTFAnimated] playing: %s", animNames_[currentAnim_].c_str());
    }

    void playRelative(int delta)
    {
        if (animNames_.empty())
            return;

        const int n = static_cast<int>(animNames_.size());
        int next = static_cast<int>(currentAnim_) + delta;
        while (next < 0) next += n;
        while (next >= n) next -= n;
        playIndex(static_cast<size_t>(next));
    }

    void onImGui()
    {
        ImGui::SetNextWindowPos({10, 100}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({420, 190}, ImGuiCond_Once);
        ImGui::Begin("GLTF Animated");

        ImGui::Text("Model: %s", loadedPath_.empty() ? "-" : loadedPath_.c_str());
        ImGui::Text("Animations: %d", static_cast<int>(animNames_.size()));

        if (!animNames_.empty())
        {
            if (currentAnim_ >= animNames_.size())
                currentAnim_ = 0;

            const char *preview = animNames_[currentAnim_].c_str();
            if (ImGui::BeginCombo("Clip", preview))
            {
                for (size_t i = 0; i < animNames_.size(); ++i)
                {
                    const bool selected = (i == currentAnim_);
                    if (ImGui::Selectable(animNames_[i].c_str(), selected))
                        playIndex(i);
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SeparatorText("Hotkeys");
        ImGui::BulletText("1 previous clip");
        ImGui::BulletText("2 next clip");
        ImGui::End();
    }

    AnimatedMesh *mesh_ = nullptr;
    AnimatedMeshNode *node_ = nullptr;
    ForwardTechnique *tech_ = nullptr;
    Shader *shader_ = nullptr;

    std::vector<std::string> animNames_;
    std::string loadedPath_;
    size_t currentAnim_ = 0;
};

