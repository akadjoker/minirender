#include <cstdio>
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

        uniform vec4      u_color;
        uniform sampler2D u_albedo;
        uniform sampler2D u_lightmap;
        uniform int       u_hasLightmap;
        uniform vec3      u_lightDir;
        uniform vec3      u_ambient;

        void main()
        {
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);

            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lit;

            if (u_hasLightmap > 0)
            {
                vec3 lm = texture(u_lightmap, v_lightmapUv).rgb;
                lit = albedo.rgb * lm;
            }
            else
            {
                lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            }

            FragColor = vec4(lit, albedo.a);
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
        200.0f,                                // move speed
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

    // Load level
    LevelData level;
    LevelReader reader;
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
        }

        shader->setInt("u_hasLightmap", level.mesh.lightmap.empty() ? 0 : 1);

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
                ImGui::Text("Lightmap: %s", level.mesh.lightmap.empty() ? "no" : "yes");
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
        scene.render(RenderType::Solid);
        scene.endPass();

        device.ImGuiEnd();
        device.Flip();
    }

    device.Close();
    return 0;
}
