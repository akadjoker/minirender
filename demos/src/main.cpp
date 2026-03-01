#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include "Core.hpp"
#include "Device.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "RenderPipeline.hpp"
#include "RenderState.hpp"
#include "Scene.hpp"
#include "DemoManager.hpp"
#include "DemoShadow.hpp"
#include "DemoDeferred.hpp"
#include "DemoSimples.hpp"
#include "DemoH3D.hpp"
#include "DemoSinbad.hpp"
#include "DemoEffects.hpp"
#include "DemoCascade.hpp"
#include "DemoCannonball.hpp"

extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libSDL2\n"
           "leak:SDL_DBus\n";
}

const int SCREEN_W = 1024;
const int SCREEN_H = 768;

// int main(int argc, char* argv[])
// {
//     SDL_Init(SDL_INIT_VIDEO);
//     IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
//     SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
//     SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

//     SDL_Window* window = SDL_CreateWindow(
//         "Mini Pipeline",
//         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
//         SCREEN_W, SCREEN_H,
//         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

//     SDL_GLContext ctx = SDL_GL_CreateContext(window);
//     SDL_GL_SetSwapInterval(1);

//     if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress)) {
//         SDL_Log("[GLAD] Failed");
//         return -1;
//     }

//     SDL_Log("[GL]  %s", glGetString(GL_VERSION));
//     SDL_Log("[GPU] %s", glGetString(GL_RENDERER));

//     auto& shaders = ShaderManager::instance();
//     auto &materials = MaterialManager::instance();
//     auto& meshes = MeshManager::instance();

//     DemoManager manager;
//     //manager.add(new DemoShadow());
//     //manager.add(new DemoDeferred());
//     //manager.add(new DemoSimples());
//     //manager.add(new DemoH3D());
//     manager.add(new DemoSinbad());

//     if (!manager.init())
//     {
//         SDL_Log("[ERR] Demo init falhou");

//         return -1;
//     }

//     Uint64 now  = SDL_GetPerformanceCounter();
//     Uint64 last = SDL_GetPerformanceCounter();
//     float  fpsTimer   = 0.0f;
//     int    fpsCounter = 0;
//     int    fps        = 0;
//     Input& input = Input::instance();

//       manager.onResize(SCREEN_W, SCREEN_H);

//     while (!input.shouldQuit())
//     {
//         last = now;
//         now  = SDL_GetPerformanceCounter();
//         float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();

//         input.process();

//         int w, h;
//         SDL_GetWindowSize(window, &w, &h);

//       if (!manager.processEvents(window))
//             break;

//         manager.onResize(w, h);
//         manager.update(dt);
//         manager.render();

//         // ── FPS + stats no título ─────────────────────────────────
//         fpsCounter++;
//         fpsTimer += dt;
//         if (fpsTimer >= 1.0f)
//         {
//             fps = fpsCounter;
//             fpsCounter = 0;
//             fpsTimer  -= 1.0f;

//             // const RenderStats &st = scene.stats();
//             // char title[128];
//             // snprintf(title, sizeof(title),
//             //     "Mini Pipeline  |  %d FPS  |  DC:%u  Tris:%u  Verts:%u  Objs:%u",
//             //     fps, st.drawCalls, st.triangles, st.vertices, st.objects);
//             // SDL_SetWindowTitle(window, title);
//         }

//         SDL_GL_SwapWindow(window);
//     }

//     manager.releaseAll();
//     materials.unloadAll();
//     meshes.unloadAll();
//     shaders.unloadAll();
//     TextureManager::instance().unloadAll();
//     RenderState::instance().shutdown();

//     SDL_GL_DeleteContext(ctx);
//     SDL_DestroyWindow(window);
//     IMG_Quit();
//     SDL_Quit();
//     return 0;
// }

int main()
{

    Device &device = Device::Instance();

    if (!device.Create(SCREEN_W, SCREEN_H, "Game", true, 1))
    {
        return 1;
    }

    auto &state = RenderState::instance();

    RenderBatch batch;
    batch.Init();

    state.setClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    Font font;
    font.SetBatch(&batch);
    font.LoadDefaultFont();

    DemoManager manager;
    // manager.add(new DemoShadow());
    // manager.add(new DemoDeferred());
    //manager.add(new DemoSimples());
     //manager.add(new DemoH3D());
    //manager.add(new DemoSinbad());
    //manager.add(new DemoEffects());
    manager.add(new DemoCascade());
   //manager.add(new DemoCannonball());

    
    if (!manager.init())
    {
        SDL_Log("[ERR] Demo init falhou");
        manager.releaseAll();
        font.Release();
        batch.Release();
        device.Close();
        return -1;
    }
    
    manager.onResize(SCREEN_W, SCREEN_H);
    
    while (device.Run())
    {
        
        float dt = device.GetFrameTime();
        
        Scene& scene = manager.getScene();
        const RenderStats &st = scene.stats();
        const Camera *cam = scene.currentCamera();
        state.clear(true, true);
        state.setViewport(0, 0, device.GetWidth(), device.GetHeight());

        state.setDepthTest(true);
        state.setBlend(false);
        state.setCull(true);

        if (device.IsResize())
        {
            int w = device.GetWidth();
            int h = device.GetHeight();
            manager.onResize(w, h);
        }
        manager.update(dt);
        manager.render();

        glm::mat4 viewProj = cam->getProjection() * cam->getView();
        batch.SetMatrix(viewProj);
        scene.debug(&batch);
        batch.Grid(10, 1.0f, true);
        batch.Render();

        //state.resetCache();

        state.setDepthTest(false);
        state.setBlend(true);
        state.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        state.setCull(false);

        glm::mat4 ortho = glm::ortho(0.0f, (float)device.GetWidth(), (float)device.GetHeight(), 0.0f, -1.0f, 1.0f);
        state.setViewport(0, 0, device.GetWidth(), device.GetHeight());
        batch.SetMatrix(ortho);

        font.SetColor(255, 255, 255);
        font.Print(10, 30, "%d FPS  |  DC:%u  Tris:%u  Verts:%u  Objs:%u",
        device.GetFPS(), st.drawCalls, st.triangles, st.vertices, st.objects);

        batch.Render();

        device.Flip();
    }
    manager.releaseAll();

    font.Release();
    batch.Release();
    device.Close();

    return 0;
}