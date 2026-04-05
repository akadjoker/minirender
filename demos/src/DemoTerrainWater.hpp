#pragma once

#include "Demo.hpp"
#include "TerrainNode.hpp"
#include "WaterNode.hpp"
#include "imgui.h"

class DemoTerrainWater final : public Demo
{
public:
    const char *name() const override { return "terrain_water"; }

    void syncWaterMaterial()
    {
        if (!water_)
            return;

        Material *waterMat = water_->getMaterial();
        if (!waterMat)
            return;

        waterMat->setFloat("u_reflectivity", water_->reflectivity);
        waterMat->setFloat("u_distortStrength", water_->distortStrength);
        waterMat->setFloat("u_waveHeight", water_->waveHeight);
        waterMat->setFloat("u_waveLength", water_->waveLength);
        waterMat->setFloat("u_foamIntensity", water_->foamIntensity);
        waterMat->setFloat("u_depthMult", water_->depthMult);
        waterMat->setFloat("u_foamScale", water_->foamScale);
        waterMat->setFloat("u_foamSpeed", water_->foamSpeed);
        waterMat->setFloat("u_foamRange", water_->foamRange);
        waterMat->setFloat("u_shoreFadeRange", water_->shoreFadeRange);
        waterMat->setFloat("u_depthDiscardCutoff", water_->depthDiscardCutoff);
        waterMat->setInt("u_foamEnabled", water_->foamEnabled ? 1 : 0);
        waterMat->setFloat("u_waterLevel", water_->waterHeight());
        waterMat->setVec4("u_waterColor", water_->waterColor);
        waterMat->setFloat("u_colorBlendFactor", water_->colorBlendFactor);

        if (terrain_)
        {
            waterMat->setVec2("u_terrainOrigin", {terrain_->position.x, terrain_->position.z});
            waterMat->setVec2("u_terrainSize", {terrain_->scale.x, terrain_->scale.z});
            waterMat->setFloat("u_terrainBaseY", terrain_->position.y);
            waterMat->setFloat("u_terrainMaxHeight", terrain_->scale.y);
            if (terrainHeightTex_)
                waterMat->setTexture("u_heightMap", terrainHeightTex_);
        }
    }

    void applySunAngles()
    {
        if (!sun_)
            return;
        sun_->setEulerAngles({sunElevation_, sunAzimuth_, 0.f});
    }

    void build(Scene &scene, Device &device) override
    {
        createMainCamera(scene, device, {0.f, 20.f, 60.f}, {0.f, 4.f, 0.f});
        configureFreeCamera(35.f, 0.18f, 3.5f);
        camera()->farPlane = 1000.f;
        camera()->nearPlane = 0.1f;
        camera()->clearColorVal = {0.45f, 0.62f, 0.86f, 1.0f};

        scene.setSkyEnabled(true);
        scene.setSkyColors({0.20f, 0.38f, 0.78f},
                           {0.78f, 0.86f, 0.96f},
                           {0.16f, 0.18f, 0.22f});

        sun_ = scene.createLight<DirectionalLight>("sun");
        sun_->color = {1.0f, 0.95f, 0.85f};
        sun_->intensity = 1.1f;
        sun_->ambient = {0.10f, 0.12f, 0.16f};
        sun_->castShadow = true;
        applySunAngles();

        auto &materials = MaterialManager::instance();
        auto &textures = TextureManager::instance();

        Texture *terrainTex = textures.load("tw_terrain_base", "assets/terrain-texture.jpg");
        if (!terrainTex)
            terrainTex = textures.getWhite();
        terrainHeightTex_ = textures.load("tw_terrain_height", "assets/terrain-heightmap.png");

        Material *terrainMat = materials.createTextured("tw_terrain_mat",
                                                        terrainTex,
                                                        {1.f, 1.f, 1.f, 1.f});

        terrain_ = scene.createTerrainLodNode("terrain");
        terrain_->setScale({1000.f, 100.f, 1000.f});
        if (terrain_->loadFromHeightmap("assets/terrain-heightmap.png", 1.f, 0))
        {
            terrain_->setMaterial(terrainMat);
            terrain_->setPosition({-500.f, -100.f, -500.f});
            terrain_->castShadow = true;
            terrain_->receiveShadow = true;
        }

        water_ = scene.createWaterNode("lake");
        water_->rtWidth = 1024;
        water_->rtHeight = 1024;
        water_->reflectivity = 0.6f;
        water_->distortStrength = 0.025f;
        water_->waveHeight = 0.65f;
        water_->waveLength = 14.0f;
        water_->windForce = 0.06f;
        water_->windDirection = {0.85f, 0.28f};
        water_->foamIntensity = 0.9f;
        water_->clipBias = 0.05f;
        water_->depthMult = 8.0f;
        water_->foamScale = 0.16f;
        water_->foamSpeed = 0.08f;
        water_->foamRange = 2.6f;
        water_->shoreFadeRange = 1.2f;
        water_->colorBlendFactor = 0.24f;
        water_->waterColor = {0.05f, 0.20f, 0.31f, 1.f};
        water_->uvTile = 24.0f;
        water_->castShadow = false;
        water_->receiveShadow = false;

        if (terrain_)
        {
            const BoundingBox bounds = terrain_->getAABB();
            const glm::vec3 center = bounds.center();
            waterSizeX_ = (bounds.max.x - bounds.min.x) + 24.0f;
            waterSizeZ_ = (bounds.max.z - bounds.min.z) + 24.0f;
            waterHeight_ = bounds.min.y + (bounds.max.y - bounds.min.y) * 0.35f;
            water_->init(waterSizeX_, waterSizeZ_);
            water_->setPosition({center.x, waterHeight_, center.z});
        }
        else
        {
            water_->init(1000.f, 1000.f);
            water_->setPosition({0.f, waterHeight_, 0.f});
        }

        syncWaterMaterial();
    }

    void drawImGui(Renderer &renderer) override
    {
        ImGui::Begin("Terrain Water");

        bool sunChanged = false;
        sunChanged |= ImGui::SliderFloat("Sun Elevation", &sunElevation_, -89.0f, 89.0f);
        sunChanged |= ImGui::SliderFloat("Sun Azimuth", &sunAzimuth_, -180.0f, 180.0f);
        if (sunChanged)
            applySunAngles();

        if (sun_)
        {
            ImGui::Checkbox("Sun Cast Shadow", &sun_->castShadow);
            ImGui::ColorEdit3("Sun Color", &sun_->color.x);
            ImGui::SliderFloat("Sun Intensity", &sun_->intensity, 0.0f, 4.0f);
            ImGui::ColorEdit3("Ambient", &sun_->ambient.x);
        }

        float shadowBias = renderer.shadowBias();
        if (ImGui::SliderFloat("Shadow Bias", &shadowBias, 0.0001f, 0.02f, "%.4f", ImGuiSliderFlags_Logarithmic))
            renderer.setShadowBias(shadowBias);

        if (terrain_)
        {
            ImGui::Separator();
            ImGui::Text("Terrain");
            ImGui::Checkbox("Terrain Cast Shadow", &terrain_->castShadow);
            ImGui::Checkbox("Terrain Receive Shadow", &terrain_->receiveShadow);
        }

        if (water_)
        {
            ImGui::Separator();
            ImGui::Text("Water");
            ImGui::SliderFloat("Reflectivity", &water_->reflectivity, 0.0f, 1.0f);
            ImGui::SliderFloat("Distort", &water_->distortStrength, 0.0f, 0.1f);
            ImGui::SliderFloat("Wave Height", &water_->waveHeight, 0.0f, 1.8f);
            ImGui::SliderFloat("Wave Length", &water_->waveLength, 4.0f, 32.0f);
            ImGui::SliderFloat("Foam Intensity", &water_->foamIntensity, 0.0f, 2.0f);
            ImGui::SliderFloat("Foam Range", &water_->foamRange, 0.1f, 8.0f);
            ImGui::SliderFloat("Foam Scale", &water_->foamScale, 0.02f, 0.6f);
            ImGui::SliderFloat("Foam Speed", &water_->foamSpeed, 0.0f, 0.4f);
            ImGui::SliderFloat("Depth Mult", &water_->depthMult, 0.5f, 40.0f);
            ImGui::SliderFloat("Depth Cutoff", &water_->depthDiscardCutoff, 0.0f, 0.2f);
            ImGui::SliderFloat("Shore Fade", &water_->shoreFadeRange, 0.0f, 12.0f);
            ImGui::SliderFloat("Clip Bias", &water_->clipBias, 0.0f, 5.0f);
            if (ImGui::SliderFloat("Water Height", &waterHeight_, -120.0f, 20.0f))
            {
                glm::vec3 p = water_->position;
                water_->setPosition({p.x, waterHeight_, p.z});
            }
            ImGui::ColorEdit4("Water Color", &water_->waterColor.x);
            ImGui::SliderFloat("Color Blend", &water_->colorBlendFactor, 0.0f, 1.0f);
            ImGui::Checkbox("Foam Enabled", &water_->foamEnabled);
            ImGui::Text("Water Size: %.1f x %.1f", waterSizeX_, waterSizeZ_);

            syncWaterMaterial();

            ImGui::Checkbox("Show RT Debug", &showDebug_);
            if (showDebug_)
            {
                const float w = 220.0f;
                const float h = 124.0f;
                if (Texture *tex = water_->debugReflTex())
                {
                    ImGui::Text("Reflection");
                    ImGui::Image((ImTextureID)(intptr_t)tex->id, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
                }
                if (Texture *tex = water_->debugRefrTex())
                {
                    ImGui::Text("Refraction");
                    ImGui::Image((ImTextureID)(intptr_t)tex->id, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
                }
                if (Texture *tex = water_->debugRefrDepthTex())
                {
                    ImGui::Text("Refraction Depth");
                    ImGui::Image((ImTextureID)(intptr_t)tex->id, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
                }
            }
        }

        FreeCameraController *controller = freeCamera();
        if (controller)
        {
            ImGui::Separator();
            ImGui::Text("Camera");
            ImGui::SliderFloat("Move Speed", &controller->moveSpeed, 1.0f, 120.0f);
            ImGui::SliderFloat("Mouse Sens.", &controller->mouseSensitivity, 0.02f, 0.5f);
            ImGui::SliderFloat("Sprint", &controller->sprintMultiplier, 1.0f, 8.0f);
        }

        ImGui::End();
    }

private:
    TerrainLodNode *terrain_ = nullptr;
    WaterNode3D *water_ = nullptr;
    DirectionalLight *sun_ = nullptr;
    Texture *terrainHeightTex_ = nullptr;
    float sunElevation_ = 55.f;
    float sunAzimuth_ = -35.f;
    float waterHeight_ = -50.f;
    float waterSizeX_ = 1000.f;
    float waterSizeZ_ = 1000.f;
    bool showDebug_ = true;
};
