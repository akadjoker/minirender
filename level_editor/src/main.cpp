#include "Device.hpp"
#include "LevelEditorApp.hpp"
#include "RenderState.hpp"

int main()
{
    Device& device = Device::Instance();
    if (!device.Create(1600, 920, "MiniRender Level Editor", true, 1))
        return 1;

    device.ImGuiInit();
    LevelEditorApp app;

    while (device.Run())
    {
        RenderState& rs = RenderState::instance();
        rs.setViewport(0, 0, device.GetWidth(), device.GetHeight());
        rs.setClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        rs.clear(true, true);

        device.ImGuiBegin();
        app.RenderFrame(device.GetFrameTime());
        device.ImGuiEnd();
        device.Flip();
    }

    device.Close();
    return 0;
}
