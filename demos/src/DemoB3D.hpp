#pragma once

#include "Animator.hpp"
#include "B3dLoader.hpp"
#include "DemoBase.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class DemoB3D : public DemoBase
{
public:
    const char *name() override { return "B3D"; }

    bool init() override
    {
        if (!DemoBase::init())
            return false;

        camera->setPosition({0.f, 2.5f, 8.f});
        camera->lookAt({0.f, 1.5f, 0.f});
        if (auto *ctrl = static_cast<FreeCameraController *>(camera->getController()))
            ctrl->moveSpeed = 8.0f;

        shader_ = shaders().load("skinned",
                                 "assets/shaders/skinned.vert",
                                 "assets/shaders/skinned.frag");
        if (!shader_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoB3D] failed to load skinned shader");
            return false;
        }

        RenderState::instance().useProgram(shader_->getId());
        shader_->setVec3("u_lightDir", glm::normalize(glm::vec3(1.f, 2.f, 1.f)));
        shader_->setVec3("u_lightColor", glm::vec3(1.f, 0.95f, 0.85f));
        shader_->setFloat("u_shadowBias", 0.005f);
        shader_->setInt("u_shadowMap", 1);

        materials().setDefaults(shader_, textures().getWhite());

        mesh_ = new AnimatedMesh();
        mesh_->name = "ninja_b3d";

        B3dLoader loader;
        std::vector<Animation *> importedAnims;
        if (!loader.load("assets/b3d/ninja.b3d",
                         "assets/b3d",
                         mesh_,
                         &importedAnims))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoB3D] failed loading ninja.b3d");
            return false;
        }

        node_ = scene.createAnimatedMeshNode("ninja", mesh_);
        if (!node_ || !node_->animator)
            return false;

        const glm::vec3 size = mesh_->aabb.size();
        const float maxDim = std::max(size.x, std::max(size.y, size.z));
        const float scale = (maxDim > 1e-5f) ? (2.6f / maxDim) : 1.0f;
        node_->setScale(glm::vec3(scale));
        node_->setPosition({0.f, -mesh_->aabb.min.y * scale, 0.f});

        AnimationLayer *layer = node_->animator->addLayer();
        for (Animation *a : importedAnims)
        {
            if (!a) continue;
            if (a->name.empty())
                a->name = "anim_" + std::to_string(animNames_.size());
            animNames_.push_back(a->name);
            layer->addAnimation(a->name, a); // layer owns Animation*
        }

        if (!animNames_.empty())
        {
            currentAnim_ = 0;
            layer->play(animNames_[currentAnim_], PlayMode::Loop, 0.1f);
            SDL_Log("[DemoB3D] Playing: %s", animNames_[currentAnim_].c_str());
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[DemoB3D] no animations found; rendering bind pose");
        }

        tech_ = new ForwardTechnique();
        tech_->opaque()->shader = shader_;
        scene.addTechnique(tech_);

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
        node_ = nullptr;
        mesh_ = nullptr;
        tech_ = nullptr;
        shader_ = nullptr;
        animNames_.clear();
        currentAnim_ = 0;
        DemoBase::release();
    }

private:
    AnimatedMesh *mesh_ = nullptr;
    AnimatedMeshNode *node_ = nullptr;
    ForwardTechnique *tech_ = nullptr;
    Shader *shader_ = nullptr;

    std::vector<std::string> animNames_;
    size_t currentAnim_ = 0;
};

