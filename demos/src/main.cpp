#include "DemoAnimation.hpp"
#include "Demo.hpp"
#include "DemoDetail.hpp"
#include "DemoSolid.hpp"
#include "DemoTerrain.hpp"
#include "DemoTerrainWater.hpp"
#include "DemoTexture.hpp"

#include <string>

enum DemoId
{
    DemoSolidId = 0,
    DemoTextureId,
    DemoDetailId,
    DemoTerrainId,
    DemoTerrainWaterId,
    DemoAnimationId
};

static Demo *createDemoById(int demoId)
{
    switch (demoId)
    {
    case DemoTextureId:
        return new DemoTexture();
    case DemoDetailId:
        return new DemoDetail();
    case DemoTerrainId:
        return new DemoTerrain();
    case DemoTerrainWaterId:
        return new DemoTerrainWater();
    case DemoAnimationId:
        return new DemoAnimation();
    case DemoSolidId:
    default:
        return new DemoSolid();
    }
}

int main()
{
    const int demoId = DemoId::DemoTerrainWaterId;
    Device &device = Device::Instance();
    if (!device.Create(1280, 720, "MiniRender V2", true, 1))
        return 1;
    device.ImGuiInit();

    Demo *demo = createDemoById(demoId);
    Scene scene;
    Renderer renderer;

    demo->build(scene, device);
    Camera *camera = demo->camera();
    if (!camera)
    {
        device.Close();
        return 1;
    }

    while (device.Run())
    {
        if (device.IsResize())
        {
            camera->setAspect(device.GetWidth(), device.GetHeight());
            camera->setViewport(0, 0, device.GetWidth(), device.GetHeight());
        }

        const float dt = device.GetFrameTime();
        camera->update(dt);
        demo->update(dt);
        scene.update(dt);
        device.ImGuiBegin();
        demo->drawImGui(renderer);
        renderer.render(scene, camera);
        device.ImGuiEnd();
        device.Flip();
    }

    scene.release();
    delete demo;
    device.Close();
    return 0;
}
