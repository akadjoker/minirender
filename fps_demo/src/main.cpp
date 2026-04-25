#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "Camera.hpp"
#include "Core.hpp"
#include "Device.hpp"
#include "GenesisBspCollider.hpp"
#include "FpsPlayerController.hpp"
#include "GenesisBspLoader.hpp"
#include "Input.hpp"
#include "Manager.hpp"
#include "Batch.hpp"
#include "genesis/GenesisGbspFile.hpp"
#include "genesis/GenesisMoverSystem.hpp"
#include "genesis/MiniGenesis.hpp"
#include "genesis/GenesisEntities.hpp"
#include "genesis/GenesisPortalSystem.hpp"
#include "genesis/GenesisUtils.hpp"
#include "imgui.h"

namespace
{
struct FpsSceneState
{
    std::string loadedPath;
    std::string textureDirectory;
    std::string status;
    std::string error;

    Mesh *mapMesh = nullptr;
    MeshNode *mapNode = nullptr;
    std::vector<glm::vec3> playerStartsBase;
    std::vector<glm::vec3> playerStartForwardsBase;
    std::vector<glm::vec3> playerStarts;
    std::vector<glm::vec3> playerStartForwards;
    BoundingBox bounds = {};
    mini_genesis::GenesisPortalSystem portalSystem;
    mini_genesis::GenesisMoverSystem moverSystem;
    bool enableCollision = true;
    bool collisionTestMode = false;
    bool drawCollisionDebug = true;
    bool drawEntityDebug = true;
    bool swapEntityYZ = false;
    int selectedPlayerStart = 0;
    std::array<float, 3> teleportPos = {0.0f, 0.0f, 0.0f};
    bool teleportPosInitialized = false;
    float collisionPlaneDrawSize = 34.0f;
};

void drawMarker(RenderBatch &batch, const glm::vec3 &p, float s)
{
    batch.Line3D(p + glm::vec3(-s, 0.0f, 0.0f), p + glm::vec3(s, 0.0f, 0.0f));
    batch.Line3D(p + glm::vec3(0.0f, -s, 0.0f), p + glm::vec3(0.0f, s, 0.0f));
    batch.Line3D(p + glm::vec3(0.0f, 0.0f, -s), p + glm::vec3(0.0f, 0.0f, s));
}

void drawPlanePatch(RenderBatch &batch,
                    const glm::vec3 &point,
                    const glm::vec3 &normal,
                    float halfSize)
{
    if (glm::length2(normal) <= 1e-8f)
        return;

    glm::vec3 n = glm::normalize(normal);
    const glm::vec3 p = point + n * 0.5f; // avoid z-fighting with world surface
    glm::vec3 ref = (std::abs(glm::dot(n, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f)
                        ? glm::vec3(1.0f, 0.0f, 0.0f)
                        : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 t0 = glm::normalize(glm::cross(ref, n));
    glm::vec3 t1 = glm::normalize(glm::cross(n, t0));

    const glm::vec3 a = p + t0 * halfSize + t1 * halfSize;
    const glm::vec3 b = p - t0 * halfSize + t1 * halfSize;
    const glm::vec3 c = p - t0 * halfSize - t1 * halfSize;
    const glm::vec3 d = p + t0 * halfSize - t1 * halfSize;

    batch.TriangleLines(a, b, c);
    batch.TriangleLines(a, c, d);
}

void drawCollisionDebug(RenderBatch &batch,
                        const FpsPlayerController &controller,
                        float planeHalfSize)
{
    const GenesisTraceResult &moveTrace = controller.lastMoveTrace;
    const GenesisTraceResult &slideTrace = controller.lastSlideTrace;
    const GenesisTraceResult &groundTrace = controller.lastGroundTrace;

    batch.SetColor(100, 200, 255, 220);
    batch.Line3D(moveTrace.start, moveTrace.end);

    if (moveTrace.hit)
    {
        batch.SetColor(40, 255, 90, 240);
        batch.Line3D(moveTrace.start, moveTrace.endPos);
    }

    if (slideTrace.hit && slideTrace.planeIndex >= 0)
    {
        batch.SetColor(255, 70, 60, 255);
        batch.Line3D(slideTrace.endPos, slideTrace.endPos + slideTrace.planeNormal * 24.0f);

        batch.SetColor(255, 220, 70, 180);
        drawPlanePatch(batch, slideTrace.endPos, slideTrace.planeNormal, planeHalfSize);
    }
    else if (moveTrace.hit && moveTrace.planeIndex >= 0)
    {
        batch.SetColor(255, 90, 40, 255);
        batch.Line3D(moveTrace.endPos, moveTrace.endPos + moveTrace.planeNormal * 24.0f);

        batch.SetColor(255, 200, 70, 180);
        drawPlanePatch(batch, moveTrace.endPos, moveTrace.planeNormal, planeHalfSize);
    }

    if (groundTrace.hit)
    {
        batch.SetColor(255, 170, 40, 240);
        batch.Line3D(groundTrace.start, groundTrace.endPos);
    }

    BoundingBox playerBox;
    playerBox.expand(controller.position - glm::vec3(controller.radius));
    playerBox.expand(controller.position + glm::vec3(controller.radius));
    batch.SetColor(80, 160, 255, 180);
    batch.Box(playerBox);
}

void drawEntityDebug(RenderBatch &batch, const FpsSceneState &state)
{
    for (size_t i = 0; i < state.playerStarts.size(); ++i)
    {
        const glm::vec3 p = state.playerStarts[i];
        const bool selected = (static_cast<int>(i) == state.selectedPlayerStart);
        batch.SetColor(selected ? 40 : 0, selected ? 255 : 190, 255, 255);
        drawMarker(batch, p, selected ? 12.0f : 9.0f);

        if (i < state.playerStartForwards.size() && glm::length2(state.playerStartForwards[i]) > 1e-8f)
        {
            const glm::vec3 f = glm::normalize(state.playerStartForwards[i]);
            batch.Line3D(p, p + f * 28.0f);
        }
    }

    for (const mini_genesis::GenesisMover &m : state.moverSystem.movers())
    {
        const glm::vec3 p0 = m.origin;
        const glm::vec3 dir = glm::length2(m.moveDir) > 1e-8f ? glm::normalize(m.moveDir) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 p1 = p0 + dir * m.travel;

        batch.SetColor(255, 150, 30, 255);
        drawMarker(batch, p0, 10.0f);
        batch.Line3D(p0, p1);
        batch.SetColor(255, 220, 60, 220);
        drawMarker(batch, p1, 6.0f);
    }

    for (const mini_genesis::GenesisTrigger &t : state.moverSystem.triggers())
    {
        batch.SetColor(180, 80, 255, 220);
        drawMarker(batch, t.origin, 7.0f);
        batch.CircleXZ(t.origin, t.radius, 28);
    }
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool hasExtension(const std::string &path, const std::string &ext)
{
    const std::string lower = toLower(path);
    const std::string lowerExt = toLower(ext);
    return lower.size() >= lowerExt.size() &&
           lower.compare(lower.size() - lowerExt.size(), lowerExt.size(), lowerExt) == 0;
}

Shader *createFpsShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec4 tangent;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;
        out vec2 v_uv;
        out vec2 v_lightmapUv;

        void main()
        {
            v_normal = normalize(u_normalMatrix * normal);
            v_uv = uv;
            v_lightmapUv = tangent.xy;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        in vec2 v_lightmapUv;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform sampler2D u_lightmap;
        uniform int u_hasAlbedo;
        uniform int u_hasLightmap;
        uniform float u_lightmapFactor;
        uniform vec3 u_lightDir;
        uniform vec3 u_ambient;

        void main()
        {
            vec3 albedo = u_color.rgb;
            if (u_hasAlbedo > 0)
                albedo *= texture(u_albedo, v_uv).rgb;

            vec3 lm = vec3(1.0);
            if (u_hasLightmap > 0)
                lm = texture(u_lightmap, v_lightmapUv).rgb;

            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec3 dynamicLit = albedo * (u_ambient + vec3(0.85 * diff));

            float lmFactor = clamp(u_lightmapFactor, 0.0, 2.0);
            vec3 finalColor = (u_hasLightmap > 0)
                ? albedo * mix(vec3(1.0), lm, lmFactor)
                : dynamicLit;

            FragColor = vec4(finalColor, u_color.a);
        });

    return ShaderManager::instance().loadFromSource("fps_demo_shader", vert, frag);
}

glm::vec3 fallbackSpawnFromBounds(const BoundingBox &bounds)
{
    if (!bounds.is_valid())
        return glm::vec3(0.0f, 64.0f, 0.0f);

    glm::vec3 spawn = bounds.center();
    spawn.y = bounds.max.y + 64.0f;
    return spawn;
}

void rebuildEntityStartsForAxisMode(FpsSceneState &state)
{
    state.playerStarts = state.playerStartsBase;
    state.playerStartForwards = state.playerStartForwardsBase;

    if (!state.swapEntityYZ)
        return;

    for (glm::vec3 &p : state.playerStarts)
        p = mini_genesis::genesisPointToEngine(p);
    for (glm::vec3 &f : state.playerStartForwards)
        f = mini_genesis::genesisDirToEngine(f);

    if (state.playerStarts.empty())
        state.selectedPlayerStart = 0;
    else
        state.selectedPlayerStart = std::clamp(state.selectedPlayerStart, 0, static_cast<int>(state.playerStarts.size()) - 1);
}

glm::vec3 currentForwardFlat(const FpsPlayerController &controller)
{
    const float yawRadians = glm::radians(controller.yawDegrees);
    glm::vec3 forward(-std::sin(yawRadians), 0.0f, -std::cos(yawRadians));
    if (glm::length2(forward) <= 1e-8f)
        return glm::vec3(0.0f, 0.0f, -1.0f);
    return glm::normalize(forward);
}

void teleportToPlayerStart(FpsSceneState &state,
                           FpsPlayerController &controller,
                           Camera *camera)
{
    if (state.playerStarts.empty())
        return;

    state.selectedPlayerStart = std::clamp(state.selectedPlayerStart, 0, static_cast<int>(state.playerStarts.size()) - 1);
    glm::vec3 forward = currentForwardFlat(controller);
    if (state.selectedPlayerStart < static_cast<int>(state.playerStartForwards.size()) &&
        glm::length2(state.playerStartForwards[static_cast<size_t>(state.selectedPlayerStart)]) > 1e-8f)
    {
        forward = state.playerStartForwards[static_cast<size_t>(state.selectedPlayerStart)];
    }
    controller.setSpawn(camera, state.playerStarts[static_cast<size_t>(state.selectedPlayerStart)], forward);
}

bool loadGenesisBspLevel(FpsSceneState &state,
                         Scene &scene,
                         GenesisBspCollider &collider,
                         FpsPlayerController &controller,
                         Camera *camera,
                         const std::string &path,
                         GenesisBspLoader &loader)
{
    if (!state.mapMesh)
        state.mapMesh = MeshManager::instance().create("fps_demo_map_mesh");
    if (!state.mapNode)
    {
        state.mapNode = scene.createMeshNode("fps_map_node", state.mapMesh);
        state.mapNode->renderType = RenderType::Solid;
    }

    GenesisLoadResult result;
    if (!loader.load(path, *state.mapMesh, collider, result))
    {
        state.error = result.error;
        state.status.clear();
        state.playerStarts.clear();
        state.playerStartForwards.clear();
        state.playerStartsBase.clear();
        state.playerStartForwardsBase.clear();
        state.mapNode->visible = false;
        return false;
    }

    state.bounds = result.bounds;
    state.loadedPath = path;
    state.textureDirectory.clear();
    state.status = result.status;
    state.error = result.error;
    state.enableCollision = true;
    state.playerStartsBase = result.playerStarts;
    state.playerStartForwardsBase = result.playerStartForwards;
    rebuildEntityStartsForAxisMode(state);
    state.mapNode->mesh = state.mapMesh;
    state.mapNode->visible = true;
    state.selectedPlayerStart = 0;

    mini_genesis::GenesisGbspFile gbspFile;
    mini_genesis::GbspData gbspData;
    std::string gbspError;
    if (gbspFile.load(path, gbspData, gbspError))
    {
        state.portalSystem.buildFromGbsp(gbspData);
        state.moverSystem.buildFromBspEntities(gbspData.entities);
    }
    else
    {
        state.portalSystem.clear();
        state.moverSystem.clear();
    }

    glm::vec3 spawn = fallbackSpawnFromBounds(state.bounds);
    glm::vec3 forward(0.0f, 0.0f, -1.0f);
    if (!state.playerStarts.empty())
    {
        spawn = state.playerStarts[0];
        if (!state.playerStartForwards.empty())
            forward = state.playerStartForwards[0];
    }
    controller.setSpawn(camera, spawn, forward);
    state.teleportPos = {spawn.x, spawn.y, spawn.z};
    state.teleportPosInitialized = true;
    return true;
}

bool loadCodeTestLevel(FpsSceneState &state,
                       Scene &scene,
                       GenesisBspCollider &collider,
                       FpsPlayerController &controller,
                       Camera *camera)
{
    if (!state.mapMesh)
        state.mapMesh = MeshManager::instance().create("fps_demo_map_mesh");
    if (!state.mapNode)
    {
        state.mapNode = scene.createMeshNode("fps_map_node", state.mapMesh);
        state.mapNode->renderType = RenderType::Solid;
    }

    Mesh &mesh = *state.mapMesh;
    mesh.release_materials();
    mesh.materials.clear();
    mesh.surfaces.clear();
    mesh.buffer.vertices.clear();
    mesh.buffer.indices.clear();

    auto addQuad = [&](const glm::vec3 &a,
                       const glm::vec3 &b,
                       const glm::vec3 &c,
                       const glm::vec3 &d,
                       const glm::vec3 &n,
                       float uvScale = 0.01f)
    {
        const uint32_t base = static_cast<uint32_t>(mesh.buffer.vertices.size());
        const std::array<glm::vec3, 4> p = {a, b, c, d};
        const std::array<glm::vec2, 4> uv = {
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(1.0f, 1.0f),
            glm::vec2(0.0f, 1.0f),
        };
        for (int i = 0; i < 4; ++i)
        {
            Vertex v{};
            v.position = p[i];
            v.normal = n;
            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            v.uv = uv[i] * uvScale * 100.0f;
            mesh.buffer.vertices.push_back(v);
        }

        mesh.buffer.indices.push_back(base + 0);
        mesh.buffer.indices.push_back(base + 1);
        mesh.buffer.indices.push_back(base + 2);
        mesh.buffer.indices.push_back(base + 0);
        mesh.buffer.indices.push_back(base + 2);
        mesh.buffer.indices.push_back(base + 3);
    };

    // Floor + 2 walls (easy collision/slide test corner).
    const float minX = -256.0f;
    const float maxX = 256.0f;
    const float minZ = -256.0f;
    const float maxZ = 256.0f;
    const float floorY = 0.0f;
    const float ceilY = 160.0f;

    addQuad(glm::vec3(minX, floorY, minZ),
            glm::vec3(maxX, floorY, minZ),
            glm::vec3(maxX, floorY, maxZ),
            glm::vec3(minX, floorY, maxZ),
            glm::vec3(0.0f, 1.0f, 0.0f));

    addQuad(glm::vec3(minX, floorY, minZ),
            glm::vec3(minX, ceilY, minZ),
            glm::vec3(minX, ceilY, maxZ),
            glm::vec3(minX, floorY, maxZ),
            glm::vec3(1.0f, 0.0f, 0.0f));

    addQuad(glm::vec3(minX, floorY, minZ),
            glm::vec3(maxX, floorY, minZ),
            glm::vec3(maxX, ceilY, minZ),
            glm::vec3(minX, ceilY, minZ),
            glm::vec3(0.0f, 0.0f, 1.0f));

    Material *mat = new Material();
    mat->name = "code_test_mat";
    mat->setCullFace(false);
    mat->setVec4("u_color", glm::vec4(0.85f, 0.88f, 0.93f, 1.0f));
    mat->setTexture("u_albedo", TextureManager::instance().getWhite());
    mat->setInt("u_hasAlbedo", 0);
    mat->setInt("u_hasLightmap", 0);
    const int matIndex = mesh.add_material(mat);
    mesh.add_surface(0, static_cast<uint32_t>(mesh.buffer.indices.size()), matIndex);
    mesh.upload();

    std::vector<GenesisBspPlane> planes;
    planes.push_back({glm::vec3(1.0f, 0.0f, 0.0f), minX});   // x >= minX
    planes.push_back({glm::vec3(1.0f, 0.0f, 0.0f), maxX});   // x <= maxX
    planes.push_back({glm::vec3(0.0f, 0.0f, 1.0f), minZ});   // z >= minZ
    planes.push_back({glm::vec3(0.0f, 0.0f, 1.0f), maxZ});   // z <= maxZ
    planes.push_back({glm::vec3(0.0f, 1.0f, 0.0f), floorY}); // y >= floorY
    planes.push_back({glm::vec3(0.0f, 1.0f, 0.0f), ceilY});  // y <= ceilY

    std::vector<GenesisBspBNode> bnodes(6);
    bnodes[0].planeNum = 0; bnodes[0].children[0] = 1;  bnodes[0].children[1] = -1;
    bnodes[1].planeNum = 1; bnodes[1].children[0] = -1; bnodes[1].children[1] = 2;
    bnodes[2].planeNum = 2; bnodes[2].children[0] = 3;  bnodes[2].children[1] = -1;
    bnodes[3].planeNum = 3; bnodes[3].children[0] = -1; bnodes[3].children[1] = 4;
    bnodes[4].planeNum = 4; bnodes[4].children[0] = 5;  bnodes[4].children[1] = -1;
    bnodes[5].planeNum = 5; bnodes[5].children[0] = -1; bnodes[5].children[1] = -2;

    collider.setTree(std::move(bnodes), std::move(planes), 0);

    state.loadedPath = "(code test room)";
    state.textureDirectory.clear();
    state.status = "Sala de teste por codigo carregada.";
    state.error.clear();
    state.enableCollision = true;
    state.mapNode->mesh = state.mapMesh;
    state.mapNode->visible = true;
    state.bounds = mesh.aabb;
    state.playerStartsBase = {glm::vec3(0.0f, 40.0f, 0.0f)};
    state.playerStartForwardsBase = {glm::vec3(0.0f, 0.0f, -1.0f)};
    rebuildEntityStartsForAxisMode(state);
    state.selectedPlayerStart = 0;
    state.portalSystem.clear();
    state.moverSystem.clear();

    controller.radius = 18.0f;
    controller.eyeOffset = 34.0f;
    controller.useGravity = false;
    controller.useJump = false;
    controller.verticalSpeed = 0.0f;
    controller.setSpawn(camera, state.playerStarts[0], state.playerStartForwards[0]);

    // Ensure test spawn is not inside solid after any future collider tweaks.
    if (state.enableCollision && collider.hasTree())
    {
        const glm::vec3 mins(-controller.radius, -controller.radius, -controller.radius);
        const glm::vec3 maxs(controller.radius, controller.radius, controller.radius);
        GenesisTraceResult probe;
        collider.traceBoxDetailed(controller.position, controller.position, mins, maxs, probe);
        if (probe.startSolid)
        {
            for (int i = 1; i <= 48; ++i)
            {
                const glm::vec3 candidate = state.playerStarts[0] + glm::vec3(0.0f, i * 4.0f, 0.0f);
                collider.traceBoxDetailed(candidate, candidate, mins, maxs, probe);
                if (!probe.startSolid)
                {
                    controller.setSpawn(camera, candidate, state.playerStartForwards[0]);
                    break;
                }
            }
        }
    }

    state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
    state.teleportPosInitialized = true;
    return true;
}

bool loadAnyLevel(FpsSceneState &state,
                  Scene &scene,
                  GenesisBspCollider &collider,
                  FpsPlayerController &controller,
                  Camera *camera,
                  const std::string &path,
                  const std::string &textureDirectory,
                  GenesisBspLoader &loader,
                  mini_genesis::MiniGenesis &miniGenesis)
{
    (void)textureDirectory;
    if (hasExtension(path, ".bsp") || hasExtension(path, ".gbsp"))
    {
        return loadGenesisBspLevel(state, scene, collider, controller, camera, path, loader);
    }

    if (hasExtension(path, ".3dt"))
    {
        if (!state.mapMesh)
            state.mapMesh = MeshManager::instance().create("fps_demo_map_mesh");
        if (!state.mapNode)
        {
            state.mapNode = scene.createMeshNode("fps_map_node", state.mapMesh);
            state.mapNode->renderType = RenderType::Solid;
        }

        mini_genesis::Map3dtData map;
        std::string error;
        if (!miniGenesis.load3dt(path, map, error))
        {
            state.error = "falha ao ler 3dt: " + error;
            state.status.clear();
            state.playerStarts.clear();
            state.playerStartForwards.clear();
            state.playerStartsBase.clear();
            state.playerStartForwardsBase.clear();
            state.mapNode->visible = false;
            collider.clear();
            return false;
        }

        if (!miniGenesis.buildBrushMesh(map, *state.mapMesh, error))
        {
            state.error = "falha a gerar mesh 3dt: " + error;
            state.status.clear();
            state.playerStarts.clear();
            state.playerStartForwards.clear();
            state.playerStartsBase.clear();
            state.playerStartForwardsBase.clear();
            state.mapNode->visible = false;
            collider.clear();
            return false;
        }

        std::vector<mini_genesis::PlayerStart> starts;
        mini_genesis::GenesisEntities::extractPlayerStarts(map.entities, starts);

        state.playerStartsBase.clear();
        state.playerStartForwardsBase.clear();
        for (const mini_genesis::PlayerStart &s : starts)
        {
            state.playerStartsBase.push_back(s.position);
            state.playerStartForwardsBase.push_back(s.forward);
        }
        rebuildEntityStartsForAxisMode(state);

        state.bounds = state.mapMesh->aabb;
        state.loadedPath = path;
        state.textureDirectory.clear();
        state.status = "3DT carregado (brushes/entities Genesis convertidos).";
        state.error.clear();
        state.mapNode->mesh = state.mapMesh;
        state.mapNode->visible = true;
        state.selectedPlayerStart = 0;
        collider.clear();
        state.portalSystem.clear();
        state.moverSystem.buildFrom3dtEntities(map.entities);

        glm::vec3 spawn = fallbackSpawnFromBounds(state.bounds);
        glm::vec3 forward(0.0f, 0.0f, -1.0f);
        if (!state.playerStarts.empty())
        {
            spawn = state.playerStarts[0];
            if (!state.playerStartForwards.empty())
                forward = state.playerStartForwards[0];
        }
        controller.setSpawn(camera, spawn, forward);
        state.teleportPos = {spawn.x, spawn.y, spawn.z};
        state.teleportPosInitialized = true;
        return true;
    }

    if (hasExtension(path, ".map") || hasExtension(path, ".mrlvl"))
    {
        state.error = "Modo mini Genesis suporta .bsp/.gbsp/.3dt.";
        state.status.clear();
        return false;
    }

    state.error = "Formato nao suportado no modo Genesis.";
    state.status.clear();
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    const std::string startPath = (argc > 1) ? argv[1] : "study/mapas/genvs.bsp";
    const std::string startTextureDir = (argc > 2) ? argv[2] : "study/Game/levels/genvs_textures";

    Device &device = Device::Instance();
    if (!device.Create(1600, 900, "MiniRender FPS Demo", true))
        return 1;

    device.ImGuiInit();

    Scene scene;
    GenesisBspCollider collider;

    Camera *camera = scene.createCamera("fps_camera");
    camera->setViewport(0, 0, device.GetWidth(), device.GetHeight());
    camera->setViewPlanes(1.0f, 12000.0f);
    camera->setFov(75.0f);
    scene.setCamera(camera);

    FpsPlayerController controller;
    GenesisBspLoader bspLoader;
    mini_genesis::MiniGenesis miniGenesis;
    RenderBatch debugBatch;
    debugBatch.Init(1, 4096);

    Shader *shader = createFpsShader();
    if (!shader)
    {
        device.Close();
        return 1;
    }
    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.23f, 0.24f, 0.26f));
    shader->setFloat("u_lightmapFactor", 1.0f);

    FpsSceneState state;
    state.mapMesh = MeshManager::instance().create("fps_demo_map_mesh");
    state.mapNode = scene.createMeshNode("fps_map_node", state.mapMesh);
    state.mapNode->renderType = RenderType::Solid;

    std::array<char, 512> levelPathBuffer = {};
    std::array<char, 512> textureDirBuffer = {};
    std::snprintf(levelPathBuffer.data(), levelPathBuffer.size(), "%s", startPath.c_str());
    std::snprintf(textureDirBuffer.data(), textureDirBuffer.size(), "%s", startTextureDir.c_str());

    loadAnyLevel(state, scene, collider, controller, camera, levelPathBuffer.data(), textureDirBuffer.data(), bspLoader, miniGenesis);
    state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
    state.teleportPosInitialized = true;

    while (device.Run())
    {
        const float dt = device.GetFrameTime();

        if (device.IsResize())
            camera->setViewport(0, 0, device.GetWidth(), device.GetHeight());

        if (Input::IsKeyPressed(KEY_ESCAPE))
        {
            const bool captured = SDL_GetRelativeMouseMode() == SDL_TRUE;
            SDL_SetRelativeMouseMode(captured ? SDL_FALSE : SDL_TRUE);
        }

        const bool allowMouseLook =
            !ImGui::GetIO().WantCaptureMouse && Input::IsMouseDown(MouseButton::LEFT);

        if (state.collisionTestMode)
        {
            controller.useGravity = false;
            controller.useJump = false;
            controller.verticalSpeed = 0.0f;
        }

        static GenesisBspCollider noCollisionCollider;
        const GenesisBspCollider &activeCollider = state.enableCollision ? collider : noCollisionCollider;
        controller.update(camera, activeCollider, dt, allowMouseLook);
        state.portalSystem.update(controller.position);
        state.moverSystem.update(dt, controller.position);
        scene.update(dt);

        device.ImGuiBegin();

        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Once);
        if (ImGui::Begin("FPS Demo"))
        {
            ImGui::InputText("Level", levelPathBuffer.data(), levelPathBuffer.size());
            ImGui::InputText("Textures", textureDirBuffer.data(), textureDirBuffer.size());

            if (ImGui::Button("Reload"))
            {
                loadAnyLevel(state,
                             scene,
                             collider,
                             controller,
                             camera,
                             levelPathBuffer.data(),
                             textureDirBuffer.data(),
                             bspLoader,
                             miniGenesis);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Code Test Room"))
            {
                loadCodeTestLevel(state, scene, collider, controller, camera);
            }

            ImGui::SameLine();
            if (ImGui::Button("Respawn"))
            {
                glm::vec3 spawn = fallbackSpawnFromBounds(state.bounds);
                glm::vec3 forward(0.0f, 0.0f, -1.0f);
                if (!state.playerStarts.empty())
                {
                    spawn = state.playerStarts[0];
                    if (!state.playerStartForwards.empty())
                        forward = state.playerStartForwards[0];
                }
                controller.setSpawn(camera, spawn, forward);
                state.teleportPos = {spawn.x, spawn.y, spawn.z};
                state.teleportPosInitialized = true;
            }

            if (!state.status.empty())
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "%s", state.status.c_str());
            if (!state.error.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", state.error.c_str());

            ImGui::Separator();
            ImGui::Text("Loaded: %s", state.loadedPath.empty() ? "(none)" : state.loadedPath.c_str());
            ImGui::Text("BSP planes (collision): %d", collider.planeCount());
            ImGui::Text("Player pos: %.1f %.1f %.1f",
                        controller.position.x,
                        controller.position.y,
                        controller.position.z);
            ImGui::Text("Grounded: %s", controller.grounded ? "yes" : "no");
            ImGui::Checkbox("Enable Collision", &state.enableCollision);
            if (state.enableCollision && collider.hasTree())
            {
                const glm::vec3 mins(-controller.radius, -controller.radius, -controller.radius);
                const glm::vec3 maxs(controller.radius, controller.radius, controller.radius);
                GenesisTraceResult probe;
                collider.traceBoxDetailed(controller.position, controller.position, mins, maxs, probe);
                ImGui::Text("Probe: startSolid=%d allSolid=%d hit=%d plane=%d",
                            probe.startSolid ? 1 : 0,
                            probe.allSolid ? 1 : 0,
                            probe.hit ? 1 : 0,
                            probe.planeIndex);
                if (ImGui::Button("Force Unstuck"))
                {
                    const glm::vec3 upPos = controller.position + glm::vec3(0.0f, 32.0f, 0.0f);
                    controller.setSpawn(camera, upPos, currentForwardFlat(controller));
                    state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
                    state.teleportPosInitialized = true;
                }
            }
            ImGui::Text("Player starts: %d", static_cast<int>(state.playerStarts.size()));
            if (!state.playerStarts.empty())
            {
                ImGui::SliderInt("Start Index", &state.selectedPlayerStart, 0, static_cast<int>(state.playerStarts.size()) - 1);
                if (ImGui::Button("Prev Start"))
                {
                    state.selectedPlayerStart =
                        (state.selectedPlayerStart <= 0) ? static_cast<int>(state.playerStarts.size()) - 1 : state.selectedPlayerStart - 1;
                    teleportToPlayerStart(state, controller, camera);
                    state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
                    state.teleportPosInitialized = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Next Start"))
                {
                    state.selectedPlayerStart =
                        (state.selectedPlayerStart + 1) % static_cast<int>(state.playerStarts.size());
                    teleportToPlayerStart(state, controller, camera);
                    state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
                    state.teleportPosInitialized = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Go Start"))
                {
                    teleportToPlayerStart(state, controller, camera);
                    state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
                    state.teleportPosInitialized = true;
                }
            }

            if (!state.teleportPosInitialized)
            {
                state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
                state.teleportPosInitialized = true;
            }
            ImGui::InputFloat3("Teleport Pos", state.teleportPos.data(), "%.1f");
            if (ImGui::Button("Use Current Pos"))
                state.teleportPos = {controller.position.x, controller.position.y, controller.position.z};
            ImGui::SameLine();
            if (ImGui::Button("Teleport Camera"))
            {
                const glm::vec3 manualPos(state.teleportPos[0], state.teleportPos[1], state.teleportPos[2]);
                controller.setSpawn(camera, manualPos, currentForwardFlat(controller));
            }
            ImGui::SliderFloat("Collision Radius", &controller.radius, 8.0f, 48.0f, "%.1f");
            ImGui::SliderFloat("Eye Offset", &controller.eyeOffset, 12.0f, 72.0f, "%.1f");
            if (ImGui::Checkbox("Swap Y/Z Entities", &state.swapEntityYZ))
            {
                rebuildEntityStartsForAxisMode(state);
                if (!state.playerStarts.empty())
                    teleportToPlayerStart(state, controller, camera);
            }
            ImGui::Checkbox("Collision Test Mode (No Gravity/Jump)", &state.collisionTestMode);
            ImGui::Checkbox("Draw Collision Debug", &state.drawCollisionDebug);
            ImGui::Checkbox("Draw Entity Debug", &state.drawEntityDebug);
            ImGui::SliderFloat("Debug Plane Size", &state.collisionPlaneDrawSize, 8.0f, 128.0f, "%.1f");
            ImGui::Checkbox("Use Gravity", &controller.useGravity);
            ImGui::Checkbox("Use Jump", &controller.useJump);
            ImGui::Separator();
            ImGui::Text("Move trace: hit=%d startSolid=%d allSolid=%d frac=%.3f visits=%d plane=%d",
                        controller.lastMoveTrace.hit ? 1 : 0,
                        controller.lastMoveTrace.startSolid ? 1 : 0,
                        controller.lastMoveTrace.allSolid ? 1 : 0,
                        controller.lastMoveTrace.fraction,
                        controller.lastMoveTrace.nodeVisits,
                        controller.lastMoveTrace.planeIndex);
            ImGui::Text("Move endPos: %.1f %.1f %.1f",
                        controller.lastMoveTrace.endPos.x,
                        controller.lastMoveTrace.endPos.y,
                        controller.lastMoveTrace.endPos.z);
            ImGui::Text("Slide trace: hit=%d frac=%.3f plane=%d endPos=(%.1f %.1f %.1f)",
                        controller.lastSlideTrace.hit ? 1 : 0,
                        controller.lastSlideTrace.fraction,
                        controller.lastSlideTrace.planeIndex,
                        controller.lastSlideTrace.endPos.x,
                        controller.lastSlideTrace.endPos.y,
                        controller.lastSlideTrace.endPos.z);
            ImGui::Text("Ground trace: hit=%d startSolid=%d allSolid=%d frac=%.3f plane=%d",
                        controller.lastGroundTrace.hit ? 1 : 0,
                        controller.lastGroundTrace.startSolid ? 1 : 0,
                        controller.lastGroundTrace.allSolid ? 1 : 0,
                        controller.lastGroundTrace.fraction,
                        controller.lastGroundTrace.planeIndex);
            const mini_genesis::PortalDebugState &pdbg = state.portalSystem.debug();
            int activeMovers = 0;
            for (const mini_genesis::GenesisMover &m : state.moverSystem.movers())
            {
                if (m.moving)
                    activeMovers++;
            }
            ImGui::Text("Portals: leaf=%d cluster=%d reachable=%d leafs=%d portals=%d",
                        pdbg.currentLeaf,
                        pdbg.currentCluster,
                        pdbg.reachableLeafs,
                        pdbg.leafCount,
                        pdbg.portalCount);
            ImGui::Text("Movers: total=%d active=%d triggers=%d",
                        static_cast<int>(state.moverSystem.movers().size()),
                        activeMovers,
                        static_cast<int>(state.moverSystem.triggers().size()));
            ImGui::Text("FPS: %.0f", dt > 0.0f ? (1.0f / dt) : 0.0f);
            ImGui::Separator();
            ImGui::Text("WASD move, SHIFT sprint, SPACE jump");
            ImGui::Text("Free-fly: Q up, Z/E down (when gravity is off)");
            ImGui::Text("Hold Left Mouse for mouselook");
            ImGui::Text("ESC toggles relative mouse mode");
        }
        ImGui::End();

        scene.beginPass();
        scene.setShader(shader);
        shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
        shader->setVec3("u_ambient", glm::vec3(0.23f, 0.24f, 0.26f));
        shader->setFloat("u_lightmapFactor", 1.0f);
        shader->setInt("u_hasLightmap", 0);
        shader->setInt("u_hasAlbedo", 0);
        shader->setVec4("u_color", glm::vec4(1.0f));
        scene.render(RenderType::Solid);

        if (state.drawCollisionDebug)
        {
            debugBatch.SetMatrix(camera->viewProjection);
            drawCollisionDebug(debugBatch, controller, state.collisionPlaneDrawSize);
            if (state.drawEntityDebug)
                drawEntityDebug(debugBatch, state);
            debugBatch.Render();
        }
        scene.endPass();

        device.ImGuiEnd();
        device.Flip();
    }

    device.Close();
    return 0;
}
