#include <cstdio>
#include <filesystem>
#include <string>

#include "Core.hpp"
#include "Device.hpp"
#include "Input.hpp"
#include "LevelFormat.hpp"
#include "LevelNode.hpp"
#include "imgui.h"

// ============================================================
//  Shader — supports albedo + lightmap (2 UV channels)
// ============================================================
static Shader* createLevelShader()
{
    const char* vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec4 tangent;
        layout(location = 3) in vec2 uv;
        layout(location = 4) in vec2 lightmapUv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;
        out vec2 v_uv;
        out vec2 v_lightmapUv;

        void main()
        {
            v_normal      = normalize(u_normalMatrix * normal);
            v_uv          = uv;
            v_lightmapUv  = lightmapUv;
            gl_Position   = u_projection * u_view * u_model * vec4(position, 1.0);
        }
    );

    const char* frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        in vec2 v_lightmapUv;
        out vec4 FragColor;

        uniform vec3      u_color;
        uniform sampler2D u_albedo;
        uniform sampler2D u_lightmap;
        uniform int       u_hasAlbedo;
        uniform int       u_hasLightmap;
        uniform int       u_debugMode;
        uniform float     u_lightmapFactor;
        uniform vec3      u_lightDir;
        uniform vec3      u_ambient;

        void main()
        {
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);

            vec3 albedo = u_color;
            if (u_hasAlbedo > 0)
                albedo *= texture(u_albedo, v_uv).rgb;

            int hasLm = (u_hasLightmap > 0) ? 1 : 0;
            vec3 lm = (hasLm > 0) ? texture(u_lightmap, v_lightmapUv).rgb : vec3(1.0);

            if (u_debugMode == 0)
            {
                float factor = clamp(u_lightmapFactor, 0.0, 2.0);
                vec3 lit = (hasLm > 0)
                    ? albedo * mix(vec3(1.0), lm, factor)
                    : albedo * (u_ambient + vec3(0.85 * diff));
                FragColor = vec4(lit, 1.0);
                return;
            }
            if (u_debugMode == 1)
            {
                FragColor = vec4(lm, 1.0);
                return;
            }
            if (u_debugMode == 2)
            {
                FragColor = vec4(albedo, 1.0);
                return;
            }
            if (u_debugMode == 3)
            {
                FragColor = vec4(0.75, 0.75, 0.75, 1.0);
                return;
            }
            if (u_debugMode == 4)
            {
                FragColor = vec4(fract(v_uv), 0.0, 1.0);
                return;
            }
        }
    );

    return ShaderManager::instance().loadFromSource("level_viewer_shader", vert, frag);
}

// ============================================================
//  Main
// ============================================================
int main(int argc, char** argv)
{
    const char* levelPath = (argc > 1) ? argv[1] : "level.mrlvl";

    Device& device = Device::Instance();
    if (!device.Create(1600, 900, "MiniRender Level Viewer", true))
        return 1;

    device.ImGuiInit();

    // Scene + camera
    Scene scene;
    Camera* camera = scene.createFreeCamera(
        "viewer_camera",
        device.GetWidth(), device.GetHeight(),
        glm::vec3(0.0f, 100.0f, 200.0f),    // position
        glm::vec3(0.0f, 0.0f, 0.0f),         // target
        800.0f,                                // move speed
        0.15f,                                 // mouse sensitivity
        2.5f                                   // sprint multiplier
    );
    camera->setViewPlanes(1.0f, 8192.0f);
    scene.setCamera(camera);

    // Shader
    Shader* shader = createLevelShader();
    if (!shader)
    {
        printf("[LevelViewer] Failed to create shader.\n");
        device.Close();
        return 1;
    }
    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.20f, 0.22f, 0.24f));
    shader->setInt("u_debugMode", 0);
    shader->setFloat("u_lightmapFactor", 1.0f);

    // Load level
    LevelData level;
    LevelReader reader;
    std::filesystem::path levelFsPath(levelPath);
    if (levelFsPath.has_parent_path())
        reader.textureDir = levelFsPath.parent_path().string();
    bool loaded = reader.load(levelPath, &level);

    LevelNode* levelNode = nullptr;
    if (loaded)
    {
        levelNode = new LevelNode();
        levelNode->name = "level";
        levelNode->levelMesh = &level.mesh;
        levelNode->uploadLightmap();
        scene.add(levelNode);

        // Position camera at player start if available
        if (!level.playerStarts.empty())
        {
            const auto& ps = level.playerStarts[0];
            camera->setPosition(ps.position + glm::vec3(0.0f, 64.0f, 0.0f));
            camera->lookAt(ps.position + ps.direction * 128.0f);
        }
        else if (level.mesh.aabb.is_valid())
        {
            const glm::vec3 center = level.mesh.aabb.center();
            const glm::vec3 size = level.mesh.aabb.size();
            const float radius = glm::max(glm::max(size.x, size.y), size.z) * 0.5f;
            camera->setViewPlanes(glm::max(1.0f, radius / 2048.0f), glm::max(8192.0f, radius * 6.0f));
            camera->setPosition(center + glm::vec3(0.0f, radius * 0.35f, radius * 1.35f));
            camera->lookAt(center);
        }

        shader->setInt("u_hasLightmap", level.mesh.lightmaps.empty() ? 0 : 1);

        printf("[LevelViewer] Loaded: %s  (%d verts, %d tris, %d entities)\n",
               levelPath,
               level.mesh.buffer.vertexCount(),
               level.mesh.buffer.indexCount() / 3,
               level.entityCount());
    }
    else
    {
        printf("[LevelViewer] Failed to load: %s\n", levelPath);
    }

    // Main loop
    int debugMode = 0;
    float lightmapFactor = 1.0f;
    while (device.Run())
    {
        const float dt = device.GetFrameTime();

        // Toggle mouse capture with Escape
        if (Input::IsKeyPressed(KEY_ESCAPE))
        {
            bool captured = SDL_GetRelativeMouseMode();
            SDL_SetRelativeMouseMode(captured ? SDL_FALSE : SDL_TRUE);
        }

        scene.update(dt);

        device.ImGuiBegin();

        // HUD
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_Always);
        if (ImGui::Begin("Level Viewer", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        {
            if (loaded)
            {
                ImGui::Text("File: %s", levelPath);
                ImGui::Text("Vertices: %d", level.mesh.buffer.vertexCount());
                ImGui::Text("Triangles: %d", level.mesh.buffer.indexCount() / 3);
                ImGui::Text("Surfaces: %d", (int)level.mesh.surfaces.size());
                ImGui::Text("Materials: %d", (int)level.mesh.materials.size());
                ImGui::Text("Lightmap pages: %d", (int)level.mesh.lightmaps.size());
                ImGui::Text("Bounds min: %.0f %.0f %.0f",
                            level.mesh.aabb.min.x, level.mesh.aabb.min.y, level.mesh.aabb.min.z);
                ImGui::Text("Bounds max: %.0f %.0f %.0f",
                            level.mesh.aabb.max.x, level.mesh.aabb.max.y, level.mesh.aabb.max.z);
                ImGui::Text("Camera: %.0f %.0f %.0f",
                            camera->position.x, camera->position.y, camera->position.z);
                ImGui::Combo("View", &debugMode, "Textures + Lightmap\0Lightmap\0Textures\0Solid\0UV\0");
                ImGui::SliderFloat("Lightmap factor", &lightmapFactor, 0.0f, 2.0f, "%.2f");
                ImGui::Separator();
                ImGui::Text("Entities: %d", level.entityCount());
                ImGui::Text("  Lights: %d", (int)level.lights.size());
                ImGui::Text("  Doors: %d", (int)level.doors.size());
                ImGui::Text("  Elevators: %d", (int)level.elevators.size());
                ImGui::Text("  Triggers: %d", (int)level.triggers.size());
            }
            else
            {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "No level loaded");
                ImGui::Text("Usage: level_viewer <file.mrlvl>");
            }
            ImGui::Separator();
            ImGui::Text("FPS: %.0f", 1.0f / dt);
            ImGui::Text("Press ESC to toggle mouse");
        }
        ImGui::End();

        // Render
        scene.setCamera(camera);
        scene.beginPass();
        scene.setShader(shader);
        shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
        shader->setVec3("u_ambient", glm::vec3(0.35f, 0.36f, 0.38f));
        shader->setInt("u_debugMode", debugMode);
        shader->setFloat("u_lightmapFactor", lightmapFactor);
        scene.render(RenderType::Solid);
        scene.endPass();

        device.ImGuiEnd();
        device.Flip();
    }

    device.Close();
    return 0;
}
