#pragma once

#include "Demo.hpp"

class DemoTexture final : public Demo
{
public:
    const char *name() const override { return "texture"; }

    void build(Scene &scene, Device &device) override
    {
        createMainCamera(scene, device);
        createSun(scene);

        auto &meshes = MeshManager::instance();
        auto &materials = MaterialManager::instance();
        auto &textures = TextureManager::instance();

        Mesh *groundMesh = meshes.create_plane("texture_ground_mesh", 24.f, 24.f, 1);
        Mesh *cubeMesh = meshes.create_cube("texture_cube_mesh", 2.f);

        Texture *pattern = textures.getPattern();
        Texture *white = textures.getWhite();

        materials.createTextured("texture_ground_mat", pattern, {0.70f, 0.70f, 0.72f, 1.0f});
        materials.createTextured("texture_red_mat", pattern, {0.95f, 0.55f, 0.55f, 1.0f});
        materials.createTextured("texture_white_mat", white, {1.0f, 1.0f, 1.0f, 1.0f});
        materials.createTextured("texture_blue_mat", pattern, {0.55f, 0.70f, 1.0f, 1.0f});

        MeshNode *ground = scene.createMeshNode("ground", groundMesh);
        ground->setMaterial("texture_ground_mat");

        MeshNode *leftCube = scene.createMeshNode("cube_red", cubeMesh);
        leftCube->setPosition({-4.f, 1.f, 0.f});
        leftCube->setMaterial("texture_red_mat");

        MeshNode *centerCube = scene.createMeshNode("cube_white", cubeMesh);
        centerCube->setPosition({0.f, 1.f, 0.f});
        centerCube->setMaterial("texture_white_mat");

        MeshNode *rightCube = scene.createMeshNode("cube_blue", cubeMesh);
        rightCube->setPosition({4.f, 1.f, 0.f});
        rightCube->setMaterial("texture_blue_mat");
    }
};
