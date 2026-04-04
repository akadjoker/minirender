#pragma once

#include "DemoBase.hpp"
#include "Input.hpp"
#include "Manager.hpp"
#include "Md2Loader.hpp"
#include "RenderPipeline.hpp"
#include "imgui.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <string>
#include <vector>

class DemoMD2 : public DemoBase
{
public:
    const char *name() override { return "MD2 Vertex Anim"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 18.f, 46.f});
        camera->lookAt({0.f, 16.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 30.f;

        shader_ = shaders().load("unlit",
                                 "assets/shaders/unlit.vert",
                                 "assets/shaders/unlit.frag");
        if (!shader_)
            return false;

        materials().setDefaults(shader_, textures().getPattern());

        tech_ = new ForwardTechnique();
        scene.addTechnique(tech_);

        node_ = scene.createMd2Node("md2_knight");
        if (!node_ || !node_->loadMd2("assets/md2/pknight.md2",
                                      "file:assets/md2/pknight.jpg",
                                      shader_,
                                      true,
                                      "Stand"))
            return false;
        node_->setScale({1.4f, 1.4f, 1.4f});

        if (node_->mesh)
        {
            const glm::vec3 c = node_->mesh->aabb.center();
            node_->setPosition({-c.x, -node_->mesh->aabb.min.y, -c.z});

            const glm::vec3 sz = node_->mesh->aabb.size() * node_->scale;
            const float focusY = std::max(8.f, sz.y * 0.55f);
            const float rawDist = std::max(24.f, std::max(sz.x, sz.z) * 2.6f);
            const float dist = std::min(rawDist, 90.f);
            camera->setPosition({0.f, focusY + 6.f, dist});
            camera->lookAt({0.f, focusY, 0.f});
        }

        rebuildClipNames();
        playClip("Stand", 0.f);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);

        if (!node_)
            return;

        if (Input::IsKeyPressed(KEY_ONE)) playClip("Stand");
        if (Input::IsKeyPressed(KEY_TWO)) playClip("Run");
        if (Input::IsKeyPressed(KEY_THREE)) playClip("Attack");
        if (Input::IsKeyPressed(KEY_FOUR)) playClip("Jump");
        if (Input::IsKeyPressed(KEY_COMMA)) cycleClip(-1);
        if (Input::IsKeyPressed(KEY_PERIOD)) cycleClip(+1);
    }

    void render() override
    {
        DemoBase::render();
        onImGui();
    }

    void release() override
    {
        tech_ = nullptr;
        shader_ = nullptr;
        node_ = nullptr;
        DemoBase::release();
    }

private:
    Md2Node *node_ = nullptr;
    ForwardTechnique *tech_ = nullptr;
    Shader *shader_ = nullptr;

    std::vector<std::string> clipNames_;
    int selectedClip_ = 0;

    void playClip(const std::string &name, float blendTime = 0.14f)
    {
        if (!node_)
            return;

        if (!node_->playMd2(name, blendTime))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[DemoMD2] clip '%s' not found", name.c_str());
            return;
        }

        for (size_t i = 0; i < clipNames_.size(); ++i)
        {
            if (clipNames_[i] == name)
            {
                selectedClip_ = static_cast<int>(i);
                break;
            }
        }
    }

    void rebuildClipNames()
    {
        clipNames_.clear();
        selectedClip_ = 0;
        if (!node_ || !node_->animAsset())
            return;

        for (const VertexAnimClip &clip : node_->animAsset()->clips)
            clipNames_.push_back(clip.name);
    }

    void cycleClip(int dir)
    {
        if (!node_ || clipNames_.empty())
            return;

        const int n = static_cast<int>(clipNames_.size());
        selectedClip_ = (selectedClip_ + dir + n) % n;
        playClip(clipNames_[static_cast<size_t>(selectedClip_)]);
    }

    void onImGui()
    {
        if (!node_) return;

        ImGui::SetNextWindowPos({10, 100}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({360, 280}, ImGuiCond_Once);
        ImGui::Begin("MD2 Clips");

        const VertexAnimClip *cur = node_->controller.currentClip();
        ImGui::Text("Current: %s", cur ? cur->name.c_str() : "<none>");
        ImGui::Text("Clips: %zu", clipNames_.size());
        ImGui::Separator();

        if (!clipNames_.empty())
        {
            if (selectedClip_ < 0) selectedClip_ = 0;
            if (selectedClip_ >= static_cast<int>(clipNames_.size()))
                selectedClip_ = static_cast<int>(clipNames_.size()) - 1;

            const char *preview = clipNames_[static_cast<size_t>(selectedClip_)].c_str();
            if (ImGui::BeginCombo("Clip", preview))
            {
                for (int i = 0; i < static_cast<int>(clipNames_.size()); ++i)
                {
                    const bool isSelected = (i == selectedClip_);
                    if (ImGui::Selectable(clipNames_[static_cast<size_t>(i)].c_str(), isSelected))
                        selectedClip_ = i;
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Play Selected"))
                playClip(clipNames_[static_cast<size_t>(selectedClip_)]);
            ImGui::SameLine();
            if (ImGui::Button("Prev"))
                cycleClip(-1);
            ImGui::SameLine();
            if (ImGui::Button("Next"))
                cycleClip(+1);
        }

        ImGui::SeparatorText("Hotkeys");
        ImGui::BulletText("1 Stand | 2 Run | 3 Attack | 4 Jump");
        ImGui::BulletText(", Prev clip | . Next clip");

        ImGui::End();
    }
};
