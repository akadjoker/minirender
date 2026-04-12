#include "Device.hpp"
#include "RenderState.hpp"
#include "SpriteGeneratorApp.hpp"
#include "imgui.h"

int main()
{
    Device& device = Device::Instance();
    if (!device.Create(1440, 900, "MiniRender Sprite Generator", true,1))
        return 1;

    device.ImGuiInit();
    SpriteGeneratorApp app;

    while (device.Run())
    {
        RenderState& rs = RenderState::instance();
        rs.setViewport(0, 0, device.GetWidth(), device.GetHeight());
        rs.setClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        rs.clear(true, true);

        device.ImGuiBegin();
        app.RenderFrame(device.GetFrameTime());

        device.ImGuiEnd();
        device.Flip();
    }

    device.Close();
    return 0;
}
