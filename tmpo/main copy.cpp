#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

#include "Input.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"

const int SCREEN_W = 1024;
const int SCREEN_H = 768;

// shader minimo — position + normal + cor flat
const char* VERT_SRC = R"(
#version 300 es
precision highp float;
layout(location=0) in vec3 a_position;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec4 a_tangent;
layout(location=3) in vec2 a_uv;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_normal;
out vec3 v_worldPos;
out vec2 v_uv;

void main() {
    vec4 world = u_model * vec4(a_position, 1.0);
    v_worldPos = world.xyz;
    v_normal   = mat3(u_model) * a_normal;
    v_uv       = a_uv;
    gl_Position = u_proj * u_view * world;
}
)";

const char* FRAG_SRC = R"(
#version 300 es
precision highp float;
in vec3 v_normal;
in vec3 v_worldPos;
in vec2 v_uv;

uniform vec3 u_lightPos;
uniform vec3 u_color;

out vec4 out_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 L = normalize(u_lightPos - v_worldPos);
    float diff = max(dot(N, L), 0.0);
    vec3 col = u_color * (0.2 + 0.8 * diff);
    out_color = vec4(col, 1.0);
}
)";

int main(int argc, char* argv[])
{
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "Mini Pipeline",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_Log("[GLAD] Failed");
        return -1;
    }

    SDL_Log("[GL]  %s", glGetString(GL_VERSION));
    SDL_Log("[GPU] %s", glGetString(GL_RENDERER));

    // Inicializar estados via RenderState

    // ── Shader ───────────────────────────────────────────
    auto& shaders = ShaderManager::instance();
    Shader* shader = shaders.loadFromSource("default", VERT_SRC, FRAG_SRC);
    if (!shader)
        return -1;

    // ── Meshes ───────────────────────────────────────────
    auto& meshes = MeshManager::instance();
    Mesh* cube  = meshes.create_cube("cube", 1.0f);
    Mesh* plane = meshes.create_plane("plane", 10.0f, 10.0f, 1);
    Mesh* debugBox = meshes.create_wire_cube("debug_box", 1.0f); // Cubo wireframe
    if (!cube || !plane)
        return -1;

    // ── Materials ────────────────────────────────────────
    auto &materials = MaterialManager::instance();
    Material *planeMat = materials.create("mat_plane");
    planeMat->setShader(shader)->setVec3("u_color", {0.4f, 0.4f, 0.4f});
    
    Material *debugMat = materials.create("mat_debug");
    debugMat->setShader(shader)->setVec3("u_color", {0.0f, 1.0f, 0.0f}); // Verde

    // ── Camera ───────────────────────────────────────────
    Camera camera;
    FreeCameraComponent cameraControl;
    camera.position = glm::vec3(0, 3, 10);
    camera.lookAt({0, 0, 0}); // Olha para a origem
    camera.setAspect(SCREEN_W, SCREEN_H);
    
    // Segunda câmara (vista de topo)
    Camera camera2;
    camera2.position = glm::vec3(0, 15, 0.1f);
    camera2.lookAt({0, 0, 0}); // Olha para baixo

    glm::vec3 cubePositions[] = {
        { 0.0f, 0.5f,  0.0f},
        { 3.0f, 0.5f, -3.0f},
        {-3.0f, 0.5f, -3.0f},
        { 5.0f, 0.5f,  2.0f},
        {-5.0f, 0.5f,  2.0f},
    };

    glm::vec3 cubeColors[] = {
        {1.0f, 0.3f, 0.3f},
        {0.3f, 1.0f, 0.3f},
        {0.3f, 0.3f, 1.0f},
        {1.0f, 1.0f, 0.3f},
        {1.0f, 0.3f, 1.0f},
    };

    Material *cubeMaterials[5] = {};
    for (int i = 0; i < 5; ++i)
    {
        cubeMaterials[i] = materials.create("mat_cube_" + std::to_string(i));
        cubeMaterials[i]->setShader(shader)->setVec3("u_color", cubeColors[i]);
    }

    RenderQueue renderQueue;
    ForwardTechnique forwardTechnique;
    DebugPass debugPass(debugBox, debugMat); // Instancia o DebugPass
    FrameContext frameCtx;
    frameCtx.camera = &camera;

    Uint64 now  = SDL_GetPerformanceCounter();
    Uint64 last = 0;
    float  time = 0.0f;
    Input& input = Input::instance();

    while (!input.shouldQuit())
    {
        last = now;
        now  = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        time += dt;

        input.process();

        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        
        // Dividir o ecrã a meio
        camera.setAspect(w / 2, h);
        camera2.setAspect(w / 2, h);
        cameraControl.update(camera, dt);

        glm::vec3 lightPos = {
            sinf(time) * 5.0f,
            5.0f + cosf(time) * 1.0f,
            cosf(time) * 4.0f
        };
        frameCtx.lightPos = lightPos;
        renderQueue.clear();

        // ── plano ─────────────────────────────────────────
        {
            RenderItem item;
            item.mesh = plane;
            item.material = planeMat;
            item.model = glm::mat4(1.0f);
            item.passMask = RenderPassMask::Opaque;
            renderQueue.add(item);
        }

        // ── cubos ─────────────────────────────────────────
        for (int i = 0; i < 5; i++)
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), cubePositions[i]);
            model = glm::rotate(model, time * 0.5f + i, glm::vec3(0, 1, 0));

            RenderItem item;
            item.mesh = cube;
            item.material = cubeMaterials[i];
            item.model = model;
            item.passMask = RenderPassMask::Opaque;
            renderQueue.add(item);
        }

        // Render Esquerda (Câmara Livre)
        frameCtx.camera = &camera;
        frameCtx.viewport = {0, 0, w / 2, h};
        forwardTechnique.render(frameCtx, renderQueue);
        debugPass.execute(frameCtx, renderQueue); // Desenha as boxes na esquerda

        // Render Direita (Câmara Topo)
        frameCtx.camera = &camera2;
        frameCtx.viewport = {w / 2, 0, w / 2, h};
        forwardTechnique.render(frameCtx, renderQueue);
        debugPass.execute(frameCtx, renderQueue); // Desenha as boxes na direita

        SDL_GL_SwapWindow(window);
    }

    materials.unloadAll();
    meshes.unloadAll();
    shaders.unloadAll();
    RenderState::instance().shutdown();

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
