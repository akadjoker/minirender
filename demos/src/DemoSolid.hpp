#pragma once

#include "Demo.hpp"

class DemoSolid final : public Demo
{
public:
    const char *name() const override { return "solid"; }

    void build(Scene &scene, Device &device) override
    {
        createMainCamera(scene, device);
        createSun(scene);

        auto &meshes = MeshManager::instance();
        auto &materials = MaterialManager::instance();

        Mesh *groundMesh = meshes.create_plane("solid_ground_mesh", 24.f, 24.f, 1);
        Mesh *cubeMesh = meshes.create_cube("solid_cube_mesh", 2.f);

        materials.createSolid("solid_ground_mat", {0.45f, 0.48f, 0.52f, 1.0f});
        materials.createSolid("solid_red_mat", {0.82f, 0.24f, 0.18f, 1.0f});
        materials.createSolid("solid_green_mat", {0.22f, 0.68f, 0.34f, 1.0f});
        materials.createSolid("solid_blue_mat", {0.24f, 0.42f, 0.82f, 1.0f});

        MeshNode *ground = scene.createMeshNode("ground", groundMesh);
        ground->setMaterial("solid_ground_mat");

        MeshNode *leftCube = scene.createMeshNode("cube_red", cubeMesh);
        leftCube->setPosition({-4.f, 1.f, 0.f});
        leftCube->setMaterial("solid_red_mat");

        MeshNode *centerCube = scene.createMeshNode("cube_green", cubeMesh);
        centerCube->setPosition({0.f, 1.f, 0.f});
        centerCube->setMaterial("solid_green_mat");

        MeshNode *rightCube = scene.createMeshNode("cube_blue", cubeMesh);
        rightCube->setPosition({4.f, 1.f, 0.f});
        rightCube->setMaterial("solid_blue_mat");
    }
};
