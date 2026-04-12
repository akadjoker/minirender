#include "SpriteGeneratorApp.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>

#include "Material.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <json.hpp>

namespace
{

ImGuiWindowFlags fixedPanelFlags()
{
    return ImGuiWindowFlags_NoResize |
           ImGuiWindowFlags_NoMove |
           ImGuiWindowFlags_NoCollapse |
           ImGuiWindowFlags_NoSavedSettings;
}

const char* previewViewLabel(SpritePreviewViewMode mode)
{
    switch (mode)
    {
    case SpritePreviewViewMode::Front: return "Front";
    case SpritePreviewViewMode::Side: return "Side";
    case SpritePreviewViewMode::Top: return "Top";
    case SpritePreviewViewMode::Custom: return "Custom";
    }
    return "Front";
}

void drawPreviewTile(const ImVec2& pos,
                     const ImVec2& size,
                     const char* label,
                     const char* subtitle,
                     bool selected)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 border = selected ? IM_COL32(110, 180, 240, 255) : IM_COL32(76, 88, 106, 255);

    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), border, 6.0f, 0, selected ? 2.0f : 1.0f);

    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + 54.0f), IM_COL32(12, 16, 22, 180), 6.0f);
    drawList->AddText(ImVec2(pos.x + 14.0f, pos.y + 12.0f), IM_COL32(225, 232, 240, 255), label);
    drawList->AddText(ImVec2(pos.x + 14.0f, pos.y + 32.0f), IM_COL32(150, 165, 180, 255), subtitle);
}

} // namespace

SpriteGeneratorApp::SpriteGeneratorApp()
{
    attachmentBone = "tag_weapon";
    applySpriteTheme(currentTheme);
    AppendLog("[ui] sprite generator shell ready");
    AppendLog("[ui] theme Studio");
    LoadProjectSettings();
}

void SpriteGeneratorApp::RenderFrame(float dt)
{
    previewRenderer_.update(project_, dt);

    ShowMenuBar();
    ShowFileDialog();
    if (showProjectPanel)
        ShowProjectPanel();
    if (showViewsPanel)
        ShowViewsPanel();
    if (showAnimationPanel)
        ShowAnimationPanel();
    if (showSurfacesPanel)
        ShowSurfacesPanel();
    if (showAttachmentsPanel)
        ShowAttachmentsPanel();
    if (showRenderPanel)
        ShowRenderPanel();
    if (showExportPanel)
        ShowExportPanel();
    if (showPreviewPanelWindow)
        ShowPreviewPanel();
    if (showLogPanelWindow)
        ShowLogPanel();
}

void SpriteGeneratorApp::ShowMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        ImGui::MenuItem("New Project", nullptr, false, false);
        ImGui::MenuItem("Open...", nullptr, false, false);
        if (ImGui::MenuItem("Save", "Ctrl+S"))
        {
            SaveProjectSettings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Load Model..."))
        {
            fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, ".");
        }
        if (ImGui::MenuItem("Load Weapon..."))
        {
            fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, ".");
        }
        ImGui::Separator();
        ImGui::MenuItem("Export", nullptr, false, false);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Project", nullptr, &showProjectPanel);
        ImGui::MenuItem("Preview", nullptr, &showPreviewPanelWindow);
        ImGui::MenuItem("Views", nullptr, &showViewsPanel);
        ImGui::MenuItem("Animation", nullptr, &showAnimationPanel);
        ImGui::MenuItem("Surfaces", nullptr, &showSurfacesPanel);
        ImGui::MenuItem("Attachments", nullptr, &showAttachmentsPanel);
        ImGui::MenuItem("Render", nullptr, &showRenderPanel);
        ImGui::MenuItem("Export", nullptr, &showExportPanel);
        ImGui::MenuItem("Log", nullptr, &showLogPanelWindow);
        ImGui::Separator();
        ImGui::MenuItem("Quad View", nullptr, &previewQuadView);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Theme"))
    {
        const SpriteTheme themes[] = {SpriteTheme::Dark, SpriteTheme::Light, SpriteTheme::Classic, SpriteTheme::Studio};
        for (SpriteTheme theme : themes)
        {
            if (ImGui::MenuItem(spriteThemeName(theme), nullptr, currentTheme == theme))
            {
                currentTheme = theme;
                applySpriteTheme(currentTheme);
                AppendLog(std::string("[ui] theme ") + spriteThemeName(currentTheme));
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        ImGui::TextUnformatted("Sprite generator shell");
        ImGui::Separator();
        ImGui::TextUnformatted("Next step: load animated model");
        ImGui::TextUnformatted("Then wire preview + export pipeline.");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void SpriteGeneratorApp::ShowFileDialog()
{
    if (fileDialog_.HasResult())
    {
        auto result = fileDialog_.ConsumeResult();
        if (result.accepted && result.mode == ImGuiFileDialog::Mode::OpenFile)
        {
            std::string path = result.path.string();
            project_.modelPath = path;

            std::string message;
            if (previewRenderer_.loadModel(project_, &message))
                AppendLog("[model] " + message);
            else
                AppendLog("[error] " + message);

            SaveProjectSettings();
        }
    }

    fileDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
}

void SpriteGeneratorApp::ShowProjectPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 12.0f, viewport->Pos.y + 36.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 220.0f), ImGuiCond_Always);
    ImGui::Begin("Project", nullptr, fixedPanelFlags());
    ImGui::InputText("Project Name", &project_.projectName);
    ImGui::TextUnformatted("Model:");
    ImGui::TextWrapped("%s", project_.modelPath.empty() ? "(none)" : project_.modelPath.c_str());
    if (ImGui::Button("Load Model..."))
    {
        fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, ".");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        std::string message;
        if (previewRenderer_.reloadModel(project_, &message))
            AppendLog("[model] " + message);
        else
            AppendLog("[error] " + message);
    }
    ImGui::InputText("Output", &project_.outputPath);
    ImGui::Separator();
    ImGui::Text("Status: %s", previewRenderer_.statusText().empty() ? "No model loaded" : previewRenderer_.statusText().c_str());
    if (previewRenderer_.hasLoadedModel())
        ImGui::TextWrapped("Loaded path: %s", previewRenderer_.loadedPath().c_str());
    ImGui::End();
}

void SpriteGeneratorApp::ShowViewsPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 12.0f, viewport->Pos.y + 268.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 230.0f), ImGuiCond_Always);
    ImGui::Begin("Views", nullptr, fixedPanelFlags());
    if (ImGui::RadioButton("Front", activePreviewView == SpritePreviewViewMode::Front))
        activePreviewView = SpritePreviewViewMode::Front;
    ImGui::SameLine();
    if (ImGui::RadioButton("Side", activePreviewView == SpritePreviewViewMode::Side))
        activePreviewView = SpritePreviewViewMode::Side;
    ImGui::SameLine();
    if (ImGui::RadioButton("Top", activePreviewView == SpritePreviewViewMode::Top))
        activePreviewView = SpritePreviewViewMode::Top;
    ImGui::SameLine();
    if (ImGui::RadioButton("Custom", activePreviewView == SpritePreviewViewMode::Custom))
        activePreviewView = SpritePreviewViewMode::Custom;
    ImGui::Checkbox("Quad View", &previewQuadView);
    ImGui::Separator();
    ImGui::Checkbox("Enable Front Export", &project_.viewFront);
    ImGui::Checkbox("Enable Side Export", &project_.viewSide);
    ImGui::Checkbox("Enable Top Export", &project_.viewTop);
    ImGui::Checkbox("Enable Custom Export", &project_.viewCustom);
    ImGui::Separator();
    ImGui::SliderFloat("Yaw", &project_.previewYaw, -180.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat("Pitch", &project_.previewPitch, -90.0f, 90.0f, "%.0f deg");
    ImGui::SliderFloat("Zoom", &project_.previewZoom, 0.1f, 4.0f, "%.2f x");
    ImGui::End();
}

void SpriteGeneratorApp::ShowAnimationPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 12.0f, viewport->Pos.y + 510.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 280.0f), ImGuiCond_Always);
    ImGui::Begin("Animation", nullptr, fixedPanelFlags());
    const std::vector<std::string>& animationNames = previewRenderer_.animationNames();
    if (!animationNames.empty())
    {
        int currentIndex = 0;
        for (int i = 0; i < static_cast<int>(animationNames.size()); ++i)
        {
            if (animationNames[i] == project_.animationName)
            {
                currentIndex = i;
                break;
            }
        }

        if (ImGui::BeginCombo("Clip", animationNames[currentIndex].c_str()))
        {
            for (int i = 0; i < static_cast<int>(animationNames.size()); ++i)
            {
                const bool selected = (i == currentIndex);
                if (ImGui::Selectable(animationNames[i].c_str(), selected))
                {
                    project_.animationName = animationNames[i];
                    project_.frameStart = 0;
                    project_.frameEnd = previewRenderer_.animationFrameMax(project_.animationName);
                    project_.currentFrame = 0.0f;
                    AppendLog("[anim] clip " + project_.animationName);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::TextDisabled("No animation clips available");
    }

    const int maxFrame = previewRenderer_.animationFrameMax(project_.animationName);
    ImGui::Checkbox("Play", &project_.animationPlaying);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &project_.animationLoop);
    ImGui::SliderFloat("FPS", &project_.animationFps, 1.0f, 60.0f, "%.1f");
    ImGui::SliderInt("Start", &project_.frameStart, 0, maxFrame);
    ImGui::SliderInt("End", &project_.frameEnd, 0, maxFrame);
    ImGui::SliderFloat("Current", &project_.currentFrame, 0.0f, static_cast<float>(std::max(0, maxFrame)), "%.0f");

    ImGui::Separator();
    ImGui::TextUnformatted("Transform Channels:");
    ImGui::Checkbox("Position##tc", &project_.usePositionChannel);
    ImGui::SameLine();
    ImGui::Checkbox("Rotation##tc", &project_.useRotationChannel);
    ImGui::SameLine();
    ImGui::Checkbox("Scale##tc", &project_.useScaleChannel);

    ImGui::End();
}

void SpriteGeneratorApp::ShowSurfacesPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 352.0f, viewport->Pos.y + 36.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 220.0f), ImGuiCond_Always);
    ImGui::Begin("Surfaces", nullptr, fixedPanelFlags());
    const char* surfaces[] = {"Body", "Head", "Weapon"};
    ImGui::ListBox("Surface", &selectedSurface, surfaces, IM_ARRAYSIZE(surfaces), 5);
    ImGui::Separator();
    ImGui::TextUnformatted("Texture override");
    ImGui::Button("Pick Texture");
    ImGui::SameLine();
    ImGui::Button("Reset");
    ImGui::Checkbox("Visible", &surfaceVisible);
    ImGui::End();
}

void SpriteGeneratorApp::ShowAttachmentsPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 352.0f, viewport->Pos.y + 650.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 180.0f), ImGuiCond_Always);
    ImGui::Begin("Attachments", nullptr, fixedPanelFlags());
    const char* attachments[] = {"Primary Weapon", "Offhand", "Muzzle FX"};
    ImGui::ListBox("Slot", &selectedAttachment, attachments, IM_ARRAYSIZE(attachments), 4);
    ImGui::Checkbox("Enabled", &attachmentEnabled);
    ImGui::InputText("Bone / Tag", &attachmentBone);
    ImGui::Separator();
    ImGui::TextUnformatted("Attachment transforms");
    ImGui::TextDisabled("Primary and secondary transform gizmos come next.");
    ImGui::End();
}

void SpriteGeneratorApp::ShowRenderPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 352.0f, viewport->Pos.y + 36.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 380.0f), ImGuiCond_Always);
    ImGui::Begin("Render", nullptr, fixedPanelFlags());

    ImGui::TextUnformatted("Background:");
    ImGui::ColorEdit4("Clear Color", &project_.clearColor.x);

    ImGui::Separator();
    ImGui::TextUnformatted("Model Transform:");

    ImGui::TextUnformatted("Position:");
    ImGui::SliderFloat("Pos X##model", &project_.modelPosition.x, -5.0f, 5.0f);
    ImGui::SliderFloat("Pos Y##model", &project_.modelPosition.y, -5.0f, 5.0f);
    ImGui::SliderFloat("Pos Z##model", &project_.modelPosition.z, -5.0f, 5.0f);

    ImGui::TextUnformatted("Rotation (degrees):");
    ImGui::SliderFloat("Rot X##model", &project_.modelRotation.x, -180.0f, 180.0f, "%.0f");
    ImGui::SliderFloat("Rot Y##model", &project_.modelRotation.y, -180.0f, 180.0f, "%.0f");
    ImGui::SliderFloat("Rot Z##model", &project_.modelRotation.z, -180.0f, 180.0f, "%.0f");

    ImGui::TextUnformatted("Scale:");
    ImGui::SliderFloat("Scale X##model", &project_.modelScale.x, 0.1f, 5.0f, "%.2f x");
    ImGui::SliderFloat("Scale Y##model", &project_.modelScale.y, 0.1f, 5.0f, "%.2f x");
    ImGui::SliderFloat("Scale Z##model", &project_.modelScale.z, 0.1f, 5.0f, "%.2f x");

    ImGui::End();
}

void SpriteGeneratorApp::ShowExportPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 352.0f, viewport->Pos.y + 420.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 220.0f), ImGuiCond_Always);
    ImGui::Begin("Export", nullptr, fixedPanelFlags());
    ImGui::InputInt("Width", &project_.spriteWidth);
    ImGui::InputInt("Height", &project_.spriteHeight);
    ImGui::Checkbox("Transparent Background", &project_.transparentBackground);
    ImGui::Checkbox("Export Frames", &project_.exportFrames);
    ImGui::Checkbox("Export Atlas", &project_.exportAtlas);
    ImGui::Checkbox("Export JSON", &project_.exportJson);
    ImGui::Checkbox("Trim Output", &project_.trimOutput);
    ImGui::Separator();
    ImGui::Button("Preview Export");
    ImGui::SameLine();
    ImGui::Button("Export");
    ImGui::End();
}

void SpriteGeneratorApp::ShowPreviewPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float leftWidth = (showProjectPanel || showViewsPanel || showAnimationPanel) ? 364.0f : 12.0f;
    const float rightWidth = (showSurfacesPanel || showAttachmentsPanel || showExportPanel) ? 364.0f : 12.0f;
    const float topOffset = 36.0f;
    const float bottomLogHeight = showLogPanelWindow ? 196.0f : 12.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + leftWidth, viewport->Pos.y + topOffset), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(viewport->Size.x - leftWidth - rightWidth, viewport->Size.y - topOffset - bottomLogHeight),
        ImGuiCond_Always);
    ImGui::Begin("Preview", nullptr, fixedPanelFlags());

    if (ImGui::BeginChild("PreviewToolbar", ImVec2(0.0f, 42.0f), ImGuiChildFlags_Borders))
    {
        if (ImGui::Selectable("Front", activePreviewView == SpritePreviewViewMode::Front, 0, ImVec2(64.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Front;
        ImGui::SameLine();
        if (ImGui::Selectable("Side", activePreviewView == SpritePreviewViewMode::Side, 0, ImVec2(64.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Side;
        ImGui::SameLine();
        if (ImGui::Selectable("Top", activePreviewView == SpritePreviewViewMode::Top, 0, ImVec2(64.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Top;
        ImGui::SameLine();
        if (ImGui::Selectable("Custom", activePreviewView == SpritePreviewViewMode::Custom, 0, ImVec2(72.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Custom;
        ImGui::SameLine();
        ImGui::Checkbox("Quad View", &previewQuadView);
    }
    ImGui::EndChild();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize(
        available.x > 64.0f ? available.x : 64.0f,
        available.y > 64.0f ? available.y : 64.0f);
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("SpritePreviewCanvas", canvasSize);

    auto drawRenderedTexture = [&](SpritePreviewViewMode mode, const ImVec2& pos, const ImVec2& size)
    {
        Texture* previewTexture = previewRenderer_.renderView(project_, mode, static_cast<int>(size.x), static_cast<int>(size.y));
        if (previewTexture && previewTexture->id != 0)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddImage(static_cast<ImTextureID>(static_cast<uintptr_t>(previewTexture->id)),
                               pos,
                               ImVec2(pos.x + size.x, pos.y + size.y),
                               ImVec2(0.0f, 1.0f),
                               ImVec2(1.0f, 0.0f));
        }

        drawPreviewTile(pos,
                        size,
                        previewViewLabel(mode),
                        mode == SpritePreviewViewMode::Custom ? "3D preview" : "Ortho preview",
                        mode == activePreviewView);
    };

    if (previewQuadView)
    {
        const float gap = 10.0f;
        const float tileWidth = (canvasSize.x - gap) * 0.5f;
        const float tileHeight = (canvasSize.y - gap) * 0.5f;
        drawRenderedTexture(SpritePreviewViewMode::Front, ImVec2(canvasPos.x, canvasPos.y), ImVec2(tileWidth, tileHeight));
        drawRenderedTexture(SpritePreviewViewMode::Side, ImVec2(canvasPos.x + tileWidth + gap, canvasPos.y), ImVec2(tileWidth, tileHeight));
        drawRenderedTexture(SpritePreviewViewMode::Top, ImVec2(canvasPos.x, canvasPos.y + tileHeight + gap), ImVec2(tileWidth, tileHeight));
        drawRenderedTexture(SpritePreviewViewMode::Custom, ImVec2(canvasPos.x + tileWidth + gap, canvasPos.y + tileHeight + gap), ImVec2(tileWidth, tileHeight));
    }
    else
    {
        drawRenderedTexture(activePreviewView, canvasPos, canvasSize);
    }

    ImGui::End();
}

void SpriteGeneratorApp::ShowLogPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float leftWidth = (showProjectPanel || showViewsPanel || showAnimationPanel) ? 364.0f : 12.0f;
    const float rightWidth = (showSurfacesPanel || showAttachmentsPanel || showExportPanel) ? 364.0f : 12.0f;
    const float bottomLogHeight = 184.0f;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + leftWidth, viewport->Pos.y + viewport->Size.y - bottomLogHeight),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(viewport->Size.x - leftWidth - rightWidth, bottomLogHeight - 12.0f),
        ImGuiCond_Always);
    ImGui::Begin("Log", nullptr, fixedPanelFlags());
    ImGui::InputTextMultiline("##SpriteLog", &logText, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
    ImGui::End();
}

void SpriteGeneratorApp::AppendLog(const std::string& line)
{
    if (!logText.empty())
        logText += '\n';
    logText += line;
}

void SpriteGeneratorApp::SaveProjectSettings()
{
    try
    {
        nlohmann::json j;
        j["projectName"] = project_.projectName;
        j["modelPath"] = project_.modelPath;
        j["outputPath"] = project_.outputPath;
        j["animationName"] = project_.animationName;
        j["frameStart"] = project_.frameStart;
        j["frameEnd"] = project_.frameEnd;
        j["currentFrame"] = project_.currentFrame;
        j["animationFps"] = project_.animationFps;
        j["animationLoop"] = project_.animationLoop;
        j["animationPlaying"] = project_.animationPlaying;
        j["spriteWidth"] = project_.spriteWidth;
        j["spriteHeight"] = project_.spriteHeight;
        j["previewYaw"] = project_.previewYaw;
        j["previewPitch"] = project_.previewPitch;
        j["previewZoom"] = project_.previewZoom;
        j["clearColor"] = {project_.clearColor.x, project_.clearColor.y, project_.clearColor.z, project_.clearColor.w};
        j["modelPosition"] = {project_.modelPosition.x, project_.modelPosition.y, project_.modelPosition.z};
        j["modelRotation"] = {project_.modelRotation.x, project_.modelRotation.y, project_.modelRotation.z};
        j["modelScale"] = {project_.modelScale.x, project_.modelScale.y, project_.modelScale.z};

        std::ofstream file("spritegen_project.json");
        file << j.dump(4);
        file.close();

        AppendLog("[project] settings saved");
    }
    catch (const std::exception& e)
    {
        AppendLog(std::string("[error] failed to save settings: ") + e.what());
    }
}

void SpriteGeneratorApp::LoadProjectSettings()
{
    try
    {
        std::ifstream file("spritegen_project.json");
        if (!file.good())
        {
            AppendLog("[project] no saved settings found");
            return;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        project_.projectName = j.value("projectName", project_.projectName);
        project_.modelPath = j.value("modelPath", project_.modelPath);
        project_.outputPath = j.value("outputPath", project_.outputPath);
        project_.animationName = j.value("animationName", project_.animationName);
        project_.frameStart = j.value("frameStart", project_.frameStart);
        project_.frameEnd = j.value("frameEnd", project_.frameEnd);
        project_.currentFrame = j.value("currentFrame", project_.currentFrame);
        project_.animationFps = j.value("animationFps", project_.animationFps);
        project_.animationLoop = j.value("animationLoop", project_.animationLoop);
        project_.animationPlaying = j.value("animationPlaying", project_.animationPlaying);
        project_.spriteWidth = j.value("spriteWidth", project_.spriteWidth);
        project_.spriteHeight = j.value("spriteHeight", project_.spriteHeight);
        project_.previewYaw = j.value("previewYaw", project_.previewYaw);
        project_.previewPitch = j.value("previewPitch", project_.previewPitch);
        project_.previewZoom = j.value("previewZoom", project_.previewZoom);

        if (j.contains("clearColor") && j["clearColor"].is_array() && j["clearColor"].size() == 4)
        {
            auto color = j["clearColor"];
            project_.clearColor = glm::vec4(color[0], color[1], color[2], color[3]);
        }

        if (j.contains("modelPosition") && j["modelPosition"].is_array() && j["modelPosition"].size() == 3)
        {
            auto pos = j["modelPosition"];
            project_.modelPosition = glm::vec3(pos[0], pos[1], pos[2]);
        }

        if (j.contains("modelPosition") && j["modelPosition"].is_array() && j["modelPosition"].size() == 3)
        {
            auto pos = j["modelPosition"];
            project_.modelPosition = glm::vec3(pos[0], pos[1], pos[2]);
        }
        if (j.contains("modelRotation") && j["modelRotation"].is_array() && j["modelRotation"].size() == 3)
        {
            auto rot = j["modelRotation"];
            project_.modelRotation = glm::vec3(rot[0], rot[1], rot[2]);
        }
        if (j.contains("modelScale") && j["modelScale"].is_array() && j["modelScale"].size() == 3)
        {
            auto scale = j["modelScale"];
            project_.modelScale = glm::vec3(scale[0], scale[1], scale[2]);
        }
      

        // Load the model after settings
        if (!project_.modelPath.empty())
        {
            std::string message;
            if (!previewRenderer_.loadModel(project_, &message))
                AppendLog("[error] failed to load model: " + message);
            else
                AppendLog("[project] settings loaded and model loaded");
        }
    }
    catch (const std::exception& e)
    {
        AppendLog(std::string("[error] failed to load settings: ") + e.what());
    }
}
