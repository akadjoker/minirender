#pragma once

#include "Batch.hpp"
#include "DemoBase.hpp"
#include "Q3Bsp.hpp"
#include "imgui.h"

#include <algorithm>
#include <string>
#include <vector>

class DemoQ3Bsp : public DemoBase
{
public:
    const char *name() override { return "Q3 BSP"; }
    float lightmapStrength() const { return bsp_.lightmapStrength(); }
    float lightmapGamma() const { return bsp_.lightmapGamma(); }
    bool debugEntitiesEnabled() const { return debugEntityBoxes_; }

    bool init() override
    {
        if (!DemoBase::init())
            return false;

        camera->setPosition({0.0f, 4.5f, 0.0f});
        camera->lookAt({0.0f, 4.0f, 1.0f});
        if (auto *ctrl = static_cast<FreeCameraController *>(camera->getController()))
            ctrl->moveSpeed = 18.0f;

        Shader *shader = shaders().load("q3bsp",
                                        "assets/shaders/q3bsp.vert",
                                        "assets/shaders/q3bsp.frag");
        if (!shader)
        {
            SDL_Log("[DemoQ3Bsp] failed to load q3bsp shader");
            return false;
        }

        forwardTechnique_ = new ForwardTechnique();
        scene.addTechnique(forwardTechnique_);

        bspNode_ = new Q3BspNode(&bsp_);
        bspNode_->name = "q3bsp_root";
        bspNode_->useBspTraversal = true;
        scene.add(bspNode_);

        struct BspCandidate
        {
            const char *bsp;
            const char *dir;
        };
        const BspCandidate candidates[4] = {
            {"assets/maps/oa_rpg3dm/maps/oa_rpg3dm2.bsp", "assets/maps/oa_rpg3dm/"},
            {"assets/maps/level.bsp", "assets/maps/"},
            {"assets/maps/gothic.bsp", "assets/maps/"},
            {"assets/maps/small.bsp", "assets/maps/"}};

        for (int i = 0; i < 4; ++i)
        {
            const BspCandidate &c = candidates[i];
            if (bsp_.load(c.bsp, c.dir, shader, 0.03f, 1.0f))
            {
                loadedMap_ = c.bsp;
                break;
            }
        }

        if (!bsp_.ready())
        {
            SDL_Log("[DemoQ3Bsp] failed to load any BSP map candidate");
            return false;
        }

        spawnEntityLights();
        SDL_Log("[DemoQ3Bsp] entity kinds: lights=%zu doors=%zu plats=%zu trains=%zu",
                bsp_.entitiesByKind(Q3BspMap::EntityKind::Light).size(),
                bsp_.entitiesByKind(Q3BspMap::EntityKind::FuncDoor).size(),
                bsp_.entitiesByKind(Q3BspMap::EntityKind::FuncPlat).size(),
                bsp_.entitiesByKind(Q3BspMap::EntityKind::FuncTrain).size());

        SDL_Log("[DemoQ3Bsp] loaded map: %s", loadedMap_.c_str());
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
    }

    void render() override
    {
        DemoBase::render();
        onImGui();
    }

    void debugDraw(RenderBatch *batch) override
    {
        if (!batch || !bspNode_ || !debugEntityBoxes_)
            return;

        const auto &ents = bsp_.entities();
        int drawn = 0;
        const int maxDraw = 2048;
        for (const auto &e : ents)
        {
            glm::vec3 originLocal;
            if (!bsp_.entityOriginLocal(e, originLocal))
                continue;

            const glm::vec3 p = bspNode_->localToWorldPoint(originLocal);
            switch (e.kind)
            {
            case Q3BspMap::EntityKind::Light:          batch->SetColor(255, 220, 80, 255); break;
            case Q3BspMap::EntityKind::FuncDoor:       batch->SetColor(255, 120, 120, 255); break;
            case Q3BspMap::EntityKind::FuncPlat:       batch->SetColor(120, 220, 255, 255); break;
            case Q3BspMap::EntityKind::FuncTrain:      batch->SetColor(180, 255, 120, 255); break;
            case Q3BspMap::EntityKind::TriggerMultiple:
            case Q3BspMap::EntityKind::TriggerPush:
            case Q3BspMap::EntityKind::TriggerTeleport: batch->SetColor(200, 120, 255, 255); break;
            default:                                    batch->SetColor(180, 180, 180, 255); break;
            }

            batch->Cube(p, entityBoxSize_, entityBoxSize_, entityBoxSize_, true);
            if (++drawn >= maxDraw)
                break;
        }
    }

    void release() override
    {
        // Scene owns techniques and deletes them in Scene::release().
        // Avoid double-free when leaving this demo.
        forwardTechnique_ = nullptr;

        DemoBase::release();
        bspNode_ = nullptr;
        entityLights_.clear();
        bsp_.clear();
    }

private:
    void onImGui()
    {
        ImGui::SetNextWindowPos({10, 100}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({360, 260}, ImGuiCond_Once);
        ImGui::Begin("Q3 BSP Debug");

        float lmMul = bsp_.lightmapStrength();
        float lmGamma = bsp_.lightmapGamma();
        if (ImGui::SliderFloat("Lightmap strength", &lmMul, 0.0f, 16.0f, "%.2f"))
            bsp_.setLightmapStrength(lmMul);
        if (ImGui::SliderFloat("Lightmap gamma", &lmGamma, 0.05f, 4.0f, "%.2f"))
            bsp_.setLightmapGamma(lmGamma);

        ImGui::SeparatorText("Entities");
        ImGui::Checkbox("Draw entity boxes", &debugEntityBoxes_);
        ImGui::SliderFloat("Box size", &entityBoxSize_, 0.05f, 1.5f, "%.2f");

        if (ImGui::Button("Reset Lightmap Params"))
        {
            bsp_.setLightmapStrength(2.0f);
            bsp_.setLightmapGamma(1.0f);
        }

        const auto totalEnts = bsp_.entities().size();
        const auto lights = bsp_.entitiesByKind(Q3BspMap::EntityKind::Light).size();
        const auto doors = bsp_.entitiesByKind(Q3BspMap::EntityKind::FuncDoor).size();
        const auto plats = bsp_.entitiesByKind(Q3BspMap::EntityKind::FuncPlat).size();
        const auto trains = bsp_.entitiesByKind(Q3BspMap::EntityKind::FuncTrain).size();
        ImGui::Text("Total entities: %zu", totalEnts);
        ImGui::Text("Lights: %zu  Doors: %zu  Plats: %zu  Trains: %zu",
                    lights, doors, plats, trains);

        ImGui::End();
    }

    void spawnEntityLights()
    {
        if (!bspNode_)
            return;

        const auto lights = bsp_.entitiesByKind(Q3BspMap::EntityKind::Light);
        const int maxLights = 64;
        int spawned = 0;

        for (const Q3BspMap::MapEntity *e : lights)
        {
            if (!e || spawned >= maxLights)
                break;

            glm::vec3 originLocal;
            if (!bsp_.entityOriginLocal(*e, originLocal))
                continue;

            auto *pl = new PointLight();
            pl->name = "bsp_light_" + std::to_string(spawned);
            pl->setPosition(originLocal);

            // Q3 light key commonly uses values around 200..800.
            const float q3Light = e->getFloat("light", 300.0f);
            pl->intensity = std::max(0.05f, q3Light / 300.0f);
            pl->range = std::max(4.0f, q3Light * bsp_.scale() * 2.0f);

            glm::vec3 color;
            pl->color = e->getVec3("color", color) ? glm::clamp(color, glm::vec3(0.0f), glm::vec3(4.0f))
                                                   : glm::vec3(1.0f);

            bspNode_->addChild(pl);
            entityLights_.push_back(pl);
            ++spawned;
        }

        SDL_Log("[DemoQ3Bsp] spawned %d/%zu point lights from BSP entities",
                spawned, lights.size());
    }

    Q3BspMap bsp_;
    Q3BspNode *bspNode_ = nullptr;
    ForwardTechnique *forwardTechnique_ = nullptr;
    std::string loadedMap_;
    std::vector<PointLight *> entityLights_;
    bool debugEntityBoxes_ = true;
    float entityBoxSize_ = 0.22f;
};
