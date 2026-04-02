#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include "Input.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"
#include "Scene.hpp"

const int SCREEN_W = 1024;
const int SCREEN_H = 768;

class Demo
{
public:
    virtual ~Demo() = default;
    virtual bool init() = 0;
    virtual void update(float dt) = 0;
    virtual void render() = 0;
    virtual void release() = 0;
    virtual const char *name() = 0;
};

int main(int argc, char *argv[])
{
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

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

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("[GLAD] Failed");
        return -1;
    }

    SDL_Log("[GL]  %s", glGetString(GL_VERSION));
    SDL_Log("[GPU] %s", glGetString(GL_RENDERER));

    // Inicializar estados via RenderState

    // ── Shader ───────────────────────────────────────────
    auto &shaders = ShaderManager::instance();
    Shader *shader = shaders.load("unlit",
                                  "assets/shaders/unlit.vert",
                                  "assets/shaders/unlit.frag");
    if (!shader)
        return -1;

    // Definir shader e textura fallback UMA VEZ — o loader aplica automaticamente
    auto &materials = MaterialManager::instance();
    materials.setDefaults(shader, TextureManager::instance().getWhite());

    // ── Meshes ───────────────────────────────────────────
    auto &meshes = MeshManager::instance();

    // Sponza — texturas na mesma pasta que o .obj (deteção automática)
    Mesh *sponza = meshes.load("sponza", "assets/obj/sponza.obj", "assets/textures");
    if (!sponza)
    {
        SDL_Log("[ERR] Falhou ao carregar sponza.obj");
        return -1;
    }

    // ── Camera ───────────────────────────────────────────

    Scene scene;

    Camera *camera = scene.createCamera("main");
    camera->setController(new FreeCameraController());
    FreeCameraController *freeCamCtrl = static_cast<FreeCameraController *>(camera->getController());
    freeCamCtrl->moveSpeed = 500.0f;
    camera->position = glm::vec3(0.0f, 150.0f, 0.0f);
    camera->lookAt({800.0f, 150.0f, 0.0f});
    camera->setFov(60.0f);
    camera->setViewPlanes(1.0f, 5000.0f);
    camera->setViewport(0, 0, SCREEN_W, SCREEN_H);

    ForwardTechnique forwardTechnique;
    scene.addTechnique(&forwardTechnique);

    MeshNode *sponzaNode = scene.createMeshNode("sponza", sponza);
    sponzaNode->passMask = RenderPassMask::Opaque;

    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 last = SDL_GetPerformanceCounter();
    float fpsTimer = 0.0f;
    int fpsCounter = 0;
    int fps = 0;
    Input &input = Input::instance();

    while (!input.shouldQuit())
    {
        last = now;
        now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();

        input.process();

        int w, h;
        SDL_GetWindowSize(window, &w, &h);

        camera->setViewport(0, 0, w, h);
        camera->update(dt);

        scene.render();

        // ── FPS + stats no título ─────────────────────────────────
        fpsCounter++;
        fpsTimer += dt;
        if (fpsTimer >= 1.0f)
        {
            fps = fpsCounter;
            fpsCounter = 0;
            fpsTimer -= 1.0f;

            const RenderStats &st = scene.stats();
            char title[128];
            snprintf(title, sizeof(title),
                     "Mini Pipeline  |  %d FPS  |  DC:%u  Tris:%u  Verts:%u  Objs:%u",
                     fps, st.drawCalls, st.triangles, st.vertices, st.objects);
            SDL_SetWindowTitle(window, title);
        }

        SDL_GL_SwapWindow(window);
    }

    materials.unloadAll();
    meshes.unloadAll();
    shaders.unloadAll();
    TextureManager::instance().unloadAll();
    scene.release();
    RenderState::instance().shutdown();

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
