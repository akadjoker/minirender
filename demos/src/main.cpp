#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

#include "Core.hpp"
#include "Device.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"
#include "Scene.hpp"
#include "Input.hpp"

extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libSDL2\n"
           "leak:SDL_DBus\n";
}

const int SCREEN_W = 1024;
const int SCREEN_H = 768;

int main()
{
    Device &device = Device::Instance();
    if (!device.Create(SCREEN_W, SCREEN_H, "minirender - flat test", true, 1))
        return 1;

    auto &rs    = RenderState::instance();
    auto &shMgr = ShaderManager::instance();

    RenderBatch batch;
    batch.Init();
    Font font;
    font.SetBatch(&batch);
    font.LoadDefaultFont();

    // Shader flat: so MVP + u_color
    Shader *flatShader = shMgr.load("flat",
        "assets/shaders/flat.vert", "assets/shaders/flat.frag");
    if (!flatShader)
    {
        SDL_Log("[ERR] flat shader falhou");
        device.Close();
        return 1;
    }

    // Cena + camera
    Scene scene;
    Camera *cam = scene.createCamera("main");
    cam->fov       = 60.f;
    cam->nearPlane = 0.1f;
    cam->farPlane  = 500.f;
    cam->setPosition({0.f, 5.f, 20.f});
    cam->lookAt({0.f, 0.f, 0.f});
    cam->setAspect(SCREEN_W, SCREEN_H);
    cam->setViewport(0, 0, SCREEN_W, SCREEN_H);

    auto *freeCam = new FreeCameraController();
    freeCam->moveSpeed        = 15.f;
    freeCam->mouseSensitivity = 0.15f;
    cam->setController(freeCam);
    scene.setCurrentCamera(cam);

    // Cubos — sem material, desenhamos directamente
    auto &meshMgr = MeshManager::instance();
    Mesh *cube = meshMgr.create_cube("cube", 2.f);
    Mesh *plane = meshMgr.create_plane("plane", 60.f, 60.f, 1);

    // Cores e posicoes
    struct CubeEntry { glm::mat4 model; glm::vec4 color; };
    std::vector<CubeEntry> cubes;

    // chao
    cubes.push_back({ glm::translate(glm::mat4(1.f), {0.f, -1.f, 0.f}), {0.3f,0.5f,0.3f,1.f} });

    // 5 cubos coloridos
    const glm::vec4 colors[] = {
        {1.f, 0.2f, 0.2f, 1.f},
        {0.2f, 1.f, 0.2f, 1.f},
        {0.2f, 0.4f, 1.f, 1.f},
        {1.f, 1.f, 0.2f, 1.f},
        {1.f, 0.4f, 1.f, 1.f},
    };
    for (int i = 0; i < 5; i++)
    {
        glm::mat4 m = glm::translate(glm::mat4(1.f), {(float)(i - 2) * 5.f, 1.f, 0.f});
        cubes.push_back({ m, colors[i] });
    }

    // Main loop
    while (device.Run())
    {
        const float dt = device.GetFrameTime();
        const int   W  = device.GetWidth();
        const int   H  = device.GetHeight();

        if (device.IsResize())
        {
            cam->setAspect(W, H);
            cam->setViewport(0, 0, W, H);
        }

        // Update camera
        scene.update(dt);
        cam->updateMatrices();

        // Clear
        rs.setViewport(0, 0, W, H);
        rs.setClearColor(0.1f, 0.12f, 0.18f, 1.f);
        rs.clear(true, true);
        rs.setDepthTest(true);
        rs.setDepthWrite(true);
        rs.setCull(true);
        rs.setCullFace(GL_BACK);
        rs.setBlend(false);

        // Bind shader + camera uniforms
        rs.useProgram(flatShader->getId());
        flatShader->setMat4("u_view", cam->view);
        flatShader->setMat4("u_proj", cam->projection);

        // Draw cubes
        for (const auto &c : cubes)
        {
            flatShader->setMat4("u_model", c.model);
            flatShader->setVec4("u_color",  c.color);
            cube->draw();
        }
        // Draw plane com modelo separado
        {
            flatShader->setMat4("u_model", glm::mat4(1.f));
            flatShader->setVec4("u_color",  glm::vec4(0.3f, 0.5f, 0.3f, 1.f));
            plane->draw();
        }

        // HUD
        batch.SetMatrix(cam->projection * cam->view);
        batch.Grid(10, 1.0f, true);
        batch.Render();

        const glm::mat4 ortho = glm::ortho(0.f, (float)W, (float)H, 0.f, -1.f, 1.f);
        batch.SetMatrix(ortho);
        rs.setDepthTest(false);
        rs.setBlend(true);
        rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        rs.setCull(false);

        font.SetColor(255, 255, 255);
        font.Print(10, 10, "%d FPS | RMB:olhar  WASD:mover  Shift:rapido",
                   device.GetFPS());
        font.Print(10, 30, "Pos: %.1f %.1f %.1f",
                   cam->position.x, cam->position.y, cam->position.z);
        batch.Render();

        device.Flip();
    }

    scene.release();
    shMgr.unloadAll();
    meshMgr.unloadAll();
    font.Release();
    batch.Release();
    device.Close();
    return 0;
}
