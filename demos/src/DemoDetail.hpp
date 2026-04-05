#pragma once

#include "Demo.hpp"

class DemoDetail final : public Demo
{
public:
    const char *name() const override { return "detail"; }

    void build(Scene &scene, Device &device) override
    {
        createMainCamera(scene, device, {0.f, 7.f, 18.f});
        createSun(scene);

        auto &meshes = MeshManager::instance();
        auto &materials = MaterialManager::instance();
        auto &textures = TextureManager::instance();

        Mesh *groundMesh = meshes.create_plane("detail_ground_mesh", 28.f, 28.f, 1);
        Mesh *cubeMesh = meshes.create_cube("detail_cube_mesh", 2.f);

        Texture *white = textures.getWhite();
        Texture *pattern = textures.getPattern();

        materials.createDetail("detail_ground_mat", white, pattern, 12.0f,
                               {0.45f, 0.48f, 0.52f, 1.0f});
        materials.createDetail("detail_red_mat", white, pattern, 6.0f,
                               {0.82f, 0.24f, 0.18f, 1.0f});
        materials.createTextured("detail_center_mat", pattern, {0.90f, 0.90f, 0.95f, 1.0f});
        materials.createDetail("detail_blue_mat", white, pattern, 20.0f,
                               {0.24f, 0.42f, 0.82f, 1.0f});

        MeshNode *ground = scene.createMeshNode("ground", groundMesh);
        ground->setMaterial("detail_ground_mat");

        MeshNode *leftCube = scene.createMeshNode("cube_red", cubeMesh);
        leftCube->setPosition({-4.f, 1.f, 0.f});
        leftCube->setMaterial("detail_red_mat");

        MeshNode *centerCube = scene.createMeshNode("cube_center", cubeMesh);
        centerCube->setPosition({0.f, 1.f, 0.f});
        centerCube->setMaterial("detail_center_mat");

        MeshNode *rightCube = scene.createMeshNode("cube_blue", cubeMesh);
        rightCube->setPosition({4.f, 1.f, 0.f});
        rightCube->setMaterial("detail_blue_mat");
    }
};
