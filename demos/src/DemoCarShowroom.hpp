#pragma once

#include "DemoBase.hpp"
#include "Input.hpp"
#include "RenderPipeline.hpp"
#include "RenderTarget.hpp"
#include "Device.hpp"
#include "imgui.h"

#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

class DemoCarShowroom : public DemoBase
{
public:
    const char *name() override { return "Car Showroom"; }

    bool init() override
    {
        if (!DemoBase::init())
            return false;

        camera->setPosition(glm::vec3(0.f, 2.0f, 8.0f));
        camera->lookAt(glm::vec3(0.f, 1.2f, 0.f));
        camera->nearPlane = 0.05f;
        camera->farPlane = 20000.0f;

        FreeCameraController *ctrl = static_cast<FreeCameraController *>(camera->getController());
        if (ctrl)
            ctrl->moveSpeed = 10.0f;

        reflectShader_ = shaders().load("showroom_reflect",
                                        "assets/shaders/showroom_reflect.vert",
                                        "assets/shaders/showroom_reflect.frag");
        skyboxShader_ = shaders().load("showroom_skybox",
                                       "assets/shaders/showroom_skybox.vert",
                                       "assets/shaders/showroom_skybox.frag");

        if (!reflectShader_ || !skyboxShader_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoCarShowroom] failed to load showroom shaders");
            return false;
        }

        materials().setDefaults(reflectShader_, textures().getWhite());

        if (!loadEnvironmentCubemap())
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[DemoCarShowroom] cubemap not found, reflections disabled");
        }

        createLights();
        if (!loadCarModel())
            return false;

        createFloor();
        createSkybox();

        technique_ = new ForwardTechnique();
        scene.addTechnique(technique_);

        createProbes();
        createProbeRTT();
        createFloorReflectionRT();
        applyMaterialParams();

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);

        if (skyboxNode_ && camera)
            skyboxNode_->setPosition(camera->position);

        if (carNode_)
        {
            if (Input::IsKeyDown(KEY_LEFT))
                carNode_->yaw(80.0f * dt);
            if (Input::IsKeyDown(KEY_RIGHT))
                carNode_->yaw(-80.0f * dt);

            if (autoRotate_)
                carNode_->yaw(autoRotateSpeed_ * dt);
        }

        // Orbita as esferas à volta do carro
        for (auto &s : spheres_)
        {
            s.angle += s.speed * dt;
            s.node->setPosition(glm::vec3(
                s.radius * std::cos(s.angle),
                s.height,
                s.radius * std::sin(s.angle)));
        }

        updateFloorPlanarCenter();
    }

    void render() override
    {
        renderFloorReflection();
        renderProbeFace();
        DemoBase::render();
        onImGui();
    }

    void release() override
    {
        carMesh_ = nullptr;
        floorMesh_ = nullptr;
        sphereMesh_ = nullptr;

        carNode_ = nullptr;
        floorNode_ = nullptr;
        skyboxNode_ = nullptr;
        spheres_.clear();

        carMaterial_ = nullptr;
        floorMaterial_ = nullptr;
        skyboxMaterial_ = nullptr;
        reflectionMaterials_.clear();

        envCubemap_ = nullptr;
        reflectShader_ = nullptr;
        skyboxShader_ = nullptr;
        technique_ = nullptr;

        // Probe RTT cleanup
        delete probeRt_;
        probeRt_ = nullptr;
        delete floorReflectRt_;
        floorReflectRt_ = nullptr;
        if (probeCubeFbo_)      { glDeleteFramebuffers(1, &probeCubeFbo_);      probeCubeFbo_ = 0; }
        if (probeDynCubeTex_)   { glDeleteTextures(1, &probeDynCubeTex_->id);   delete probeDynCubeTex_;  probeDynCubeTex_  = nullptr; }
        delete probeCam_;      probeCam_      = nullptr;
        delete floorReflectCam_; floorReflectCam_ = nullptr;

        loadedModelPath_.clear();
        DemoBase::release();
    }

private:
    struct ModelCandidate
    {
        const char *path;
        const char *textureDir;
    };

    bool fileExists(const std::string &path) const
    {
        if (path.empty())
            return false;

        SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw)
            return false;

        SDL_RWclose(rw);
        return true;
    }

    bool tryFaceLayout(const std::string &dir,
                       const char *ext,
                       const char *const names[6],
                       std::vector<std::string> &outFaces) const
    {
        outFaces.clear();
        for (int i = 0; i < 6; ++i)
        {
            const std::string path = dir + "/" + names[i] + ext;
            if (!fileExists(path))
            {
                outFaces.clear();
                return false;
            }
            outFaces.push_back(path);
        }
        return true;
    }

    bool tryCubemapPrefix(const std::string &dir,
                          const std::string &prefix,
                          const char *ext,
                          std::vector<std::string> &outFaces) const
    {
        // OpenGL cubemap face order: +X -X +Y -Y +Z -Z
        static const char *suffixes[6] = {"_rt", "_lf", "_up", "_dn", "_fr", "_bk"};

        outFaces.clear();
        for (int i = 0; i < 6; ++i)
        {
            const std::string path = dir + "/" + prefix + suffixes[i] + ext;
            if (!fileExists(path))
            {
                outFaces.clear();
                return false;
            }
            outFaces.push_back(path);
        }
        return true;
    }

    bool loadEnvironmentCubemap()
    {
        std::vector<std::string> faces;

        const std::string cubemapDir = "assets/cubemaps";
        const char *skyNames[] = {
            "cloudy_noon",
            "early_morning",
            "evening",
            "morning",
            "stormy"
        };
        const char *skyExts[] = {".jpg", ".png", ".jpeg", ".bmp"};

        for (int i = 0; i < 5; ++i)
        {
            for (int e = 0; e < 4; ++e)
            {
                if (tryCubemapPrefix(cubemapDir, skyNames[i], skyExts[e], faces))
                {
                    loadedCubemapName_ = skyNames[i];
                    envCubemap_ = textures().loadCubemap("showroom_env", faces);
                    return envCubemap_ != nullptr;
                }
            }
        }

        static const char *layoutA[6] = {"px", "nx", "py", "ny", "pz", "nz"};
        static const char *layoutB[6] = {"right", "left", "top", "bottom", "front", "back"};

        const char *dirs[] = {
            "assets/skybox/car",
            "assets/skybox/showroom",
            "assets/skybox"
        };

        const char *exts[] = {".png", ".jpg", ".jpeg", ".bmp"};

        bool found = false;
        for (int d = 0; d < 3 && !found; ++d)
        {
            for (int e = 0; e < 4 && !found; ++e)
            {
                if (tryFaceLayout(dirs[d], exts[e], layoutA, faces) ||
                    tryFaceLayout(dirs[d], exts[e], layoutB, faces))
                {
                    found = true;
                }
            }
        }

        if (!found)
        {
            const std::string fallback = "assets/powerplant/textures/tank_top.png";
            if (fileExists(fallback))
            {
                faces.assign(6, fallback);
                found = true;
            }
        }

        if (!found)
            return false;

        loadedCubemapName_ = "legacy";
        envCubemap_ = textures().loadCubemap("showroom_env", faces);
        return envCubemap_ != nullptr;
    }

    void createLights()
    {
        DirectionalLight *sun = scene.createLight<DirectionalLight>("sun");
        sun->intensity = 1.35f;
        sun->ambient = glm::vec3(0.15f, 0.15f, 0.18f);
        sun->setPosition(glm::vec3(0.f, 10.f, 0.f));
        sun->lookDirection(glm::normalize(glm::vec3(-0.6f, -1.0f, -0.2f)));

        PointLight *fill = scene.createLight<PointLight>("fill");
        fill->setPosition(glm::vec3(3.5f, 2.8f, 2.0f));
        fill->color = glm::vec3(0.35f, 0.40f, 0.55f);
        fill->intensity = 3.0f;
        fill->range = 15.0f;
    }

    bool loadCarModel()
    {
        const ModelCandidate candidates[] = {
            {"assets/3ds/bmw/model.3ds", "assets/3ds/bmw/textures"},
            {"assets/3ds/aston/aston.3ds", "assets/3ds/aston/textures"},
            {"assets/3ds/aston/aston.3ds", "assets/3ds/aston"},
        };

        const int candidateCount = static_cast<int>(sizeof(candidates) / sizeof(candidates[0]));

        for (int i = 0; i < candidateCount; ++i)
        {
            const std::string path = candidates[i].path;
            if (!fileExists(path))
                continue;

            carMesh_ = meshes().load("showroom_car", path, candidates[i].textureDir);
            if (!carMesh_)
                continue;

            // Imprimir nomes de todas as surfaces para diagnóstico
            SDL_Log("[DemoCarShowroom] surfaces em '%s':", path.c_str());
            for (size_t s = 0; s < carMesh_->surfaces.size(); ++s)
                SDL_Log("  [%zu] '%s'  idx=%u  count=%u",
                        s,
                        carMesh_->surfaces[s].name.c_str(),
                        carMesh_->surfaces[s].index_start,
                        carMesh_->surfaces[s].index_count);

         

            loadedModelPath_ = path;
            break;
        }

        if (!carMesh_)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[DemoCarShowroom] no supported car model found (try assets/gltf/car.glb)");
            return false;
        }

        carNode_ = scene.createMeshNode("car", carMesh_);
        if (!carNode_)
            return false;

        // MeshWriter writer;
        //  writer.save(carMesh_, "assets/3ds/bmw/model.h3d");

        const glm::vec3 size = carMesh_->aabb.size();
        const float maxDim = std::max(size.x, std::max(size.y, size.z));
        const float scale = (maxDim > 1e-6f) ? (4.0f / maxDim) : 1.0f;

        carNode_->setScale(glm::vec3(scale));
        carNode_->setPosition(glm::vec3(0.f, -carMesh_->aabb.min.y * scale + 0.02f, 0.f));
      //  carNode_->setEulerAngles(glm::vec3(0.f, 180.f, 0.f));
 
        // If the mesh has per-surface materials, update all of them.
        for (size_t i = 0; i < carMesh_->materials.size(); ++i)
        {
            Material *m = carMesh_->materials[i];
            if (!m)
                continue;

            m->setShader(reflectShader_);
            m->setInt("u_hasAlbedo", m->hasTexture("u_albedo") ? 1 : 0);
            m->setFloat("u_reflectivity", carReflectivity_);
            m->setFloat("u_roughness", carRoughness_);
            m->setFloat("u_fresnelPower", fresnelPower_);
            m->setFloat("u_exposure", exposure_);
            m->setInt("u_captureMode", 0);
            m->setInt("u_usePlanarReflection", 0);
            m->setMat4("u_reflectionViewProj", glm::mat4(1.0f));
            if (envCubemap_)
                m->setTexture("u_envMap", envCubemap_);
            m->setDepthTest(true);
            if (m->blend)
            {
                m->setCullFace(false);
                m->setDepthWrite(false);
            }
            else
            {
                m->setCullFace(false);
                m->setDepthWrite(true);
            }

            if (!carMaterial_)
                carMaterial_ = m;

            registerReflectionMaterial(m);
        }

        return true;
    }

    void createFloor()
    {
        floorMesh_ = meshes().create_plane("showroom_floor_mesh", 45.f, 45.f, 70);
        floorNode_ = scene.createMeshNode("showroom_floor", floorMesh_);
        if (!floorMesh_ || !floorNode_)
            return;

        floorNode_->setPosition(glm::vec3(0.f, 0.f, 0.f));

        floorMaterial_ = materials().create("showroom_floor_mat");
        floorMaterial_->setShader(reflectShader_);

        Texture *floorTex = textures().getWhite();
        if (fileExists("assets/powerplant/textures/tank_top.png"))
        {
            Texture *tex = textures().load("showroom_floor_tex",
                                           "assets/powerplant/textures/tank_top.png");
            if (tex)
                floorTex = tex;
        }

        floorMaterial_->setTexture("u_albedo", floorTex);
        floorMaterial_->setInt("u_hasAlbedo", 1);
        floorMaterial_->setVec3("u_albedoTint", glm::vec3(0.22f, 0.24f, 0.28f));
        floorMaterial_->setFloat("u_reflectivity", floorReflectivity_);
        floorMaterial_->setFloat("u_roughness", floorRoughness_);
        floorMaterial_->setFloat("u_fresnelPower", fresnelPower_);
        floorMaterial_->setFloat("u_exposure", exposure_);
        floorMaterial_->setFloat("u_opacity", 1.0f);
        floorMaterial_->setInt("u_captureMode", 0);
        floorMaterial_->setInt("u_usePlanarReflection", 1);
        floorMaterial_->setInt("u_debugPlanarMode", debugPlanarMode_);
        floorMaterial_->setVec3("u_planarCenter", glm::vec3(0.0f));
        floorMaterial_->setFloat("u_planarRadius", floorPlanarRadius_);
        floorMaterial_->setFloat("u_planarSoftness", floorPlanarSoftness_);
        floorMaterial_->setFloat("u_planarViewFalloff", floorPlanarViewFalloff_);
        floorMaterial_->setMat4("u_reflectionViewProj", glm::mat4(1.0f));
        floorMaterial_->setCullFace(true);
        floorMaterial_->setDepthTest(true);
        floorMaterial_->setDepthWrite(true);
        if (envCubemap_)
            floorMaterial_->setTexture("u_envMap", envCubemap_);

        floorNode_->setMaterial(floorMaterial_->name);
        registerReflectionMaterial(floorMaterial_);
    }

    void createSkybox()
    {
        if (!envCubemap_)
            return;

        Mesh *skyMesh = meshes().create_cube("showroom_sky_cube", 1.0f);
        if (!skyMesh)
            return;

        skyboxNode_ = scene.createMeshNode("showroom_skybox", skyMesh);
        if (!skyboxNode_)
            return;

        skyboxNode_->setScale(glm::vec3(700.0f));
        if (camera)
            skyboxNode_->setPosition(camera->position);

        skyboxMaterial_ = materials().create("showroom_skybox_mat");
        skyboxMaterial_->setShader(skyboxShader_);
        skyboxMaterial_->setTexture("u_skyCube", envCubemap_);
        skyboxMaterial_->setFloat("u_skyExposure", skyExposure_);
        skyboxMaterial_->setCullFace(false);
        skyboxMaterial_->setDepthTest(true);
        skyboxMaterial_->setDepthWrite(false);

        skyboxNode_->setMaterial(skyboxMaterial_->name);
    }

    void applyMaterialParams()
    {
        if (carMesh_)
        {
            for (size_t i = 0; i < carMesh_->materials.size(); ++i)
            {
                Material *m = carMesh_->materials[i];
                if (!m)
                    continue;

                m->setFloat("u_reflectivity", carReflectivity_);
                m->setFloat("u_roughness", carRoughness_);
                m->setFloat("u_fresnelPower", fresnelPower_);
                m->setFloat("u_exposure", exposure_);
                // Usa cubemap dinâmico se disponível, senão o estático
                Texture *envTex = probeDynCubeTex_ ? probeDynCubeTex_ : envCubemap_;
                if (envTex)
                    m->setTexture("u_envMap", envTex);
            }
        }

        if (floorMaterial_)
        {
            floorMaterial_->setFloat("u_reflectivity", floorReflectivity_);
            floorMaterial_->setFloat("u_roughness", floorRoughness_);
            floorMaterial_->setFloat("u_fresnelPower", fresnelPower_);
            floorMaterial_->setFloat("u_exposure", exposure_);
            floorMaterial_->setInt("u_usePlanarReflection", floorReflectRt_ ? 1 : 0);
            floorMaterial_->setInt("u_debugPlanarMode", debugPlanarMode_);
            floorMaterial_->setFloat("u_planarRadius", floorPlanarRadius_);
            floorMaterial_->setFloat("u_planarSoftness", floorPlanarSoftness_);
            floorMaterial_->setFloat("u_planarViewFalloff", floorPlanarViewFalloff_);
            if (floorReflectRt_ && floorReflectRt_->colorTex())
                floorMaterial_->setTexture("u_planarReflection", floorReflectRt_->colorTex());
            else if (envCubemap_)
                floorMaterial_->setTexture("u_envMap", envCubemap_);
        }

        if (skyboxMaterial_)
            skyboxMaterial_->setFloat("u_skyExposure", skyExposure_);
    }

    // ── Cubemap face directions  ──────────────────────────
    static constexpr int kProbeSize = 256;

    struct FaceDesc { glm::vec3 dir; glm::vec3 up; };
    static const FaceDesc &probeFaceDesc(int i)
    {
        static const FaceDesc kFaces[6] = {
            {{ 1, 0, 0}, {0,-1, 0}},  // +X
            {{-1, 0, 0}, {0,-1, 0}},  // -X
            {{ 0, 1, 0}, {0, 0, 1}},  // +Y
            {{ 0,-1, 0}, {0, 0,-1}},  // -Y
            {{ 0, 0, 1}, {0,-1, 0}},  // +Z
            {{ 0, 0,-1}, {0,-1, 0}},  // -Z
        };
        return kFaces[i];
    }

    void createProbes()
    {
        sphereMesh_ = meshes().create_sphere("showroom_sphere_mesh", 0.28f, 32);
        if (!sphereMesh_)
            return;

        // Cores e órbitas das esferas
        struct SphereDef { glm::vec3 color; float radius; float height; float speed; float phaseRad; };
        static const SphereDef kDefs[] = {
            {{1.0f, 0.25f, 0.05f}, 2.2f, 1.4f,  0.9f, 0.0f},               // laranja, órbita baixa
            {{0.1f, 0.4f,  1.0f}, 2.8f, 0.5f, -0.7f, 1.05f},              // azul, órbita muito baixa
            {{0.1f, 0.9f,  0.2f}, 2.4f, 2.2f,  1.2f, 2.09f},              // verde, órbita alta
            {{0.9f, 0.9f,  0.1f}, 1.8f, 1.0f, -1.5f, 3.14f},              // amarelo, órbita intermédia
            {{0.8f, 0.1f,  0.8f}, 3.2f, 1.8f,  0.6f, 4.19f},              // roxo, órbita larga
            {{1.0f, 1.0f,  1.0f}, 2.0f, 0.2f,  1.8f, 5.24f},              // branco, rente ao chão
        };

        for (int i = 0; i < 6; ++i)
        {
            const SphereDef &d = kDefs[i];
            std::string matName = "showroom_sphere_mat_" + std::to_string(i);
            Material *mat = materials().create(matName);
            mat->setShader(reflectShader_);
            mat->setTexture("u_albedo", textures().getWhite());
            mat->setInt("u_hasAlbedo", 0);
            mat->setVec3("u_albedoTint", d.color);
            mat->setFloat("u_reflectivity", 0.0f);
            mat->setFloat("u_roughness", 0.85f);
            mat->setFloat("u_fresnelPower", fresnelPower_);
            mat->setFloat("u_exposure", exposure_);
            mat->setInt("u_captureMode", 0);
            mat->setInt("u_usePlanarReflection", 0);
            mat->setMat4("u_reflectionViewProj", glm::mat4(1.0f));
            mat->setCullFace(true);
            mat->setDepthTest(true);
            mat->setDepthWrite(true);
            if (envCubemap_)
                mat->setTexture("u_envMap", envCubemap_);
            registerReflectionMaterial(mat);

            std::string nodeName = "showroom_sphere_" + std::to_string(i);
            MeshNode *node = scene.createMeshNode(nodeName, sphereMesh_);
            if (!node) continue;
            node->setMaterial(mat->name);

            float startX = d.radius * std::cos(d.phaseRad);
            float startZ = d.radius * std::sin(d.phaseRad);
            node->setPosition(glm::vec3(startX, d.height, startZ));

            SphereOrbit s;
            s.node   = node;
            s.mat    = mat;
            s.radius = d.radius;
            s.height = d.height;
            s.speed  = d.speed;
            s.angle  = d.phaseRad;
            spheres_.push_back(s);
        }
    }

    // Cria cubemap dinâmico + câmara para uma probe
    bool buildProbe(const std::string &camName,
                    Texture *&outCubeTex, GLuint &outFbo, Camera *&outCam)
    {
        outCubeTex = new Texture();
        outCubeTex->width  = kProbeSize;
        outCubeTex->height = kProbeSize;
        outCubeTex->target = GL_TEXTURE_CUBE_MAP;
        glGenTextures(1, &outCubeTex->id);
        glBindTexture(GL_TEXTURE_CUBE_MAP, outCubeTex->id);
        for (int i = 0; i < 6; ++i)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8,
                         kProbeSize, kProbeSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        glGenFramebuffers(1, &outFbo);

        // Câmara gerida por nós (não registada no scene para não ser apagada por ele)
        outCam = new Camera();
        outCam->name         = camName;
        outCam->fov          = 90.0f;
        outCam->nearPlane    = 0.1f;
        outCam->farPlane     = 500.0f;
        outCam->clearColor   = true;
        outCam->clearDepth   = true;
        outCam->clearColorVal = glm::vec4(0.f, 0.f, 0.f, 1.f);
        outCam->setViewport(0, 0, kProbeSize, kProbeSize);
        outCam->updateMatrices();
        return true;
    }

    void createProbeRTT()
    {
        // RT 2D partilhado por ambas as probes
        probeRt_ = new RenderTarget();
        if (!probeRt_->create(kProbeSize, kProbeSize))
            return;
        probeRt_->addColorAttachment();
        probeRt_->addDepthAttachment();
        if (!probeRt_->finalize())
            return;

        // Probe do CARRO (captura do centro do carro)
        buildProbe("probe_car_cam",  probeDynCubeTex_, probeCubeFbo_,  probeCam_);

        // Carro usa cubemap do carro
        if (carMesh_)
            for (size_t i = 0; i < carMesh_->materials.size(); ++i)
                if (carMesh_->materials[i])
                    carMesh_->materials[i]->setTexture("u_envMap", probeDynCubeTex_);

        if (floorMaterial_ && envCubemap_)
            floorMaterial_->setTexture("u_envMap", envCubemap_);
    }

    void createFloorReflectionRT()
    {
        if (!floorReflectCam_)
        {
            floorReflectCam_ = new Camera();
            floorReflectCam_->name = "floor_reflect_cam";
        }

        floorReflectRt_ = new RenderTarget();
        if (!floorReflectRt_->create(1024, 1024))
            return;
        floorReflectRt_->addColorAttachment();
        floorReflectRt_->addDepthAttachment();
        if (!floorReflectRt_->finalize())
        {
            delete floorReflectRt_;
            floorReflectRt_ = nullptr;
            return;
        }

        if (floorMaterial_ && floorReflectRt_->colorTex())
        {
            floorMaterial_->setTexture("u_planarReflection", floorReflectRt_->colorTex());
            floorMaterial_->setInt("u_usePlanarReflection", 1);
        }
    }

    void updateFloorReflectionCamera(float floorY)
    {
        if (!camera || !floorReflectCam_)
            return;

        const glm::vec3 camPos = camera->position;
        const glm::vec3 planeNormal(0.0f, 1.0f, 0.0f);

        floorReflectCam_->setPosition({camPos.x, 2.0f * floorY - camPos.y, camPos.z});
        floorReflectCam_->fov           = camera->fov;
        floorReflectCam_->nearPlane     = camera->nearPlane;
        floorReflectCam_->farPlane      = camera->farPlane;
        floorReflectCam_->viewport      = camera->viewport;
        floorReflectCam_->setAspect(camera->viewport.z, camera->viewport.w);
        floorReflectCam_->clearColor    = camera->clearColor;
        floorReflectCam_->clearDepth    = camera->clearDepth;
        floorReflectCam_->clearColorVal = camera->clearColorVal;
        floorReflectCam_->lookDirection(glm::reflect(camera->forward(), planeNormal),
                                        glm::reflect(camera->up(), planeNormal));
        floorReflectCam_->updateMatrices();
    }

    void renderFloorReflection()
    {
        if (!camera || !floorReflectRt_ || !floorReflectRt_->valid() || !floorMaterial_ || !floorReflectCam_)
            return;

        const float floorY = floorNode_ ? floorNode_->worldPosition().y : 0.0f;
        updateFloorReflectionCamera(floorY);

        setReflectionCaptureMode(true);
        if (floorNode_)
            floorNode_->visible = false;

        scene.setClipPlane({0.f, 1.f, 0.f, -(floorY - 0.02f)});
        scene.renderToTarget(floorReflectCam_, floorReflectRt_);
        scene.clearClipPlanes();

        if (floorNode_)
            floorNode_->visible = true;
        setReflectionCaptureMode(false);

        floorMaterial_->setMat4("u_reflectionViewProj", floorReflectCam_->viewProjection);
    }

    // Renderiza 1 face da probe do carro + 1 face da probe do chão por frame
    void renderProbeFace()
    {
        if (!probeRt_ || !probeRt_->valid()) return;
        auto &dev = Device::Instance();
        setReflectionCaptureMode(true);

        // --- Probe do CARRO (o carro fica invisível para não se reflectir a si próprio) ---
        if (probeCam_ && probeDynCubeTex_ && probeCubeFbo_ && carNode_)
        {
            const int face = probeFaceIndex_;
            probeFaceIndex_ = (probeFaceIndex_ + 1) % 6;

            carNode_->visible = false;
            const glm::vec3 pos = glm::vec3(carNode_->worldMatrix()[3]);
            probeCam_->setPosition(pos);
            const FaceDesc &fd = probeFaceDesc(face);
            probeCam_->lookDirection(fd.dir, fd.up);
            probeCam_->updateMatrices();
            scene.renderToTarget(probeCam_, probeRt_);
            blitToFace(probeRt_->fbo(), probeCubeFbo_, probeDynCubeTex_->id, face);
            carNode_->visible = true;
        }

        setReflectionCaptureMode(false);
        glViewport(0, 0, dev.GetWidth(), dev.GetHeight());
    }

    void blitToFace(GLuint srcFbo, GLuint dstFbo, GLuint cubeTex, int face)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubeTex, 0);
        glBlitFramebuffer(0, 0, kProbeSize, kProbeSize,
                          0, 0, kProbeSize, kProbeSize,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    void onImGui()
    {
        ImGui::SetNextWindowPos(ImVec2(10, 100), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(430, 400), ImGuiCond_Once);

        ImGui::Begin("Car Showroom");

        ImGui::Text("Model: %s", loadedModelPath_.empty() ? "-" : loadedModelPath_.c_str());
        ImGui::Text("Cubemap: %s", envCubemap_ ? "loaded" : "missing");
        ImGui::Text("Sky: %s", loadedCubemapName_.empty() ? "-" : loadedCubemapName_.c_str());

        bool changed = false;

        changed |= ImGui::SliderFloat("Car reflect", &carReflectivity_, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Car roughness", &carRoughness_, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Floor reflect", &floorReflectivity_, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Floor roughness", &floorRoughness_, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Fresnel power", &fresnelPower_, 0.5f, 8.0f, "%.2f");
        changed |= ImGui::SliderFloat("Exposure", &exposure_, 0.3f, 2.5f, "%.2f");
        changed |= ImGui::SliderFloat("Sky exposure", &skyExposure_, 0.3f, 2.5f, "%.2f");
        changed |= ImGui::SliderFloat("Floor area", &floorPlanarRadius_, 1.0f, 10.0f, "%.2f");
        changed |= ImGui::SliderFloat("Floor fade", &floorPlanarSoftness_, 0.1f, 10.0f, "%.2f");
        changed |= ImGui::SliderFloat("Floor view fade", &floorPlanarViewFalloff_, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderInt("Planar debug", &debugPlanarMode_, 0, 2);

        ImGui::Separator();
        ImGui::Checkbox("Auto rotate", &autoRotate_);
        ImGui::SliderFloat("Rotate speed", &autoRotateSpeed_, -120.0f, 120.0f, "%.1f deg/s");

        ImGui::Separator();
        ImGui::Text("Esferas: %d a orbitar", (int)spheres_.size());

        if (floorReflectRt_ && floorReflectRt_->colorTex())
{
    ImGui::Text("Floor Reflection RT");
    ImGui::Image((ImTextureID)(intptr_t)floorReflectRt_->colorTex()->id,
                 ImVec2(200, 200),
                 ImVec2(0, 1),
                 ImVec2(1, 0));
}


        ImGui::SeparatorText("Tips");
        ImGui::BulletText("Arrow Left/Right rotates the car");

        if (changed)
            applyMaterialParams();

        ImGui::End();
    }

    void registerReflectionMaterial(Material *mat)
    {
        if (!mat)
            return;

        if (std::find(reflectionMaterials_.begin(), reflectionMaterials_.end(), mat) ==
            reflectionMaterials_.end())
        {
            reflectionMaterials_.push_back(mat);
        }
    }

    void setReflectionCaptureMode(bool enabled)
    {
        const int value = enabled ? 1 : 0;
        for (Material *mat : reflectionMaterials_)
        {
            if (mat)
                mat->setInt("u_captureMode", value);
        }
    }

    void updateFloorPlanarCenter()
    {
        if (!floorMaterial_)
            return;

        glm::vec3 center(0.0f);
        if (carNode_)
            center = glm::vec3(carNode_->worldMatrix()[3]);
        floorMaterial_->setVec3("u_planarCenter", center);
    }

private:
    Mesh *carMesh_ = nullptr;
    Mesh *floorMesh_ = nullptr;
    Mesh *sphereMesh_ = nullptr;  // partilhado por todas as esferas

    struct SphereOrbit
    {
        MeshNode *node   = nullptr;
        Material *mat    = nullptr;
        float     radius = 2.0f;
        float     height = 1.0f;
        float     speed  = 1.0f;
        float     angle  = 0.0f;
    };

    MeshNode *carNode_ = nullptr;
    MeshNode *floorNode_ = nullptr;
    MeshNode *skyboxNode_ = nullptr;
    std::vector<SphereOrbit> spheres_;

    Material *carMaterial_ = nullptr;
    Material *floorMaterial_ = nullptr;
    Material *skyboxMaterial_ = nullptr;
    std::vector<Material *> reflectionMaterials_;

    // Render-to-Cubemap — probe do CARRO (centro do carro)
    RenderTarget *probeRt_         = nullptr;
    RenderTarget *floorReflectRt_  = nullptr;
    GLuint        probeCubeFbo_    = 0;
    Texture      *probeDynCubeTex_ = nullptr;
    Camera       *probeCam_        = nullptr;
    Camera       *floorReflectCam_ = nullptr;
    int           probeFaceIndex_  = 0;

    Texture *envCubemap_ = nullptr;

    Shader *reflectShader_ = nullptr;
    Shader *skyboxShader_ = nullptr;
    ForwardTechnique *technique_ = nullptr;

    std::string loadedModelPath_;
    std::string loadedCubemapName_;

    float carReflectivity_ = 0.72f;
    float carRoughness_ = 0.18f;
    float floorReflectivity_ = 0.42f;
    float floorRoughness_ = 0.30f;
    float fresnelPower_ = 4.0f;
    float exposure_ = 1.0f;
    float skyExposure_ = 1.0f;
    float floorPlanarRadius_ = 4.2f;
    float floorPlanarSoftness_ = 2.8f;
    float floorPlanarViewFalloff_ = 0.85f;
    int debugPlanarMode_ = 0;

    bool autoRotate_ = true;
    float autoRotateSpeed_ = 25.0f;
};
