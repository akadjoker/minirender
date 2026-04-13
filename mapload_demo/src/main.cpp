#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "Core.hpp"
#include "Device.hpp"
#include "Input.hpp"
#include "MapClipper.hpp"
#include "Utils.hpp"
#include "imgui.h"

extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libSDL2\n"
           "leak:SDL_DBus\n";
}

namespace
{
struct DoorVisual
{
    std::string meshName;
    Mesh *mesh = nullptr;
    MeshNode *node = nullptr;
    MapClipper clipper;
    BoundingBox bounds = {};
    glm::vec3 moveDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    float travelDistance = 0.0f;
    float speed = 100.0f;
    float lip = 8.0f;
    float wait = 3.0f;
    int angle = 0;
    int spawnflags = 0;
    bool startOpen = false;
    bool targetOpen = false;
    float openAmount = 0.0f;
};

struct CameraBody
{
    glm::vec3 position = glm::vec3(0.0f);
    float radius = 18.0f;
    float eyeOffset = 36.0f;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float turnSpeed = 180.0f;
    float forwardSpeed = 128.0f;
    float backwardSpeed = 92.0f;
    float strafeSpeed = 96.0f;
    float sprintMultiplier = 2.0f;
    float mouseSensitivity = 0.12f;
    float gravity = 512.0f;
    float jumpSpeed = 256.0f;
    float verticalSpeed = 0.0f;
    bool grounded = false;

    glm::vec3 eyePosition() const
    {
        return position + glm::vec3(0.0f, eyeOffset, 0.0f);
    }
};

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

std::string describeEntity(const TextMapEntity &entity, int index)
{
    const glm::vec3 origin = entity.origin(glm::vec3(0.0f));
    const std::string classname = entity.classname().empty() ? "(no classname)" : entity.classname();
    const std::string targetname = entity.value("targetname");
    const std::string target = entity.value("target");
    const std::string model = entity.value("model");
    const std::string mdl = entity.value("MDL");
    const std::string angle = entity.value("angle");
    const std::string spawnflags = entity.value("spawnflags");
    const std::string light = entity.value("light");

    char header[256];
    std::snprintf(header,
                  sizeof(header),
                  "[%d] class=%s brushes=%d origin=(%.0f %.0f %.0f)",
                  index,
                  classname.c_str(),
                  static_cast<int>(entity.brushes.size()),
                  origin.x,
                  origin.y,
                  origin.z);

    std::string line(header);
    if (!targetname.empty())
        line += " targetname=" + targetname;
    if (!target.empty())
        line += " target=" + target;
    if (!mdl.empty())
        line += " MDL=" + mdl;
    if (!model.empty())
        line += " model=" + model;
    if (!angle.empty())
        line += " angle=" + angle;
    if (!spawnflags.empty())
        line += " spawnflags=" + spawnflags;
    if (!light.empty())
        line += " light=" + light;
    return line;
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

    return ShaderManager::instance().loadFromSource("mapload_demo_static_shader", vert, frag);
}

void setupLighting(Shader *shader)
{
    if (!shader)
        return;
    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.20f, 0.22f, 0.24f));
}

void setProbeDefaults(glm::vec3 &probePosition, const BoundingBox &bounds)
{
    if (!bounds.is_valid())
        return;
    probePosition = bounds.center();
    probePosition.y = bounds.max.y + glm::max(bounds.size().y * 0.35f, 12.0f);
}

void copyStringToBuffer(std::array<char, 512> &buffer, const std::string &value)
{
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}

TextMapDocument filteredDocument(const TextMapDocument &document,
                                 const std::function<bool(const TextMapEntity &)> &predicate)
{
    TextMapDocument filtered;
    for (const TextMapEntity &entity : document.entities)
    {
        if (predicate(entity))
            filtered.entities.push_back(entity);
    }
    return filtered;
}

bool isDoorClassname(const std::string &classname)
{
    return classname == "func_door" ||
           classname == "func_door_rotating" ||
           classname == "func_button" ||
           classname == "func_plat";
}

bool isRenderableStaticBrushEntity(const TextMapEntity &entity)
{
    if (entity.brushes.empty())
        return false;

    const std::string classname = entity.classname();
    if (classname.empty())
        return true;

    if (isDoorClassname(classname))
        return false;

    if (classname == "trigger_once" ||
        classname == "trigger_multiple" ||
        classname == "trigger_push" ||
        classname == "trigger_teleport")
        return false;

    return true;
}

int parseIntOr(const std::string &value, int fallback)
{
    if (value.empty())
        return fallback;
    return std::atoi(value.c_str());
}

float parseFloatOr(const std::string &value, float fallback)
{
    if (value.empty())
        return fallback;
    return std::strtof(value.c_str(), nullptr);
}

glm::vec3 doorMoveDirection(int angle)
{
    if (angle >= 0)
    {
        const float radians = glm::radians(static_cast<float>(angle));
        return glm::normalize(glm::vec3(std::cos(radians), 0.0f, std::sin(radians)));
    }

    const float verticalDir = (angle == -1) ? 1.0f : -1.0f;
    return glm::vec3(0.0f, verticalDir, 0.0f);
}

float doorTravelDistance(const BoundingBox &bounds, int angle, float lip)
{
    if (!bounds.is_valid())
        return 0.0f;

    const glm::vec3 size = bounds.size();
    float distance = 0.0f;

    if (angle >= 0)
    {
        int codeAngle = 0;
        if (angle > 45 && angle <= 135) codeAngle = 90;
        if (angle > 135 && angle <= 225) codeAngle = 180;
        if (angle > 225 && angle <= 315) codeAngle = 270;

        switch (codeAngle)
        {
        case 0:
        case 180:
            distance = size.x;
            break;
        case 90:
        case 270:
            distance = size.z;
            break;
        }
    }
    else
    {
        distance = size.y;
    }

    return glm::max(distance - lip, 0.0f);
}

glm::vec3 findCameraSpawn(const BoundingBox &bounds,
                          const MapClipper &clipper,
                          float radius)
{
    glm::vec3 spawn = bounds.is_valid() ? bounds.center() : glm::vec3(0.0f);
    spawn.y = bounds.is_valid() ? (bounds.max.y + radius + 96.0f) : (radius + 96.0f);
    bool hitGround = false;
    clipper.dropToFloor(spawn, radius, 100000.0f, hitGround);
    return spawn;
}
} // namespace

int main()
{
    Device &device = Device::Instance();
    if (!device.Create(1600, 900, "MiniRender Map Load Demo", true))
        return 1;

    device.ImGuiInit();

    Scene scene;
    Camera *camera = scene.createCamera("mapload_camera");
    camera->setViewport(0, 0, device.GetWidth(), device.GetHeight());
    camera->setViewPlanes(1.0f, 8192.0f);
    scene.setCamera(camera);

    Shader *staticShader = createStaticShader();
    if (!staticShader)
    {
        device.Close();
        return 1;
    }

    Texture *white = TextureManager::instance().getWhite();

    Material probeMaterial;
    probeMaterial.name = "mapload_probe_material";
    probeMaterial.setTexture("u_albedo", white);
    probeMaterial.setVec4("u_color", glm::vec4(1.0f, 0.82f, 0.18f, 1.0f));

    Material hitMaterial;
    hitMaterial.name = "mapload_hit_material";
    hitMaterial.setTexture("u_albedo", white);
    hitMaterial.setVec4("u_color", glm::vec4(0.15f, 1.0f, 0.45f, 1.0f));

    Mesh *probeMesh = MeshManager::instance().create_sphere("mapload_probe_mesh", 0.35f, 12);
    Mesh *hitMesh = MeshManager::instance().create_sphere("mapload_hit_mesh", 0.18f, 10);
    Mesh *mapMesh = MeshManager::instance().create("mapload_scene_mesh");

    MeshNode *mapNode = scene.createMeshNode("mapload_map_node", mapMesh);
    mapNode->renderType = RenderType::Solid;

    MeshNode *probeNode = scene.createMeshNode("mapload_probe_node", probeMesh);
    probeNode->renderType = RenderType::Solid;
    probeNode->setMaterial(&probeMaterial);

    MeshNode *hitNode = scene.createMeshNode("mapload_hit_node", hitMesh);
    hitNode->renderType = RenderType::Solid;
    hitNode->setMaterial(&hitMaterial);

    std::array<char, 512> mapPathBuffer = {};
    std::array<char, 512> textureDirBuffer = {};
    copyStringToBuffer(mapPathBuffer, "assets/maps/doortest.map");
    copyStringToBuffer(textureDirBuffer, "assets/maps/base");

    TextMapDocument document;
    TextMapLoadResult loadResult;
    MapClipper staticClipper;
    MapClipper frameClipper;
    RenderBatch debugBatch;
    std::string statusMessage;
    std::string lastError;
    std::string resolvedMapPath;
    std::string resolvedTextureDir;
    std::string resolvedLightingPath;
    std::vector<std::string> entityLogLines;
    std::vector<DoorVisual> doors;
    CameraBody cameraBody;
    glm::vec3 probePosition(0.0f, 32.0f, 0.0f);
    glm::vec3 lastProbeGround(0.0f);
    bool hasProbeHit = false;
    bool showProbe = true;
    bool showHit = true;
    bool autoResetCamera = true;
    bool autoResetProbe = true;
    bool showMapBounds = true;
    bool showSurfaceBounds = false;
    bool showProbeBounds = true;
    bool showWorldGizmo = true;
    bool animateDoors = true;

    debugBatch.Init();

    const auto rebuildClipper = [&]()
    {
        frameClipper = staticClipper;
        for (const DoorVisual &door : doors)
        {
            const glm::vec3 offset = door.node ? door.node->position : glm::vec3(0.0f);
            frameClipper.appendTransformed(door.clipper, offset);
        }
    };

    const auto resetCameraBody = [&]()
    {
        cameraBody.position = findCameraSpawn(loadResult.bounds, frameClipper, cameraBody.radius);
        cameraBody.verticalSpeed = 0.0f;
        cameraBody.grounded = false;
        camera->setPosition(cameraBody.eyePosition());
        camera->setEulerAngles(glm::vec3(cameraBody.pitchDegrees, cameraBody.yawDegrees, 0.0f));
    };

    const auto reloadMap = [&](bool resetView) -> bool
    {
        for (DoorVisual &door : doors)
        {
            if (door.node)
            {
                scene.remove(door.node);
                delete door.node;
            }
            if (!door.meshName.empty())
                MeshManager::instance().unload(door.meshName);
        }
        doors.clear();
        staticClipper.clear();
        frameClipper.clear();

        TextMapDocument nextDocument;
        std::string error;
        resolvedMapPath = resolveProjectPath(mapPathBuffer.data(), false);
        resolvedTextureDir = resolveProjectPath(textureDirBuffer.data(), true);
        resolvedLightingPath = resolvedMapPath + ".lighting";

        if (!TextMapParser::loadFromFile(resolvedMapPath, nextDocument, &error))
        {
            lastError = "Mapa nao encontrado: " + resolvedMapPath + " (" + error + ")";
            statusMessage.clear();
            mapNode->visible = false;
            return false;
        }

        TextMapLoadOptions options;
        options.textureDirectory = resolvedTextureDir;

        TextMapDocument worldDocument = filteredDocument(
            nextDocument,
            [](const TextMapEntity &entity)
            {
                return isRenderableStaticBrushEntity(entity);
            });

        if (!TextMapLoader::build("mapload_scene_mesh", worldDocument, options, *mapMesh, &loadResult, &error))
        {
            lastError = error;
            statusMessage.clear();
            mapNode->visible = false;
            return false;
        }

        document = std::move(nextDocument);
        lastError.clear();
        mapNode->mesh = mapMesh;
        mapNode->visible = true;
        statusMessage = "Mapa carregado com sucesso.";
        if (pathExists(resolvedLightingPath))
            statusMessage += " Encontrado sidecar .lighting.";

        entityLogLines.clear();
        entityLogLines.reserve(document.entities.size());
        std::printf("\n=== Map Entities: %s ===\n", resolvedMapPath.c_str());
        for (size_t i = 0; i < document.entities.size(); ++i)
        {
            const std::string line = describeEntity(document.entities[i], static_cast<int>(i));
            entityLogLines.push_back(line);
            std::printf("%s\n", line.c_str());
        }
        std::printf("=== End Map Entities ===\n");

        for (const TextMapEntity &entity : document.entities)
        {
            if (isRenderableStaticBrushEntity(entity))
                staticClipper.addEntity(entity, true);
        }

        int doorIndex = 0;
        for (const TextMapEntity &entity : document.entities)
        {
            if (entity.classname() != "func_door" || entity.brushes.empty())
                continue;

            TextMapDocument doorDocument;
            doorDocument.entities.push_back(entity);
            DoorVisual door;
            door.meshName = "mapload_door_mesh_" + std::to_string(doorIndex);
            door.mesh = MeshManager::instance().create(door.meshName);
            TextMapLoadResult doorResult;

            if (!TextMapLoader::build(door.meshName, doorDocument, options, *door.mesh, &doorResult, &error))
            {
                MeshManager::instance().unload(door.meshName);
                continue;
            }

            door.node = scene.createMeshNode("mapload_door_node_" + std::to_string(doorIndex), door.mesh);
            door.node->renderType = RenderType::Solid;
            door.bounds = doorResult.bounds;
            door.clipper.addEntity(entity, true);
            door.angle = parseIntOr(entity.value("angle"), 0);
            door.speed = parseFloatOr(entity.value("speed"), 100.0f);
            door.lip = parseFloatOr(entity.value("lip"), 8.0f);
            door.wait = parseFloatOr(entity.value("wait"), 3.0f);
            door.spawnflags = parseIntOr(entity.value("spawnflags"), 0);
            door.startOpen = (door.spawnflags & 1) != 0;
            door.targetOpen = door.startOpen;
            door.moveDirection = doorMoveDirection(door.angle);
            door.travelDistance = doorTravelDistance(door.bounds, door.angle, door.lip);
            door.openAmount = door.startOpen ? 1.0f : 0.0f;
            door.node->setPosition(door.moveDirection * (door.travelDistance * door.openAmount));
            doors.push_back(std::move(door));
            ++doorIndex;
        }

        rebuildClipper();

        if (autoResetProbe || resetView)
            setProbeDefaults(probePosition, loadResult.bounds);

        if (resetView || autoResetCamera)
            resetCameraBody();

        return true;
    };

    reloadMap(true);

    while (device.Run())
    {
        const float dt = device.GetFrameTime();

        if (animateDoors)
        {
            for (DoorVisual &door : doors)
            {
                if (!door.node || door.travelDistance <= 1e-4f)
                    continue;

                const float travelTime = door.travelDistance / glm::max(door.speed, 1.0f);
                const float step = (travelTime > 1e-4f) ? (dt / travelTime) : 1.0f;
                const float target = door.targetOpen ? 1.0f : 0.0f;
                if (door.openAmount < target)
                    door.openAmount = glm::min(target, door.openAmount + step);
                else if (door.openAmount > target)
                    door.openAmount = glm::max(target, door.openAmount - step);

                door.node->setPosition(door.moveDirection * (door.travelDistance * door.openAmount));
            }
        }

        rebuildClipper();

        if (frameClipper.brushCount() > 0)
        {
            float movement = 0.0f;
            float strafe = 0.0f;
            float turn = 0.0f;
            const bool allowMouseLook =
                !ImGui::GetIO().WantCaptureMouse && Input::IsMouseDown(MouseButton::LEFT);

            if (Input::IsKeyDown(KEY_UP) || Input::IsKeyDown(KEY_W))
                movement = cameraBody.forwardSpeed;
            if (Input::IsKeyDown(KEY_DOWN) || Input::IsKeyDown(KEY_S))
                movement = -cameraBody.backwardSpeed;
            if (Input::IsKeyDown(KEY_A))
                strafe = -cameraBody.strafeSpeed;
            if (Input::IsKeyDown(KEY_D))
                strafe = cameraBody.strafeSpeed;
            if (Input::IsKeyDown(KEY_LEFT))
                turn = -cameraBody.turnSpeed;
            if (Input::IsKeyDown(KEY_RIGHT))
                turn = cameraBody.turnSpeed;
            if (Input::IsKeyDown(KEY_LEFT_SHIFT))
            {
                if (movement > 0.0f)
                    movement *= cameraBody.sprintMultiplier;
                strafe *= cameraBody.sprintMultiplier;
            }
            if (Input::IsKeyPressed(KEY_SPACE) && cameraBody.grounded)
                cameraBody.verticalSpeed = cameraBody.jumpSpeed;

            cameraBody.yawDegrees += turn * dt;
            if (allowMouseLook)
            {
                const glm::vec2 mouseDelta = Input::GetMouseDelta();
                cameraBody.yawDegrees += -mouseDelta.x * cameraBody.mouseSensitivity;
                cameraBody.pitchDegrees += -mouseDelta.y * cameraBody.mouseSensitivity;
                cameraBody.pitchDegrees = glm::clamp(cameraBody.pitchDegrees, -89.0f, 89.0f);
            }
            camera->setEulerAngles(glm::vec3(cameraBody.pitchDegrees, cameraBody.yawDegrees, 0.0f));

            glm::vec3 forward = camera->forward();
            forward.y = 0.0f;
            if (glm::length2(forward) > 1e-8f)
                forward = glm::normalize(forward);
            else
                forward = glm::vec3(0.0f, 0.0f, -1.0f);

            glm::vec3 right = camera->right();
            right.y = 0.0f;
            if (glm::length2(right) > 1e-8f)
                right = glm::normalize(right);
            else
                right = glm::vec3(1.0f, 0.0f, 0.0f);

            cameraBody.verticalSpeed -= cameraBody.gravity * dt;
            const glm::vec3 desiredEnd = cameraBody.position +
                                         forward * (movement * dt) +
                                         right * (strafe * dt) +
                                         glm::vec3(0.0f, cameraBody.verticalSpeed * dt, 0.0f);

            bool hitGround = false;
            frameClipper.traceSphere(cameraBody.position, desiredEnd, cameraBody.radius, hitGround);
            if (hitGround && cameraBody.verticalSpeed < 0.0f)
                cameraBody.verticalSpeed = 0.0f;
            cameraBody.grounded = hitGround;

            camera->setPosition(cameraBody.eyePosition());
        }

        scene.update(dt);

        probeNode->visible = showProbe;
        probeNode->setPosition(probePosition);

        hasProbeHit = false;
        if (frameClipper.brushCount() > 0)
        {
            glm::vec3 probeCenter = probePosition;
            bool hitGround = false;
            if (frameClipper.dropToFloor(probeCenter, 0.5f, 10000.0f, hitGround))
            {
                hasProbeHit = true;
                lastProbeGround = probeCenter - glm::vec3(0.0f, 0.5f, 0.0f);
            }
        }

        hitNode->visible = showHit && hasProbeHit;
        if (hasProbeHit)
            hitNode->setPosition(lastProbeGround);

        device.ImGuiBegin();

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Once);
        if (ImGui::Begin("Map Load Demo"))
        {
            ImGui::InputText("Map", mapPathBuffer.data(), mapPathBuffer.size());
            ImGui::InputText("Textures", textureDirBuffer.data(), textureDirBuffer.size());

            if (ImGui::Button("Reload Map"))
                reloadMap(false);
            ImGui::SameLine();
            if (ImGui::Button("Reset Camera"))
                resetCameraBody();

            ImGui::Checkbox("Auto Reset Camera", &autoResetCamera);
            ImGui::Checkbox("Auto Reset Probe", &autoResetProbe);
            ImGui::Checkbox("Show Map Bounds", &showMapBounds);
            ImGui::Checkbox("Show Surface Bounds", &showSurfaceBounds);
            ImGui::Checkbox("Show Probe Bounds", &showProbeBounds);
            ImGui::Checkbox("Show World Gizmo", &showWorldGizmo);
            ImGui::Checkbox("Animate Doors", &animateDoors);
            ImGui::DragFloat("Body Radius", &cameraBody.radius, 0.25f, 1.0f, 96.0f, "%.2f");
            ImGui::DragFloat("Eye Offset", &cameraBody.eyeOffset, 0.25f, -64.0f, 128.0f, "%.2f");
            ImGui::DragFloat("Strafe Speed", &cameraBody.strafeSpeed, 1.0f, 0.0f, 1024.0f, "%.1f");
            ImGui::DragFloat("Mouse Sens", &cameraBody.mouseSensitivity, 0.01f, 0.01f, 2.0f, "%.2f");
            ImGui::DragFloat("Gravity", &cameraBody.gravity, 1.0f, 0.0f, 4096.0f, "%.1f");
            ImGui::DragFloat("Jump", &cameraBody.jumpSpeed, 1.0f, 0.0f, 2048.0f, "%.1f");

            if (!statusMessage.empty())
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "%s", statusMessage.c_str());
            if (!lastError.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", lastError.c_str());

            ImGui::TextWrapped("Resolved Map: %s", resolvedMapPath.empty() ? "(none)" : resolvedMapPath.c_str());
            ImGui::TextWrapped("Resolved Textures: %s", resolvedTextureDir.empty() ? "(none)" : resolvedTextureDir.c_str());
            ImGui::TextWrapped("Resolved Lighting: %s", resolvedLightingPath.empty() ? "(none)" : resolvedLightingPath.c_str());
            ImGui::Text("Lighting Cache: %s", pathExists(resolvedLightingPath) ? "yes" : "no");

            ImGui::Separator();
            ImGui::Text("Entities: %d", document.entityCount());
            ImGui::Text("Brushes: %d", document.brushCount());
            ImGui::Text("Faces: %d", document.faceCount());
            ImGui::Text("Render verts: %d", mapMesh ? mapMesh->vertexCount() : 0);
            ImGui::Text("Render tris: %d", mapMesh ? mapMesh->indexCount() / 3 : 0);
            ImGui::Text("Clip brushes: %d", frameClipper.brushCount());
            ImGui::Text("Doors: %d", static_cast<int>(doors.size()));

            if (ImGui::CollapsingHeader("Entity Log", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const std::string &line : entityLogLines)
                    ImGui::TextWrapped("%s", line.c_str());
            }

            if (!doors.empty())
            {
                if (ImGui::Button("Toggle Doors"))
                {
                    for (DoorVisual &door : doors)
                        door.targetOpen = !door.targetOpen;
                }
                const DoorVisual &door = doors.front();
                ImGui::Text("Door angle: %d", door.angle);
                ImGui::Text("Door travel: %.2f", door.travelDistance);
                ImGui::Text("Door speed: %.2f", door.speed);
                ImGui::Text("Door open: %.2f", door.openAmount);
            }

            ImGui::Text("Body pos: %.2f %.2f %.2f",
                        cameraBody.position.x, cameraBody.position.y, cameraBody.position.z);
            ImGui::Text("Yaw: %.2f", cameraBody.yawDegrees);
            ImGui::Text("Pitch: %.2f", cameraBody.pitchDegrees);
            ImGui::Text("Vertical speed: %.2f", cameraBody.verticalSpeed);
            ImGui::Text("Grounded: %s", cameraBody.grounded ? "yes" : "no");

            ImGui::Separator();
            ImGui::Checkbox("Show Probe", &showProbe);
            ImGui::Checkbox("Show Hit Marker", &showHit);
            ImGui::DragFloat3("Probe Position", &probePosition.x, 0.25f);
            if (hasProbeHit)
                ImGui::Text("Ground Y: %.3f", lastProbeGround.y);
            else
                ImGui::TextDisabled("No downward hit from clipper.");

            if (loadResult.bounds.is_valid())
            {
                const glm::vec3 center = loadResult.bounds.center();
                const glm::vec3 size = loadResult.bounds.size();
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
        scene.render(RenderType::Solid);
        scene.endPass();

        debugBatch.SetMatrix(camera->viewProjection);
        Material::applyDefaultStates();

        if (showWorldGizmo)
        {
            debugBatch.SetColor(255, 64, 64, 255);
            debugBatch.Line3D(glm::vec3(-8.0f, 0.0f, 0.0f), glm::vec3(8.0f, 0.0f, 0.0f));
            debugBatch.SetColor(64, 255, 64, 255);
            debugBatch.Line3D(glm::vec3(0.0f, -8.0f, 0.0f), glm::vec3(0.0f, 8.0f, 0.0f));
            debugBatch.SetColor(64, 160, 255, 255);
            debugBatch.Line3D(glm::vec3(0.0f, 0.0f, -8.0f), glm::vec3(0.0f, 0.0f, 8.0f));
            debugBatch.SetColor(255, 255, 255, 255);
            debugBatch.Cube(glm::vec3(0.0f, 0.0f, 0.0f), 2.0f, 2.0f, 2.0f, true);
        }

        if (showMapBounds && loadResult.bounds.is_valid())
        {
            debugBatch.SetColor(255, 220, 64, 255);
            debugBatch.Box(loadResult.bounds);
        }

        if (showSurfaceBounds)
            scene.debug(&debugBatch);

        if (showProbeBounds)
        {
            BoundingBox probeBox;
            const glm::vec3 halfProbe(0.45f);
            probeBox.expand(probePosition - halfProbe);
            probeBox.expand(probePosition + halfProbe);
            debugBatch.SetColor(255, 160, 0, 255);
            debugBatch.Box(probeBox);

            if (hasProbeHit)
            {
                BoundingBox hitBox;
                const glm::vec3 halfHit(0.25f);
                hitBox.expand(lastProbeGround - halfHit);
                hitBox.expand(lastProbeGround + halfHit);
                debugBatch.SetColor(64, 255, 128, 255);
                debugBatch.Box(hitBox);
            }
        }

        debugBatch.Render();
        device.Flip();
    }

    scene.remove(mapNode);
    scene.remove(probeNode);
    scene.remove(hitNode);
    delete mapNode;
    delete probeNode;
    delete hitNode;
    for (DoorVisual &door : doors)
    {
        if (door.node)
        {
            scene.remove(door.node);
            delete door.node;
        }
        if (!door.meshName.empty())
            MeshManager::instance().unload(door.meshName);
    }

    device.ImGuiShutdown();
    device.Close();
    return 0;
}
