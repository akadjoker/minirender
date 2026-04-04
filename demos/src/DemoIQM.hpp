#pragma once

#include "Animator.hpp"
#include "DemoBase.hpp"
#include "Input.hpp"
#include "IqmLoader.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class DemoIQM : public DemoBase
{
public:
    const char *name() override { return "IQM"; }

    bool init() override
    {
        if (!DemoBase::init())
            return false;

        camera->setPosition({0.f, 2.5f, 6.f});
        camera->lookAt({0.f, 1.5f, 0.f});
        if (auto *ctrl = static_cast<FreeCameraController *>(camera->getController()))
            ctrl->moveSpeed = 8.0f;

        shader_ = shaders().load("skinned",
                                 "assets/shaders/skinned.vert",
                                 "assets/shaders/skinned.frag");
        if (!shader_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoIQM] failed to load skinned shader");
            return false;
        }

        RenderState::instance().useProgram(shader_->getId());
        shader_->setVec3("u_lightDir", glm::normalize(glm::vec3(1.f, 2.f, 1.f)));
        shader_->setVec3("u_lightColor", glm::vec3(1.f, 0.95f, 0.85f));
        shader_->setFloat("u_shadowBias", 0.005f);
        shader_->setInt("u_shadowMap", 1);

        materials().setDefaults(shader_, textures().getWhite());

        mesh_ = new AnimatedMesh();
        mesh_->name = "erebus_iqm";

        IqmLoader loader;
        std::vector<Animation *> importedAnims;
        if (!loader.load("assets/iqm/erebus/erebus_lod2.iqm",
                         "assets/iqm/erebus",
                         mesh_,
                         &importedAnims))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoIQM] failed loading erebus_lod2.iqm");
            return false;
        }

        if (importedAnims.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoIQM] IQM loaded but no animations were created");
            return false;
        }

        node_ = scene.createAnimatedMeshNode("erebus", mesh_);
        if (!node_ || !node_->animator)
            return false;

        const glm::vec3 size = mesh_->aabb.size();
        const float maxDim = std::max(size.x, std::max(size.y, size.z));
        const float scale = (maxDim > 1e-5f) ? (2.4f / maxDim) : 1.0f;
        node_->setScale(glm::vec3(scale));
        node_->setPosition({0.f, -mesh_->aabb.min.y * scale, 0.f});

        AnimationLayer *layer = node_->animator->addLayer();
        for (Animation *a : importedAnims)
        {
            if (!a)
                continue;
            if (a->name.empty())
                a->name = "clip_" + std::to_string(animNames_.size());
            animNames_.push_back(a->name);
            layer->addAnimation(a->name, a); // layer takes ownership
        }

        if (animNames_.empty())
            return false;

        currentAnim_ = 0;
        layer->play(animNames_[currentAnim_], PlayMode::Loop, 0.1f);
        SDL_Log("[DemoIQM] Playing: %s", animNames_[currentAnim_].c_str());

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
    }

    void release() override
    {
        node_ = nullptr;
        mesh_ = nullptr;
        tech_ = nullptr;
        shader_ = nullptr;
        animNames_.clear();
        currentAnim_ = 0;
        DemoBase::release();
    }

private:
    void playRelative(int delta)
    {
        AnimationLayer *layer = node_->animator->getLayer(0);
        if (!layer || animNames_.empty())
            return;

        const int n = static_cast<int>(animNames_.size());
        int next = static_cast<int>(currentAnim_) + delta;
        while (next < 0) next += n;
        while (next >= n) next -= n;
        currentAnim_ = static_cast<size_t>(next);
        layer->play(animNames_[currentAnim_], PlayMode::Loop, 0.12f);
        SDL_Log("[DemoIQM] Playing: %s", animNames_[currentAnim_].c_str());
    }

    AnimatedMesh *mesh_ = nullptr;
    AnimatedMeshNode *node_ = nullptr;
    ForwardTechnique *tech_ = nullptr;
    Shader *shader_ = nullptr;

    std::vector<std::string> animNames_;
    size_t currentAnim_ = 0;
};
