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
    if (!device.Create(SCREEN_W, SCREEN_H, "minirender - mirror", true, 1))
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

    // ── Shaders ──────────────────────────────────────────────────────────────
    Shader *depthShader = shMgr.load("depth",
        "assets/shaders/depth.vert", "assets/shaders/depth.frag");
    Shader *litShader = shMgr.load("lit_shadow",
        "assets/shaders/lit_shadow.vert", "assets/shaders/lit_shadow.frag");
    Shader *unlitShader = shMgr.load("unlit",
        "assets/shaders/unlit.vert", "assets/shaders/unlit.frag");
    Shader *flatShader = shMgr.load("flat",
        "assets/shaders/flat.vert", "assets/shaders/flat.frag");

    if (!depthShader || !litShader || !unlitShader || !flatShader)
    {
        SDL_Log("[ERR] shader load failed");
        device.Close();
        return 1;
    }

    // ── Textures + materials ──────────────────────────────────────────────────
    Texture *white   = texMgr.getWhite();
    Texture *pattern = texMgr.getPattern();
    Texture *texWall = texMgr.load("wall", "assets/wall.jpg");

    Material *matGround = matMgr.create("ground");
    matGround->setShader(litShader)->setTexture("u_albedo", texWall ? texWall : white);

    Material *matCube = matMgr.create("cube");
    matCube->setShader(litShader)->setTexture("u_albedo", pattern ? pattern : white);

    // ── Scene ─────────────────────────────────────────────────────────────────
    Scene scene;

    Camera *cam = scene.createCamera("main");
    cam->fov       = 60.f;
    cam->nearPlane = 0.1f;
    cam->farPlane  = 500.f;
    cam->setPosition({0.f, 8.f, 22.f});
    cam->lookAt({0.f, 2.f, 0.f});
    cam->setAspect(SCREEN_W, SCREEN_H);
    cam->setViewport(0, 0, SCREEN_W, SCREEN_H);

    auto *freeCam = new FreeCameraController();
    freeCam->moveSpeed        = 20.f;
    freeCam->mouseSensitivity = 0.15f;
    cam->setController(freeCam);
    scene.setCurrentCamera(cam);

    // ── Meshes + nodes ────────────────────────────────────────────────────────
    Mesh *plane = meshMgr.create_plane("ground", 40.f, 40.f, 1);
    Mesh *cube  = meshMgr.create_cube("cube",    2.f);

    scene.createMeshNode("ground", plane)->setMaterial("ground");
    for (int i = 0; i < 5; i++)
    {
        auto *n = scene.createMeshNode("cube_" + std::to_string(i), cube);
        n->setMaterial("cube");
        n->setPosition({(float)(i - 2) * 6.f, 1.f, 0.f});
    }

    // ── Shadow map ────────────────────────────────────────────────────────────
    const int   SHADOW_SIZE = 2048;
    const float ORTHO_SIZE  = 40.f;
    const float LIGHT_DIST  = 100.f;
    const float SHADOW_BIAS = 0.005f;
    const glm::vec3 LIGHT_DIR   = glm::normalize(glm::vec3(1.f, 3.f, 1.f));
    const glm::vec3 LIGHT_COLOR = {1.f, 1.f, 0.95f};

    ShadowMap shadowMap;
    if (!shadowMap.create(SHADOW_SIZE))
    {
        SDL_Log("[ERR] shadow map failed");
        device.Close();
        return 1;
    }

    const glm::vec3 lightUp    = (glm::abs(glm::dot(LIGHT_DIR, {0,1,0})) > 0.99f)
                                 ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    const glm::mat4 lightSpace = glm::ortho(-ORTHO_SIZE, ORTHO_SIZE,
                                            -ORTHO_SIZE, ORTHO_SIZE,
                                             0.1f, LIGHT_DIST * 2.f)
                               * glm::lookAt(LIGHT_DIR * LIGHT_DIST,
                                             glm::vec3(0.f), lightUp);

    // ── Mirror setup ──────────────────────────────────────────────────────────
    // Mirror plane: vertical, at z = -14, facing +Z (toward the camera)
    const glm::vec3 MIRROR_POS    = {0.f, 5.f, -14.f};
    const float     MIRROR_W      = 14.f;
    const float     MIRROR_H      = 10.f;

    // Render target for the mirror reflection
    RenderTarget mirrorRT;
    mirrorRT.create(512, 512).addColor().addDepthRB();
    if (!mirrorRT.finalize())
    {
        SDL_Log("[ERR] mirror render target failed");
        device.Close();
        return 1;
    }

    // Material that displays the RT on the mirror surface (unlit — reflection
    // is already shaded when rendered into the RT)
    Material *matMirror = matMgr.create("mirror");
    matMirror->setShader(unlitShader)
             ->setTexture("u_albedo", mirrorRT.colorTex());

    // Vertical quad representing the mirror in the scene
    Mesh *mirrorMesh = meshMgr.create_plane("mirror", MIRROR_W, MIRROR_H, 1);
    auto *mirrorNode = scene.createMeshNode("mirror", mirrorMesh);
    mirrorNode->setMaterial("mirror");
    mirrorNode->setPosition(MIRROR_POS);
    mirrorNode->setEulerAngles({90.f, 0.f, 0.f}); // rotate horizontal plane → vertical
 

    // ── Main loop ─────────────────────────────────────────────────────────────
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
 

        // ── Pass 1: shadow depth ──────────────────────────────────────────────
        {
            shadowMap.bind();
            rs.setViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
            //glClear(GL_DEPTH_BUFFER_BIT);
            rs.clear(false, true);
            rs.setDepthTest(true);
            rs.setDepthWrite(true);
            rs.setCull(true);

            scene.gatherScene(cam);
            scene.drawShadowDepth(depthShader, lightSpace);

            shadowMap.unbind();
        }

        // ── Pass 2: reflection into mirror RT ────────────────────────────────
        {
            // Reflected camera: fixed position behind the mirror, looking at scene center
            Camera reflCam;
            reflCam.fov       = cam->fov;
            reflCam.nearPlane = cam->nearPlane;
            reflCam.farPlane  = cam->farPlane;
            reflCam.viewport  = {0, 0, 512, 512};
            reflCam.setAspect(512, 512);
            reflCam.position= cam->position;
            reflCam.rotation= cam->rotation;
            //reflCam.setPosition({0.f, 8.f, -22.f});
            //reflCam.lookAt({0.f, 2.f, 0.f});
            reflCam.updateMatrices();

            mirrorNode->visible = false;
            scene.gatherScene(&reflCam);
            mirrorNode->visible = true;

            mirrorRT.bind();
            mirrorRT.clear(true, true);
            rs.setDepthTest(true);
            rs.setDepthWrite(true);
            rs.setCull(true);
 

            rs.useProgram(litShader->getId());
            litShader->setMat4("u_view",       reflCam.view);
            litShader->setMat4("u_proj",       reflCam.projection);
            litShader->setVec4("u_cameraPos",  glm::vec4(reflCam.position, 1.f));
            litShader->setVec4("u_clipPlane",  glm::vec4(0.f));
            litShader->setMat4("u_lightSpace", lightSpace);
            litShader->setInt ("u_shadowMap",  1);
            litShader->setVec3("u_lightDir",   LIGHT_DIR);
            litShader->setVec3("u_lightColor", LIGHT_COLOR);
            litShader->setFloat("u_shadowBias", SHADOW_BIAS);
            rs.bindTexture(1, GL_TEXTURE_2D, shadowMap.depthTexId());

            scene.drawPass(litShader, RenderPassMask::Opaque,
                           RenderSortMode::FrontToBack);

            mirrorRT.unbind();
     
        }

        // ── Pass 3: main lit pass ─────────────────────────────────────────────
        {
            // Hide mirror so it doesn't go through the lit pass —
            // we'll draw it manually with the unlit shader afterwards.
            mirrorNode->visible = false;
            scene.gatherScene(cam);
            mirrorNode->visible = true;

            rs.setViewport(0, 0, W, H);
            rs.setClearColor(0.1f, 0.12f, 0.15f, 1.f);
            rs.clear(true, true);
            rs.setDepthTest(true);
            rs.setDepthWrite(true);
            rs.setCull(true);
            rs.setCullFace(GL_BACK);
            rs.setBlend(false);

            rs.useProgram(litShader->getId());
            litShader->setMat4("u_view",       cam->view);
            litShader->setMat4("u_proj",       cam->projection);
            litShader->setVec4("u_cameraPos",  glm::vec4(cam->position, 1.f));
            litShader->setVec4("u_clipPlane",  glm::vec4(0.f));
            litShader->setMat4("u_lightSpace", lightSpace);
            litShader->setInt ("u_shadowMap",  1);
            litShader->setVec3("u_lightDir",   LIGHT_DIR);
            litShader->setVec3("u_lightColor", LIGHT_COLOR);
            litShader->setFloat("u_shadowBias", SHADOW_BIAS);
            rs.bindTexture(1, GL_TEXTURE_2D, shadowMap.depthTexId());

            scene.drawPass(litShader, RenderPassMask::Opaque,
                           RenderSortMode::FrontToBack);
            scene.drawPass(litShader, RenderPassMask::Transparent,
                           RenderSortMode::BackToFront);

            // Mirror surface drawn with unlit shader (shows RT texture)
            rs.useProgram(unlitShader->getId());
            unlitShader->setMat4("u_view",  cam->view);
            unlitShader->setMat4("u_proj",  cam->projection);
            unlitShader->setMat4("u_model", mirrorNode->worldMatrix());
            matMirror->bindTextures();
            mirrorMesh->draw();
        }

       

        // ── HUD ───────────────────────────────────────────────────────────────
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
            font.Print(10, 10, "%d FPS  |  LMB: olhar  WASD/QE: mover", device.GetFPS());
            font.Print(10, 30, "Pos: %.1f %.1f %.1f",
                       cam->position.x, cam->position.y, cam->position.z);
            const auto &st = scene.stats();
            font.Print(10, 50, "DC:%u  Tris:%u", st.drawCalls, st.triangles);
            batch.Render();
        }

        device.Flip();
    }

    mirrorRT.destroy();
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
