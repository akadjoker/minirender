#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
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
#include "ShadowMap.hpp"
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
    if (!device.Create(SCREEN_W, SCREEN_H, "minirender", true, 1))
        return 1;

    auto &rs      = RenderState::instance();
    auto &shMgr   = ShaderManager::instance();
    auto &texMgr  = TextureManager::instance();
    auto &matMgr  = MaterialManager::instance();
    auto &meshMgr = MeshManager::instance();

    RenderBatch batch;
    batch.Init();

    Font font;
    font.SetBatch(&batch);
    font.LoadDefaultFont();

    // Shaders
    Shader *depthShader = shMgr.load("depth",
        "assets/shaders/depth.vert", "assets/shaders/depth.frag");
    Shader *litShader = shMgr.load("lit_shadow",
        "assets/shaders/lit_shadow.vert", "assets/shaders/lit_shadow.frag");

    if (!depthShader || !litShader)
    {
        SDL_Log("[ERR] Failed to load shaders");
        device.Close();
        return 1;
    }

    // Textures
    Texture *white   = texMgr.getWhite();
    Texture *texWall = texMgr.load("wall", "assets/wall.jpg");

    // Materials
    Material *matGround = matMgr.create("ground");
    matGround->setShader(litShader)->setTexture("u_albedo", texWall ? texWall : white);

    Material *matCube = matMgr.create("cube");
    matCube->setShader(litShader)->setTexture("u_albedo", white);

    // Scene + camera
    Scene scene;

    Camera *cam = scene.createCamera("main");
    cam->fov       = 60.f;
    cam->nearPlane = 0.1f;
    cam->farPlane  = 500.f;
    cam->setPosition({0.f, 15.f, 30.f});
    cam->lookAt({0.f, 0.f, 0.f});
    cam->setAspect(SCREEN_W, SCREEN_H);
    cam->setViewport(0, 0, SCREEN_W, SCREEN_H);

    auto *freeCam = new FreeCameraController();
    freeCam->moveSpeed        = 20.f;
    freeCam->mouseSensitivity = 0.15f;
    cam->setController(freeCam);
    scene.setCurrentCamera(cam);

    // Nodes
    Mesh *plane = meshMgr.create_plane("ground", 40.f, 40.f, 1);
    Mesh *cube  = meshMgr.create_cube("cube", 2.f);

    scene.createMeshNode("ground", plane)->setMaterial("ground");

    for (int i = 0; i < 5; i++)
    {
        auto *node = scene.createMeshNode("cube_" + std::to_string(i), cube);
        node->setMaterial("cube");
        node->setPosition({(float)(i - 2) * 5.f, 1.f, 0.f});
    }

    // Shadow map
    const int   SHADOW_SIZE  = 2048;
    const float ORTHO_SIZE   = 40.f;
    const float LIGHT_DIST   = 100.f;
    const float SHADOW_BIAS  = 0.005f;
    const glm::vec3 LIGHT_DIR   = glm::normalize(glm::vec3(1.f, 3.f, 1.f));
    const glm::vec3 LIGHT_COLOR = {1.f, 1.f, 0.95f};

    ShadowMap shadowMap;
    if (!shadowMap.create(SHADOW_SIZE))
    {
        SDL_Log("[ERR] Failed to create shadow map");
        device.Close();
        return 1;
    }

    const glm::vec3 lightUp   = (glm::abs(glm::dot(LIGHT_DIR, {0,1,0})) > 0.99f)
                                ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    const glm::mat4 lightView = glm::lookAt(LIGHT_DIR * LIGHT_DIST,
                                            glm::vec3(0.f), lightUp);
    const glm::mat4 lightProj = glm::ortho(-ORTHO_SIZE, ORTHO_SIZE,
                                           -ORTHO_SIZE, ORTHO_SIZE,
                                            0.1f, LIGHT_DIST * 2.f);
    const glm::mat4 lightSpace = lightProj * lightView;

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

        scene.update(dt);

        // Gather
        scene.gatherScene(cam);

        // Pass 1: shadow depth
        {
            shadowMap.bind();
            rs.setViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
            glClear(GL_DEPTH_BUFFER_BIT);
            rs.setDepthTest(true);
            rs.setDepthWrite(true);
            rs.setCull(true);
   

            scene.drawShadowDepth(depthShader, lightSpace);

         
            shadowMap.unbind();
        }

        // Pass 2: main lit pass
        {
            rs.setViewport(0, 0, W, H);
            rs.setClearColor(0.1f, 0.12f, 0.15f, 1.f);
            rs.clear(true, true);
            rs.setDepthTest(true);
            rs.setDepthWrite(true);
            rs.setCull(true);
            rs.setCullFace(GL_BACK);
            rs.setBlend(false);

            rs.useProgram(litShader->getId());
            litShader->setMat4("u_view",      cam->view);
            litShader->setMat4("u_proj",      cam->projection);
            litShader->setVec4("u_cameraPos", glm::vec4(cam->position, 1.f));
            litShader->setVec4("u_clipPlane", glm::vec4(0.f));
            litShader->setMat4("u_lightSpace",  lightSpace);
            litShader->setInt ("u_shadowMap",   1);
            litShader->setVec3("u_lightDir",    LIGHT_DIR);
            litShader->setVec3("u_lightColor",  LIGHT_COLOR);
            litShader->setFloat("u_shadowBias", SHADOW_BIAS);
            rs.bindTexture(1, GL_TEXTURE_2D, shadowMap.depthTexId());

            scene.drawPass(litShader, RenderPassMask::Opaque,
                           RenderSortMode::FrontToBack);
            scene.drawPass(litShader, RenderPassMask::Transparent,
                           RenderSortMode::BackToFront);
        }

        // HUD
        {
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
            font.Print(10, 10, "%d FPS  |  RMB: olhar  WASD/QE: mover",
                       device.GetFPS());
            font.Print(10, 30, "Pos: %.1f %.1f %.1f",
                       cam->position.x, cam->position.y, cam->position.z);
            const auto &st = scene.stats();
            font.Print(10, 50, "DC:%u  Tris:%u  Verts:%u",
                       st.drawCalls, st.triangles, st.vertices);
            batch.Render();
        }

        device.Flip();
    }

    // Cleanup
    shadowMap.destroy();
    scene.release();
    shMgr.unloadAll();
    matMgr.unloadAll();
    meshMgr.unloadAll();
    texMgr.unloadAll();
    font.Release();
    batch.Release();
    device.Close();

    return 0;
}
