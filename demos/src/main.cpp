#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "Opengl.hpp"
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
#include "DemoMD2.hpp"
#include "DemoSinbad.hpp"
#include "DemoEffects.hpp"
#include "DemoCascade.hpp"
#include "DemoCannonball.hpp"
#include "DemoWater.hpp"
#include "DemoTerrainLod.hpp"
#include "DemoPerformance.hpp"
#include "DemoBatch.hpp"
#include "DemoSponzaCSM.hpp"
#include "DemoInstanceCSM.hpp"
#include "DemoVertexArray.hpp"
#include "Input.hpp"

extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libSDL2\n"
           "leak:SDL_DBus\n";
}

const int SCREEN_W = 1824;
const int SCREEN_H = 968;

int main()
{

    Device &device = Device::Instance();

    if (!device.Create(SCREEN_W, SCREEN_H, "Game", true, 1))
    {
        return 1;
    }

    device.ImGuiInit("#version 300 es");

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
    //manager.add(new DemoMD2());
    //manager.add(new DemoSinbad());
    //manager.add(new DemoEffects());
    //manager.add(new DemoBatch());
    //manager.add(new DemoCascade());
    manager.add(new DemoCannonball());
    //manager.add(new DemoSponzaCSM());
    //manager.add(new DemoWater());
    //manager.add(new DemoTerrainLod());
 //   manager.add(new DemoPerformance());
    // manager.add(new DemoInstanceCSM());
    //manager.add(new DemoVertexArray());
   
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

        // Switch demo with Tab key
        static bool tabWasDown = false;
        bool tabDown = Input::IsKeyDown(KEY_TAB);
        if (tabDown && !tabWasDown) manager.switchNext();
        tabWasDown = tabDown;

        state.clear(true, true);
        state.setViewport(0, 0, device.GetWidth(), device.GetHeight());

        device.ImGuiBegin();

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

        // ImGui (and the Batch system) change GL state outside RenderState,
        // so invalidate the cache before the scene pipeline runs.
        state.resetCache();

        manager.render();

        // Re-acquire scene/cam after potential demo switch
        Scene        &scene    = manager.getScene();
        const RenderStats &st  = scene.stats();
        const Camera      *cam = scene.currentCamera();

        glm::mat4 viewProj = cam->getProjection() * cam->getView();
        batch.SetMatrix(viewProj);
  //      scene.debug(&batch);
        batch.Grid(10, 1.0f, true);
        batch.Render();

        state.setDepthTest(false);
        state.setBlend(true);
        state.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        state.setCull(false);

        glm::mat4 ortho = glm::ortho(0.0f, (float)device.GetWidth(),
                                     (float)device.GetHeight(), 0.0f, -1.0f, 1.0f);
        state.setViewport(0, 0, device.GetWidth(), device.GetHeight());
        batch.SetMatrix(ortho);

        font.SetColor(255, 255, 255);
        font.Print(10, 30, "[%s]  %d FPS  |  DC:%u  Tris:%u  Verts:%u",
            manager.currentName(),
            device.GetFPS(), st.drawCalls, st.triangles, st.vertices);
        font.Print(10, 50, "SH:%u  MAT:%u  TX:%u  OBJ:%u",
            st.shaderChanges, st.materialChanges, st.textureBinds, st.objects);
        font.Print(10, 70, "[Tab] switch demo");

        if (auto *sinbad = dynamic_cast<DemoSinbad *>(manager.currentDemo()))
        {
            const std::string queued = sinbad->queuedUpperState().empty()
                ? "-" : sinbad->queuedUpperState();
            font.SetColor(120, 255, 120);
            font.Print(10, 92, "LOC:%s  desired:%s",
                       sinbad->locomotionState().c_str(),
                       sinbad->desiredGroundState().c_str());
            font.Print(10, 112, "UPR:%s  queue:%s  sup:%s",
                       sinbad->upperState().c_str(),
                       queued.c_str(),
                       sinbad->isUpperSuppressed() ? "yes" : "no");
            font.SetColor(255, 255, 255);
        }

        batch.Render();

        device.Flip();
    }
    manager.releaseAll();

    font.Release();
    batch.Release();
    device.Close();

    return 0;
}