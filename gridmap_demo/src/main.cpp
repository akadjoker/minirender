#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "Core.hpp"
#include "Device.hpp"
#include "Pixmap.hpp"
#include "Utils.hpp"
#include "imgui.h"

extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libSDL2\n"
           "leak:SDL_DBus\n";
}
namespace
{
enum DemoTerrainSource
{
    TerrainFromGrid = 0,
    TerrainGenerated = 1,
};

bool pathExists(const std::string &path);
std::string joinPath(const std::string &base, const std::string &path);

bool ensureChunkTextures(const GridMapData &gridData,
                         const GridMapMeshBuildOptions &options,
                         std::string *error)
{
    if (options.textureDirectory.empty())
        return true;

    const int chunkSizeTiles = std::max(options.chunkSizeTiles, 1);
    const int chunkCountX = (gridData.width() + chunkSizeTiles - 1) / chunkSizeTiles;
    const int chunkCountY = (gridData.height() + chunkSizeTiles - 1) / chunkSizeTiles;

    const std::string baseImagePath = joinPath(options.textureDirectory, options.fallbackTextureName);
    if (!pathExists(baseImagePath))
        return true;

    bool missingAny = false;
    for (int cy = 0; cy < chunkCountY && !missingAny; ++cy)
    {
        for (int cx = 0; cx < chunkCountX; ++cx)
        {
            const std::string chunkPath = joinPath(options.textureDirectory,
                                                   options.textureBaseName + "_" +
                                                   std::to_string(cx) + "_x_" +
                                                   std::to_string(cy) + ".bmp");
            if (!pathExists(chunkPath))
            {
                missingAny = true;
                break;
            }
        }
    }

    if (!missingAny)
        return true;

    Pixmap baseImage;
    if (!baseImage.Load(baseImagePath.c_str()))
    {
        if (error)
            *error = "falhou a carregar a imagem base das texturas: " + baseImagePath;
        return false;
    }

    for (int cy = 0; cy < chunkCountY; ++cy)
    {
        for (int cx = 0; cx < chunkCountX; ++cx)
        {
            const std::string chunkPath = joinPath(options.textureDirectory,
                                                   options.textureBaseName + "_" +
                                                   std::to_string(cx) + "_x_" +
                                                   std::to_string(cy) + ".bmp");
            if (pathExists(chunkPath))
                continue;

            if (!baseImage.Save(chunkPath.c_str()))
            {
                if (error)
                    *error = "falhou a gravar texture de chunk: " + chunkPath;
                return false;
            }
        }
    }

    return true;
}

bool pathExists(const std::string &path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

bool directoryExists(const std::string &path)
{
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
        return false;
    return (info.st_mode & S_IFDIR) != 0;
}

std::string joinPath(const std::string &base, const std::string &path)
{
    if (base.empty() || base == ".")
        return path;
    if (!base.empty() && base.back() == '/')
        return base + path;
    return base + "/" + path;
}

std::string resolveProjectPath(const std::string &path, bool requireDirectory)
{
    if (path.empty())
        return std::string();

    const auto matches = [&](const std::string &candidate) -> bool
    {
        return requireDirectory ? directoryExists(candidate) : pathExists(candidate);
    };

    if (PathIsAbsolute(path) && matches(path))
        return path;

    const std::vector<std::string> relativeCandidates = {
        path,
        joinPath(".", path),
        joinPath("..", path),
        joinPath("../..", path),
    };

    for (const std::string &candidate : relativeCandidates)
    {
        if (matches(candidate))
            return candidate;
    }

    char *basePath = SDL_GetBasePath();
    if (!basePath)
        return path;

    const std::string executableBase(basePath);
    SDL_free(basePath);

    const std::vector<std::string> executableCandidates = {
        joinPath(executableBase, path),
        joinPath(executableBase, "../" + path),
        joinPath(executableBase, "../../" + path),
    };

    for (const std::string &candidate : executableCandidates)
    {
        if (matches(candidate))
            return candidate;
    }

    return path;
}

void copyStringToBuffer(std::array<char, 512> &buffer, const std::string &value)
{
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}

Shader *createStaticShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;
        out vec2 v_uv;

        void main()
        {
            v_normal = normalize(u_normalMatrix * normal);
            v_uv = uv;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform vec3 u_lightDir;
        uniform vec3 u_ambient;

        void main()
        {
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            FragColor = vec4(lit, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("gridmap_demo_static_shader", vert, frag);
}

void setupLighting(Shader *shader)
{
    if (!shader)
        return;
    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.55f, -1.0f, -0.35f)));
    shader->setVec3("u_ambient", glm::vec3(0.24f, 0.22f, 0.20f));
}

void resetCamera(Camera *camera, const BoundingBox &bounds)
{
    if (!camera || !bounds.is_valid())
        return;

    const glm::vec3 center = bounds.center();
    const glm::vec3 size = bounds.size();
    const float radius = glm::max(glm::length(size) * 0.5f, 32.0f);

    // Mantemos a câmara dentro da área horizontal do terreno para que a seleção
    // de chunks baseada na posição da câmara continue a apanhar chunks válidos.
    const glm::vec3 eye = center + glm::vec3(size.x * 0.18f,
                                             glm::max(size.y * 1.2f, radius * 0.35f),
                                             size.z * 0.18f);

    const float distance = glm::length(eye - center);
    const float nearPlane = glm::max(1.0f, distance / 2048.0f);
    const float farPlane = glm::max(8192.0f, distance + radius * 4.0f);
    camera->setViewPlanes(nearPlane, farPlane);
    camera->setPosition(eye);
    camera->lookAt(center);
}
}

int main()
{
    Device &device = Device::Instance();
    if (!device.Create(1600, 900, "MiniRender GridMap Demo", true))
        return 1;

    device.ImGuiInit();

    Scene scene;
    Camera *camera = scene.createFreeCamera("gridmap_camera",
                                            device.GetWidth(), device.GetHeight(),
                                            glm::vec3(300.0f, 260.0f, 300.0f),
                                            glm::vec3(0.0f, 0.0f, 0.0f),
                                            1400.0f, 0.16f, 4.0f);
    camera->setViewPlanes(1.0f, 16384.0f);
    scene.setCamera(camera);
    float cameraMoveSpeed = 1400.0f;
    if (auto *controller = dynamic_cast<FreeCameraController *>(camera->getController()))
        controller->moveSpeed = cameraMoveSpeed;

    Shader *staticShader = createStaticShader();
    if (!staticShader)
    {
        device.Close();
        return 1;
    }

    GridMapNode *terrainNode = new GridMapNode("gridmap_demo_node");
    scene.add(terrainNode);

    std::array<char, 512> gridPathBuffer = {};
    std::array<char, 512> textureDirBuffer = {};
    copyStringToBuffer(gridPathBuffer, "gdx/cardemo/terrain/cardemo.grid");
    copyStringToBuffer(textureDirBuffer, "gdx/cardemo/terrain/textures");

    GridMapData gridData;
    GridMapMeshBuildOptions buildOptions;
    buildOptions.chunkSizeTiles = 32;
    buildOptions.heightScale = 3.0f;
    buildOptions.renderRadiusChunks = 6;
    int terrainSource = TerrainGenerated;
    int generatedWidth = 512;
    int generatedHeight = 512;
    int generatedGranularity = 1;
    int generatedTileWidth = 256;
    int generatedSeed = 1337;
    float generatedMaxHeight = 220.0f;
    float generatedNoiseScale = 0.028f;

    RenderBatch debugBatch;
    debugBatch.Init();

    std::string statusMessage;
    std::string lastError;
    std::string resolvedGridPath;
    std::string resolvedTextureDir;
    BoundingBox terrainBounds;
    bool autoResetCamera = true;
    bool showBounds = true;
    bool showSurfaceBounds = false;
    bool showWorldGizmo = true;

    const auto reloadTerrain = [&](bool resetView) -> bool
    {
        resolvedGridPath = resolveProjectPath(gridPathBuffer.data(), false);
        resolvedTextureDir = resolveProjectPath(textureDirBuffer.data(), true);

        std::string error;
        if (terrainSource == TerrainFromGrid)
        {
            if (!gridData.load(resolvedGridPath, &error))
            {
                lastError = "Grid nao carregado: " + resolvedGridPath + " (" + error + ")";
                statusMessage.clear();
                terrainNode->visible = false;
                return false;
            }
        }
        else
        {
            if (!gridData.generateProcedural(generatedWidth, generatedHeight,
                                             generatedGranularity, generatedTileWidth,
                                             generatedSeed, generatedMaxHeight,
                                             generatedNoiseScale, &error))
            {
                lastError = "Terreno gerado falhou: " + error;
                statusMessage.clear();
                terrainNode->visible = false;
                return false;
            }
        }

        buildOptions.textureDirectory = resolvedTextureDir;
        if (!ensureChunkTextures(gridData, buildOptions, &error))
        {
            lastError = error;
            statusMessage.clear();
            terrainNode->visible = false;
            return false;
        }

        if (!terrainNode->load(gridData, buildOptions, &error))
        {
            lastError = error;
            statusMessage.clear();
            terrainNode->visible = false;
            return false;
        }

        terrainNode->visible = true;
        terrainBounds = terrainNode->getAABB();
        statusMessage = "Terreno carregado com sucesso.";
        lastError.clear();

        if (resetView || autoResetCamera)
            resetCamera(camera, terrainBounds);

        return true;
    };

    reloadTerrain(true);

    while (device.Run())
    {
        const float dt = device.GetFrameTime();
        scene.update(dt);

        device.ImGuiBegin();

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Once);
        if (ImGui::Begin("GridMap Demo"))
        {
            ImGui::InputText("Grid", gridPathBuffer.data(), gridPathBuffer.size());
            ImGui::InputText("Textures", textureDirBuffer.data(), textureDirBuffer.size());
            ImGui::Combo("Source", &terrainSource, "Grid File\0Generated Giant\0");
            ImGui::SliderInt("Chunk Size", &buildOptions.chunkSizeTiles, 8, 64);
            ImGui::SliderFloat("Height Scale", &buildOptions.heightScale, 0.25f, 8.0f, "%.2f");
            ImGui::SliderInt("Render Radius", &buildOptions.renderRadiusChunks, 1, 24);
            if (ImGui::SliderFloat("Camera Speed", &cameraMoveSpeed, 100.0f, 12000.0f, "%.0f"))
            {
                if (auto *controller = dynamic_cast<FreeCameraController *>(camera->getController()))
                    controller->moveSpeed = cameraMoveSpeed;
            }

            if (terrainSource == TerrainGenerated)
            {
                ImGui::SliderInt("Gen Width", &generatedWidth, 128, 2048);
                ImGui::SliderInt("Gen Height", &generatedHeight, 128, 2048);
                ImGui::SliderInt("Gen Granularity", &generatedGranularity, 1, 2);
                ImGui::SliderInt("Gen Tile Width", &generatedTileWidth, 64, 512);
                ImGui::InputInt("Gen Seed", &generatedSeed);
                ImGui::SliderFloat("Gen Max Height", &generatedMaxHeight, 32.0f, 512.0f, "%.1f");
                ImGui::SliderFloat("Gen Noise Scale", &generatedNoiseScale, 0.002f, 0.08f, "%.4f");
            }

            if (ImGui::Button("Reload Terrain"))
                reloadTerrain(false);
            ImGui::SameLine();
            if (ImGui::Button("Reset Camera"))
                resetCamera(camera, terrainBounds);

            ImGui::Checkbox("Auto Reset Camera", &autoResetCamera);
            ImGui::Checkbox("Show Bounds", &showBounds);
            ImGui::Checkbox("Show Surface Bounds", &showSurfaceBounds);
            ImGui::Checkbox("Show World Gizmo", &showWorldGizmo);

            if (!statusMessage.empty())
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "%s", statusMessage.c_str());
            if (!lastError.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", lastError.c_str());

            ImGui::TextWrapped("Resolved Grid: %s", resolvedGridPath.empty() ? "(none)" : resolvedGridPath.c_str());
            ImGui::TextWrapped("Resolved Textures: %s", resolvedTextureDir.empty() ? "(none)" : resolvedTextureDir.c_str());

            ImGui::Separator();
            ImGui::Text("Notice: %s", gridData.notice().empty() ? "(none)" : gridData.notice().c_str());
            ImGui::Text("Tiles: %d x %d", gridData.width(), gridData.height());
            ImGui::Text("Samples: %d x %d", gridData.sampleWidth(), gridData.sampleHeight());
            ImGui::Text("Granularity: %d", gridData.granularity());
            ImGui::Text("Tile Width: %d", gridData.tileWidth());
            ImGui::Text("Height Scale: %.2f", buildOptions.heightScale);
            ImGui::Text("Render Radius: %d", buildOptions.renderRadiusChunks);
            ImGui::Text("World Size: %.2f x %.2f", gridData.worldWidth(), gridData.worldDepth());
            ImGui::Text("Chunks: %d x %d", terrainNode->chunkCountX(), terrainNode->chunkCountY());
            ImGui::Text("Selected chunks: %d", terrainNode->selectedChunkCount());
            ImGui::Text("Visible chunks: %d", terrainNode->visibleChunkCount());

            if (terrainBounds.is_valid())
            {
                const glm::vec3 center = terrainBounds.center();
                const glm::vec3 size = terrainBounds.size();
                ImGui::Separator();
                ImGui::Text("Bounds Center: %.2f %.2f %.2f", center.x, center.y, center.z);
                ImGui::Text("Bounds Size: %.2f %.2f %.2f", size.x, size.y, size.z);
            }
        }
        ImGui::End();

        device.ImGuiEnd();

        scene.setCamera(camera);
        scene.beginPass();
        scene.setShader(staticShader);
        setupLighting(staticShader);
        scene.render(RenderType::Terrain);
        scene.endPass();

        debugBatch.SetMatrix(camera->viewProjection);
        Material::applyDefaultStates();

        if (showWorldGizmo)
        {
            debugBatch.SetColor(255, 64, 64, 255);
            debugBatch.Line3D(glm::vec3(-64.0f, 0.0f, 0.0f), glm::vec3(64.0f, 0.0f, 0.0f));
            debugBatch.SetColor(64, 255, 64, 255);
            debugBatch.Line3D(glm::vec3(0.0f, -64.0f, 0.0f), glm::vec3(0.0f, 64.0f, 0.0f));
            debugBatch.SetColor(64, 160, 255, 255);
            debugBatch.Line3D(glm::vec3(0.0f, 0.0f, -64.0f), glm::vec3(0.0f, 0.0f, 64.0f));
        }

        if (showBounds && terrainBounds.is_valid())
        {
            debugBatch.SetColor(255, 220, 64, 255);
            debugBatch.Box(terrainBounds);
        }

        if (showSurfaceBounds)
            scene.debug(&debugBatch);

        debugBatch.Render();
        device.Flip();
    }

    scene.clear();
    device.Close();
    return 0;
}
