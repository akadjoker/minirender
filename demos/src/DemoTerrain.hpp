#pragma once

#include "Demo.hpp"
#include "TerrainNode.hpp"

class DemoTerrain final : public Demo
{
public:
    const char *name() const override { return "terrain"; }

    void build(Scene &scene, Device &device) override
    {
        createMainCamera(scene, device, {0.f, 30.f, 60.f}, {0.f, 5.f, 0.f});
        configureFreeCamera(45.f, 0.18f, 3.5f);
        camera()->farPlane = 1600.f;
        camera()->nearPlane = 0.1f;
        camera()->clearColorVal = {0.52f, 0.68f, 0.88f, 1.0f};

        createSun(scene);

        auto &materials = MaterialManager::instance();
        auto &textures = TextureManager::instance();

        Texture *base = textures.load("terrain_base", "assets/terrain-texture.jpg");
        if (!base)
            base = textures.getWhite();

        Material *terrainMaterial = materials.createTextured("terrain_mat",
                                                             base,
                                                             {1.f, 1.f, 1.f, 1.f});

        TerrainLodNode *terrain = scene.createTerrainLodNode("terrain");
        terrain->setScale({1000.f, 100.f, 1000.f});
        if (terrain->loadFromHeightmap("assets/terrain-heightmap.png", 1.f, 0))
        {
            terrain->setMaterial(terrainMaterial);
            terrain->setPosition({-500.f, -100.f, -500.f});
        }
        else
        {
            scene.remove(terrain);
            delete terrain;
        }
    }
};
