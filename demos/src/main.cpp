#include <string>
#include <vector>

#include "DemoRegistry.hpp"
#include "Device.hpp"
#include "imgui.h"

extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libSDL2\n"
           "leak:SDL_DBus\n";
}

namespace
{

IDemo *createActiveDemo(const DemoEntry &entry, Device &device, std::string &errorMessage)
{
    IDemo *demo = entry.create();
    if (!demo)
    {
        errorMessage = "Nao foi possivel criar a demo selecionada.";
        return nullptr;
    }

    if (!demo->setup(device))
    {
        errorMessage = std::string("Falha ao iniciar a demo: ") + entry.name;
        delete demo;
        return nullptr;
    }

    errorMessage.clear();
    return demo;
}

void destroyActiveDemo(IDemo *&demo)
{
    if (!demo)
        return;

    demo->shutdown();
    delete demo;
    demo = nullptr;
}

} // namespace

int main()
{
    Device &device = Device::Instance();
    if (!device.Create(1280, 720, "MiniRender Scene Demo", true))
        return 1;

    device.ImGuiInit();

    const std::vector<DemoEntry> &demos = getDemoRegistry();
    if (demos.empty())
    {
        device.Close();
        return 1;
    }

    int activeDemoIndex = 0;
    int pendingDemoIndex = 0;
    bool switchDemo = false;
    std::string demoError;
    IDemo *activeDemo = createActiveDemo(demos[activeDemoIndex], device, demoError);

    while (device.Run())
    {
        const float dt = device.GetFrameTime();
        if (activeDemo)
            activeDemo->update(dt);

        device.ImGuiBegin();

        ImGui::SetNextWindowPos(ImVec2((float)device.GetWidth() - 316.0f, 16.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_Always);
        if (ImGui::Begin("Demo Setup"))
        {
            const char *previewValue = demos[activeDemoIndex].name;
            if (activeDemo)
                previewValue = activeDemo->title();

            if (ImGui::BeginCombo("Demo", previewValue))
            {
                for (int i = 0; i < (int)demos.size(); ++i)
                {
                    const bool isSelected = (i == activeDemoIndex);
                    if (ImGui::Selectable(demos[i].name, isSelected))
                    {
                        pendingDemoIndex = i;
                        switchDemo = (pendingDemoIndex != activeDemoIndex);
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (activeDemo)
                ImGui::TextWrapped("%s", activeDemo->description());

            if (!demoError.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", demoError.c_str());

            ImGui::TextDisabled("Adicione uma nova demo em demos/src/demos e registre em DemoRegistry.hpp.");
        }
        ImGui::End();

        if (activeDemo)
            activeDemo->drawGui();

        device.ImGuiEnd();

        if (activeDemo)
            activeDemo->render();

        device.Flip();

        if (switchDemo)
        {
            destroyActiveDemo(activeDemo);
            activeDemo = createActiveDemo(demos[pendingDemoIndex], device, demoError);
            activeDemoIndex = pendingDemoIndex;
            switchDemo = false;
        }
    }

    destroyActiveDemo(activeDemo);
    device.Close();
    return 0;
}
