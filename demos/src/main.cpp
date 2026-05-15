#include <string>
#include <vector>

#include "DemoRegistry.hpp"
#include "Device.hpp"
#include <WidgetApp.hpp>
#include <ViewWidgets.hpp>
#include <BasicWidgets.hpp>
#include <ComboBox.hpp>

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

    device.BuGUIInit();

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

    // ── Selector FloatWindow (created once, retained) ─────────────────────
    auto& wapp = BuGUI::WidgetApp::instance();
    auto* selectorFW = wapp.addFloat<BuGUI::FloatWindow>("Demo Selector");
    selectorFW->setFloatPos((float)device.GetWidth() - 316.0f, 16.0f);
    selectorFW->setFloatSize(300.0f, 180.0f);
    selectorFW->setClosable(false);
    selectorFW->setResizable(false);

    auto* vbox = selectorFW->setContent<BuGUI::BoxLayout>(BuGUI::LayoutDir::Vertical);
    vbox->setSpacing(4.0f);
    vbox->setPadding(4.0f);

    auto* demoCombo = vbox->createChild<BuGUI::ComboBox>();
    for (const auto& d : demos)
        demoCombo->addItem(d.name);
    demoCombo->setSelectedIndex(activeDemoIndex);
    demoCombo->selectionChanged.connect([&](int idx) {
        pendingDemoIndex = idx;
        switchDemo = true;
    });

    auto* descLabel  = vbox->createChild<BuGUI::Label>(activeDemo ? activeDemo->description() : "");
    auto* errorLabel = vbox->createChild<BuGUI::Label>("");
    errorLabel->setColor(BuGUI::Color(255, 90, 90, 255));
    auto* hintLabel  = vbox->createChild<BuGUI::Label>(
        "Adicione demos em demos/src/demos\ne registe em DemoRegistry.hpp.");
    hintLabel->setColor(BuGUI::Color(160, 160, 160, 255));
    (void)hintLabel;

    while (device.Run())
    {
        const float dt = device.GetFrameTime();

        device.BuGUIBegin();  // WidgetApp::update(io) — define se GUI quer o mouse
        // a partir daqui Input::IsMouseDown/Delta/Wheel bloqueiam se cursor está sobre GUI

        if (activeDemo)
            activeDemo->update(dt);

        // Update dynamic labels each frame
        descLabel->setText(activeDemo ? activeDemo->description() : "");
        errorLabel->setText(demoError);

        if (activeDemo)
            activeDemo->drawGui();

        device.BuGUIEnd();  // WidgetApp::paint() + BuGUI::Render()

        if (activeDemo)
            activeDemo->render();

        device.Flip();

        if (switchDemo)
        {
            destroyActiveDemo(activeDemo);
            activeDemo = createActiveDemo(demos[pendingDemoIndex], device, demoError);
            activeDemoIndex = pendingDemoIndex;
            switchDemo = false;
            demoCombo->setSelectedIndex(activeDemoIndex);
        }
    }

    destroyActiveDemo(activeDemo);
    device.Close();
    return 0;
}
