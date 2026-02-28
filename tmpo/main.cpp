#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Input.hpp"
#include "Mesh.hpp"
#include "MeshManager.hpp"
#include "Texture.hpp"
#include "TextureManager.hpp"
#include "Shader.hpp"
#include "ShaderManager.hpp"
#include "Camera.hpp"
#include "Material.hpp"

const int SCREEN_W = 1024;
const int SCREEN_H = 768;

// ── Shaders inline ───────────────────────────────────────
static const char *VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

void main() {
    vec4 world  = u_model * vec4(aPos, 1.0);
    FragPos     = world.xyz;
    Normal      = mat3(transpose(inverse(u_model))) * aNormal;
    TexCoord    = aUV;
    gl_Position = u_proj * u_view * world;
}
)";

static const char *FRAG_SRC = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform vec4      u_color;
uniform vec3      u_lightPos;
uniform vec3      u_viewPos;
uniform sampler2D u_diffuse;
uniform int       u_hasTexture;

void main() {
    vec3 base = (u_hasTexture == 1)
        ? texture(u_diffuse, TexCoord).rgb
        : u_color.rgb;

    // Ambient
    vec3 ambient = 0.2 * base;

    // Diffuse
    vec3 norm     = normalize(Normal);
    vec3 lightDir = normalize(u_lightPos - FragPos);
    float diff    = max(dot(norm, lightDir), 0.0);
    vec3 diffuse  = diff * base;

    // Specular Blinn-Phong
    vec3 viewDir  = normalize(u_viewPos - FragPos);
    vec3 halfway  = normalize(lightDir + viewDir);
    float spec    = pow(max(dot(norm, halfway), 0.0), 32.0);
    vec3 specular = 0.3 * spec * vec3(1.0);

    FragColor = vec4(ambient + diffuse + specular, u_color.a);
}
)";

// ── Helpers ──────────────────────────────────────────────
static void drawMesh(Shader *shader, Mesh *mesh,
                     const glm::mat4 &model,
                     const glm::mat4 &view,
                     const glm::mat4 &proj,
                     const glm::vec3 &lightPos,
                     const glm::vec3 &camPos,
                     const glm::vec4 &color,
                     Texture *tex = nullptr)
{
    shader->bind();
    shader->setMat4("u_model", model);
    shader->setMat4("u_view", view);
    shader->setMat4("u_proj", proj);
    shader->setVec3("u_lightPos", lightPos);
    shader->setVec3("u_viewPos", camPos);
    shader->setVec4("u_color", color);

    if (tex)
    {
        tex->bind(0);
        shader->setInt("u_diffuse", 0);
        shader->setInt("u_hasTexture", 1);
    }
    else
    {
        shader->setInt("u_hasTexture", 0);
    }

    mesh->draw();
}

// ────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{

    // ── SDL init ─────────────────────────────────────────
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        SDL_Log("[SDL] Init failed: %s", SDL_GetError());
        return 1;
    }
    if (IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) == 0)
    {
        SDL_Log("[IMG] Init failed: %s", IMG_GetError());
        return 1;
    }

    // ── OpenGL context ───────────────────────────────────
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        "Mini Pipeline",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Log("[SDL] Window failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // vsync

    // ── GLAD ─────────────────────────────────────────────
    if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("[GLAD] Failed to load OpenGL");
        return 1;
    }

    SDL_Log("[GL]  %s", glGetString(GL_VERSION));
    SDL_Log("[GPU] %s", glGetString(GL_RENDERER));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glViewport(0, 0, SCREEN_W, SCREEN_H);

    // ── Shaders ──────────────────────────────────────────
    Shader *shader = ShaderManager::instance().loadFromSource("main", VERT_SRC, FRAG_SRC);
    if (!shader)
    {
        SDL_Log("[Main] Shader failed");
        return 1;
    }

    // ── Meshes ───────────────────────────────────────────
    Mesh *cube = MeshManager::instance().createCube("Cube");
    Mesh *plane = MeshManager::instance().createPlane("Ground", 20.0f, 10);

    // ── Texturas ─────────────────────────────────────────
    Texture *texWall = TextureManager::instance().load("assets/wall.jpg");
    Texture *texGrass = TextureManager::instance().load("assets/grass.jpg");

    // ── Camera ───────────────────────────────────────────
    Camera camera;
    FreeCameraComponent cameraControl;
    camera.setAspect(SCREEN_W, SCREEN_H);

    // ── Posições dos cubos ───────────────────────────────
    glm::vec3 cubePositions[] = {
        {0.0f, 0.5f, 0.0f},
        {3.0f, 0.5f, -3.0f},
        {-3.0f, 0.5f, -3.0f},
        {5.0f, 0.5f, 2.0f},
        {-5.0f, 0.5f, 2.0f},
    };

    // ── Time ─────────────────────────────────────────────
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 last = 0;
    float time = 0.0f;

    Input &input = Input::instance();

    // ── Loop ─────────────────────────────────────────────
    while (!input.shouldQuit())
    {
        last = now;
        now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        time += dt;

        input.process();

        // Resize
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        camera.setAspect(w, h);

        // Update câmera
        cameraControl.update(camera, dt);

        // Luz animada
        glm::vec3 lightPos = {
            sin(time) * 5.0f,
            5.0f + cos(time) * 1.0f,
            cos(time) * 4.0f};

        glm::mat4 view = camera.getView();
        glm::mat4 proj = camera.getProjection();

        // ── Clear ────────────────────────────────────────
        glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ── Plano ────────────────────────────────────────
        glm::mat4 planeModel = glm::translate(glm::mat4(1.0f), glm::vec3(0, -1, 0));
        drawMesh(shader, plane, planeModel, view, proj,
                 lightPos, camera.position,
                 glm::vec4(1, 1, 1, 1), texGrass);

        // ── Cubos ────────────────────────────────────────
        for (auto &pos : cubePositions)
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
            model = glm::rotate(model, time * glm::radians(20.0f), glm::vec3(0, 1, 0));
            drawMesh(shader, cube, model, view, proj,
                     lightPos, camera.position,
                     glm::vec4(1, 1, 1, 1), texWall);
        }

        SDL_GL_SwapWindow(window);
    }

    // ── Cleanup ──────────────────────────────────────────
    MeshManager::instance().clear();
    TextureManager::instance().clear();
    ShaderManager::instance().clear();

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();

    return 0;
}
