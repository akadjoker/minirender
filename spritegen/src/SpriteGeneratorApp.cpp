#include "SpriteGeneratorApp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "Manager.hpp"
#include "Material.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#define STB_RECT_PACK_IMPLEMENTATION
#include "imstb_rectpack.h"
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

const char* frontDirectionLabel(SpriteFrontDirection dir)
{
    return dir == SpriteFrontDirection::Front ? "Front" : "Back";
}

const char* sideDirectionLabel(SpriteSideDirection dir)
{
    return dir == SpriteSideDirection::Left ? "Left" : "Right";
}

const char* topDirectionLabel(SpriteTopDirection dir)
{
    return dir == SpriteTopDirection::Top ? "Top" : "Bottom";
}

const char* exportViewLabel(SpritePreviewViewMode mode)
{
    switch (mode)
    {
    case SpritePreviewViewMode::Front: return "front";
    case SpritePreviewViewMode::Side: return "side";
    case SpritePreviewViewMode::Top: return "top";
    case SpritePreviewViewMode::Custom: return "custom";
    }
    return "front";
}

struct AtlasPackResult
{
    int width = 0;
    int height = 0;
    std::vector<stbrp_rect> rects;
};

AtlasPackResult packAtlasRects(int count, int frameWidth, int frameHeight)
{
    AtlasPackResult result;
    if (count <= 0 || frameWidth <= 0 || frameHeight <= 0)
        return result;

    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
    const int rows = static_cast<int>(std::ceil(count / static_cast<float>(cols)));
    result.width = cols * frameWidth;
    result.height = rows * frameHeight;
    result.rects.resize(count);

    for (int i = 0; i < count; ++i)
    {
        result.rects[i].id = i;
        result.rects[i].w = frameWidth;
        result.rects[i].h = frameHeight;
        result.rects[i].x = 0;
        result.rects[i].y = 0;
        result.rects[i].was_packed = 0;
    }

    std::vector<stbrp_node> nodes(static_cast<size_t>(result.width));
    stbrp_context ctx{};
    stbrp_init_target(&ctx, result.width, result.height, nodes.data(), static_cast<int>(nodes.size()));
    stbrp_pack_rects(&ctx, result.rects.data(), static_cast<int>(result.rects.size()));

    return result;
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

constexpr float kRightPanelWidth = 340.0f;
constexpr float kRightPanelMargin = 12.0f;
constexpr float kRightPanelTop = 36.0f;
constexpr float kRightPanelBottom = 12.0f;
constexpr float kLeftPanelWidth = 340.0f;
constexpr float kLeftPanelMargin = 12.0f;
constexpr float kLeftPanelTop = 36.0f;
constexpr float kLeftPanelBottom = 12.0f;

} // namespace

SpriteGeneratorApp::SpriteGeneratorApp()
{
    attachmentBone = "tag_weapon";
    applySpriteTheme(currentTheme);
    AppendLog("[ui] sprite generator shell ready");
    AppendLog("[ui] theme Studio");
    lastModelDirectory = std::filesystem::current_path();
    lastTextureDirectory = std::filesystem::current_path();
    lastWeaponDirectory = std::filesystem::current_path();
    LoadProjectSettings();
}

void SpriteGeneratorApp::RenderFrame(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool allowShortcuts = !io.WantTextInput && !ImGui::IsAnyItemActive();
    if (allowShortcuts)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        {
            project_.animationPlaying = false;
            project_.currentFrame = std::max(static_cast<float>(project_.frameStart), project_.currentFrame - 1.0f);
            previewRenderer_.update(project_, 0.0f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        {
            project_.animationPlaying = false;
            project_.currentFrame = std::min(static_cast<float>(project_.frameEnd), project_.currentFrame + 1.0f);
            previewRenderer_.update(project_, 0.0f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            CaptureCurrentFrame();
        }
    }

    previewRenderer_.update(project_, dt);

    ShowMenuBar();
    ShowFileDialog();
    if (showProjectPanel || showViewsPanel || showAnimationPanel)
        ShowLeftPanel();
    if (showRenderPanel || showSurfacesPanel || showAttachmentsPanel || showExportPanel)
        ShowRightPanel();
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
            fileDialogAction = FileDialogAction::LoadModel;
            fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, lastModelDirectory);
        }
        if (ImGui::MenuItem("Load Weapon..."))
        {
            fileDialogAction = FileDialogAction::LoadWeapon;
            fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, lastWeaponDirectory);
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
            if (fileDialogAction == FileDialogAction::LoadTexture)
            {
                std::string message;
                if (previewRenderer_.setSurfaceTexture(textureTargetSurface, path, &message))
                    AppendLog("[surface] texture set");
                else
                    AppendLog("[error] " + message);
                lastTextureDirectory = result.path.parent_path();
            }
            else if (fileDialogAction == FileDialogAction::LoadWeapon)
            {
                std::string message;
                if (previewRenderer_.loadWeapon(path, &message))
                    AppendLog("[weapon] loaded");
                else
                    AppendLog("[error] " + message);
                lastWeaponDirectory = result.path.parent_path();
            }
            else
            {
                project_.modelPath = path;

                std::string message;
                if (previewRenderer_.loadModel(project_, &message))
                    AppendLog("[model] " + message);
                else
                    AppendLog("[error] " + message);

                lastModelDirectory = result.path.parent_path();
                SaveProjectSettings();
            }
        }
        fileDialogAction = FileDialogAction::None;
    }

    const std::filesystem::path projectDir =
        (fileDialogAction == FileDialogAction::LoadTexture) ? lastTextureDirectory :
        (fileDialogAction == FileDialogAction::LoadWeapon) ? lastWeaponDirectory :
        lastModelDirectory;
    fileDialog_.Render(projectDir, std::filesystem::current_path(), std::filesystem::current_path());
}

void SpriteGeneratorApp::ShowLeftPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float x = viewport->Pos.x + kLeftPanelMargin;
    const float y = viewport->Pos.y + kLeftPanelTop;
    const float height = viewport->Size.y - kLeftPanelTop - kLeftPanelBottom;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kLeftPanelWidth, height), ImGuiCond_Always);
    ImGui::Begin("Sidebar", nullptr, fixedPanelFlags());
    ImGui::BeginChild("##LeftScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (showProjectPanel && ImGui::CollapsingHeader("Project", ImGuiTreeNodeFlags_DefaultOpen))
        DrawProjectSection();
    if (showViewsPanel && ImGui::CollapsingHeader("Views", ImGuiTreeNodeFlags_DefaultOpen))
        DrawViewsSection();
    if (showAnimationPanel && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
        DrawAnimationSection();

    ImGui::EndChild();
    ImGui::End();
}

void SpriteGeneratorApp::DrawProjectSection()
{
    ImGui::InputText("Project Name", &project_.projectName);
    ImGui::TextUnformatted("Model:");
    ImGui::TextWrapped("%s", project_.modelPath.empty() ? "(none)" : project_.modelPath.c_str());
    if (ImGui::Button("Load Model..."))
    {
        fileDialogAction = FileDialogAction::LoadModel;
        fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, lastModelDirectory);
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
}

void SpriteGeneratorApp::DrawViewsSection()
{
    if (ImGui::RadioButton(frontDirectionLabel(project_.frontDirection), activePreviewView == SpritePreviewViewMode::Front))
        activePreviewView = SpritePreviewViewMode::Front;
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        ImGui::OpenPopup("FrontDirectionPopup");
    ImGui::SameLine();
    if (ImGui::SmallButton("...##frontdir"))
        ImGui::OpenPopup("FrontDirectionPopup");
    if (ImGui::BeginPopup("FrontDirectionPopup"))
    {
        if (ImGui::Selectable("Front", project_.frontDirection == SpriteFrontDirection::Front))
            project_.frontDirection = SpriteFrontDirection::Front;
        if (ImGui::Selectable("Back", project_.frontDirection == SpriteFrontDirection::Back))
            project_.frontDirection = SpriteFrontDirection::Back;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(sideDirectionLabel(project_.sideDirection), activePreviewView == SpritePreviewViewMode::Side))
        activePreviewView = SpritePreviewViewMode::Side;
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        ImGui::OpenPopup("SideDirectionPopup");
    ImGui::SameLine();
    if (ImGui::SmallButton("...##sidedir"))
        ImGui::OpenPopup("SideDirectionPopup");
    if (ImGui::BeginPopup("SideDirectionPopup"))
    {
        if (ImGui::Selectable("Left", project_.sideDirection == SpriteSideDirection::Left))
            project_.sideDirection = SpriteSideDirection::Left;
        if (ImGui::Selectable("Right", project_.sideDirection == SpriteSideDirection::Right))
            project_.sideDirection = SpriteSideDirection::Right;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(topDirectionLabel(project_.topDirection), activePreviewView == SpritePreviewViewMode::Top))
        activePreviewView = SpritePreviewViewMode::Top;
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        ImGui::OpenPopup("TopDirectionPopup");
    ImGui::SameLine();
    if (ImGui::SmallButton("...##topdir"))
        ImGui::OpenPopup("TopDirectionPopup");
    if (ImGui::BeginPopup("TopDirectionPopup"))
    {
        if (ImGui::Selectable("Top", project_.topDirection == SpriteTopDirection::Top))
            project_.topDirection = SpriteTopDirection::Top;
        if (ImGui::Selectable("Bottom", project_.topDirection == SpriteTopDirection::Bottom))
            project_.topDirection = SpriteTopDirection::Bottom;
        ImGui::EndPopup();
    }
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
    if (activePreviewView == SpritePreviewViewMode::Front)
        ImGui::TextDisabled("Right click Front for Front/Back");
    else if (activePreviewView == SpritePreviewViewMode::Side)
        ImGui::TextDisabled("Right click Side for Left/Right");
    else if (activePreviewView == SpritePreviewViewMode::Top)
        ImGui::TextDisabled("Right click Top for Top/Bottom");

    if (activePreviewView == SpritePreviewViewMode::Custom)
    {
        ImGui::SliderFloat("Yaw", &project_.customPreviewYaw, -180.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat("Pitch", &project_.customPreviewPitch, -90.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Zoom", &project_.customPreviewZoom, 0.1f, 4.0f, "%.2f x");
    }
    else
    {
        ImGui::SliderFloat("Ortho Zoom", &project_.orthoPreviewZoom, 0.1f, 4.0f, "%.2f x");
    }
}

void SpriteGeneratorApp::DrawAnimationSection()
{
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
}

void SpriteGeneratorApp::ShowRightPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float x = viewport->Pos.x + viewport->Size.x - (kRightPanelWidth + kRightPanelMargin);
    const float y = viewport->Pos.y + kRightPanelTop;
    const float height = viewport->Size.y - kRightPanelTop - kRightPanelBottom;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kRightPanelWidth, height), ImGuiCond_Always);
    ImGui::Begin("Controls", nullptr, fixedPanelFlags());
    ImGui::BeginChild("##RightScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (showRenderPanel && ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen))
        DrawRenderSection();
    if (showSurfacesPanel && ImGui::CollapsingHeader("Surfaces", ImGuiTreeNodeFlags_DefaultOpen))
        DrawSurfacesSection();
    if (showAttachmentsPanel && ImGui::CollapsingHeader("Attachments", ImGuiTreeNodeFlags_DefaultOpen))
        DrawAttachmentsSection();
    if (showExportPanel && ImGui::CollapsingHeader("Export", ImGuiTreeNodeFlags_DefaultOpen))
        DrawExportSection();

    ImGui::EndChild();
    ImGui::End();
}

void SpriteGeneratorApp::DrawSurfacesSection()
{
    const std::vector<std::string>& labels = previewRenderer_.surfaceLabels();
    if (labels.empty())
    {
        ImGui::TextDisabled("No surfaces found");
        return;
    }

    if (selectedSurface < 0 || selectedSurface >= static_cast<int>(labels.size()))
        selectedSurface = 0;

    std::vector<const char*> items;
    items.reserve(labels.size());
    for (const auto& label : labels)
        items.push_back(label.c_str());

    ImGui::ListBox("Surface", &selectedSurface, items.data(), static_cast<int>(items.size()), 6);
    ImGui::Separator();
    ImGui::TextUnformatted("Texture override");
    if (ImGui::Button("Pick Texture"))
    {
        textureTargetSurface = selectedSurface;
        fileDialogAction = FileDialogAction::LoadTexture;
        fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, lastTextureDirectory);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        std::string message;
        if (!previewRenderer_.resetSurfaceTexture(selectedSurface, &message))
            AppendLog("[error] " + message);
    }
    ImGui::Checkbox("Visible", &surfaceVisible);
}

void SpriteGeneratorApp::DrawAttachmentsSection()
{
    const char* attachments[] = {"Primary Weapon", "Offhand", "Muzzle FX"};
    ImGui::ListBox("Slot", &selectedAttachment, attachments, IM_ARRAYSIZE(attachments), 4);
    ImGui::Checkbox("Enabled", &attachmentEnabled);
    previewRenderer_.setAttachmentEnabled(attachmentEnabled);

    if (ImGui::Button("Load Weapon..."))
    {
        fileDialogAction = FileDialogAction::LoadWeapon;
        fileDialog_.Open(ImGuiFileDialog::Mode::OpenFile, lastWeaponDirectory);
    }

    const std::vector<std::string>& bones = previewRenderer_.boneLabels();
    if (!bones.empty())
    {
        if (attachmentBoneIndex < 0 || attachmentBoneIndex >= static_cast<int>(bones.size()))
            attachmentBoneIndex = 0;
        std::vector<const char*> boneItems;
        boneItems.reserve(bones.size());
        for (const auto& bone : bones)
            boneItems.push_back(bone.c_str());
        ImGui::Combo("Bone", &attachmentBoneIndex, boneItems.data(), static_cast<int>(boneItems.size()));
        std::string boneName = bones[attachmentBoneIndex];
        const auto hash = boneName.find(" (#");
        if (hash != std::string::npos)
            boneName = boneName.substr(0, hash);
        previewRenderer_.setAttachmentBoneName(boneName);
    }
    else
    {
        ImGui::TextDisabled("No bones available");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Attachment transforms");
    ImGui::DragFloat3("Pos##attach", &attachmentPosition.x, 0.01f);
    ImGui::DragFloat3("Rot##attach", &attachmentRotation.x, 0.5f);
    ImGui::DragFloat3("Scale##attach", &attachmentScale.x, 0.01f);
    previewRenderer_.setAttachmentTransform(attachmentPosition, attachmentRotation, attachmentScale);
}

void SpriteGeneratorApp::DrawRenderSection()
{
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
}

void SpriteGeneratorApp::DrawExportSection()
{
    ImGui::InputInt("Width", &project_.spriteWidth);
    ImGui::InputInt("Height", &project_.spriteHeight);
    ImGui::Checkbox("Transparent Background", &project_.transparentBackground);
    ImGui::Checkbox("Export Frames", &project_.exportFrames);
    ImGui::Checkbox("Export Atlas", &project_.exportAtlas);
    ImGui::Checkbox("Export JSON", &project_.exportJson);
    ImGui::Checkbox("Trim Output", &project_.trimOutput);
    ImGui::Separator();
    ImGui::InputText("Prefix", &project_.exportPrefix);
    if (ImGui::Button("Preview Export"))
        BuildAtlasPreview();
    ImGui::SameLine();
    if (ImGui::Button("Export##"))
        ExportSprites();
    ImGui::Checkbox("Show Atlas Preview", &showAtlasPreview);
}

void SpriteGeneratorApp::ExportSprites()
{
    if (!previewRenderer_.hasLoadedModel())
    {
        AppendLog("[export] no model loaded");
        return;
    }

    const int width = std::max(1, project_.spriteWidth);
    const int height = std::max(1, project_.spriteHeight);
    const int startFrame = project_.frameStart;
    const int endFrame = std::max(project_.frameStart, project_.frameEnd);
    const int frameCount = endFrame - startFrame + 1;
    if (frameCount <= 0)
    {
        AppendLog("[export] invalid frame range");
        return;
    }

    if (project_.trimOutput)
        AppendLog("[export] trim output not implemented yet");

    std::filesystem::path outputRoot = project_.outputPath.empty()
        ? std::filesystem::path("bin/sprites")
        : std::filesystem::path(project_.outputPath);
    std::filesystem::create_directories(outputRoot);

    const std::string animName = project_.animationName.empty() ? "anim" : project_.animationName;
    const std::string baseName = project_.exportPrefix.empty() ? animName : project_.exportPrefix;

    struct ViewConfig
    {
        SpritePreviewViewMode mode;
        bool enabled;
    };

    std::array<ViewConfig, 4> views = {{
        { SpritePreviewViewMode::Front, project_.viewFront },
        { SpritePreviewViewMode::Side, project_.viewSide },
        { SpritePreviewViewMode::Top, project_.viewTop },
        { SpritePreviewViewMode::Custom, project_.viewCustom },
    }};

    const bool wasPlaying = project_.animationPlaying;
    const float oldFrame = project_.currentFrame;
    project_.animationPlaying = false;

    for (const auto& view : views)
    {
        if (!view.enabled)
            continue;

        const char* viewLabel = exportViewLabel(view.mode);
        std::filesystem::path viewRoot = outputRoot / animName / viewLabel;
        std::filesystem::create_directories(viewRoot);

        std::vector<std::unique_ptr<Pixmap>> frames;
        frames.reserve(frameCount);

        for (int i = 0; i < frameCount; ++i)
        {
            project_.currentFrame = static_cast<float>(startFrame + i);
            previewRenderer_.update(project_, 0.0f);

            std::string error;
            auto frame = previewRenderer_.renderToPixmap(project_, view.mode, width, height, &error);
            if (!frame)
            {
                AppendLog("[export] render failed: " + error);
                project_.animationPlaying = wasPlaying;
                project_.currentFrame = oldFrame;
                return;
            }

            if (project_.exportFrames)
            {
                std::ostringstream name;
                name << baseName << "_" << viewLabel << "_"
                     << std::setw(frameCount > 999 ? 4 : 3) << std::setfill('0') << i << ".png";
                const std::filesystem::path framePath = viewRoot / name.str();
                frame->Save(framePath.string().c_str());
            }

            if (project_.exportAtlas || project_.exportJson)
                frames.emplace_back(std::move(frame));
        }

        if (project_.exportAtlas || project_.exportJson)
        {
            AtlasPackResult pack = packAtlasRects(frameCount, width, height);
            if (pack.width <= 0 || pack.height <= 0)
            {
                AppendLog("[export] atlas pack failed");
                project_.animationPlaying = wasPlaying;
                project_.currentFrame = oldFrame;
                return;
            }

            Pixmap atlas(pack.width, pack.height, 4);
            atlas.Clear();

            nlohmann::json meta;
            meta["animation"] = animName;
            meta["view"] = viewLabel;
            meta["fps"] = project_.animationFps;
            meta["frameCount"] = frameCount;
            meta["frameSize"] = { width, height };
            meta["atlasSize"] = { pack.width, pack.height };
            meta["pivot"] = { width / 2, height / 2 };
            meta["startFrame"] = startFrame;
            meta["endFrame"] = endFrame;

            nlohmann::json framesJson = nlohmann::json::array();
            for (int i = 0; i < frameCount; ++i)
            {
                const stbrp_rect& rect = pack.rects[i];
                if (!rect.was_packed)
                    continue;
                const int x = rect.x;
                const int y = rect.y;
                atlas.DrawPixmap(*frames[i], x, y);

                nlohmann::json entry;
                entry["index"] = i;
                entry["atlas"] = { x, y, width, height };
                if (project_.exportFrames)
                {
                    std::ostringstream name;
                    name << baseName << "_" << viewLabel << "_"
                         << std::setw(frameCount > 999 ? 4 : 3) << std::setfill('0') << i << ".png";
                    entry["file"] = name.str();
                }
                framesJson.push_back(entry);
            }
            meta["frames"] = framesJson;

            if (project_.exportAtlas)
            {
                const std::filesystem::path atlasPath = viewRoot / (baseName + "_" + viewLabel + "_atlas.png");
                atlas.Save(atlasPath.string().c_str());
            }

            if (project_.exportJson)
            {
                const std::filesystem::path jsonPath = viewRoot / (baseName + "_" + viewLabel + "_atlas.json");
                std::ofstream file(jsonPath);
                file << meta.dump(2);
            }
        }
    }

    project_.animationPlaying = wasPlaying;
    project_.currentFrame = oldFrame;

    AppendLog("[export] finished");
}

void SpriteGeneratorApp::CaptureCurrentFrame()
{
    if (!previewRenderer_.hasLoadedModel())
    {
        AppendLog("[capture] no model loaded");
        return;
    }

    const int width = std::max(1, project_.spriteWidth);
    const int height = std::max(1, project_.spriteHeight);
    const int frameIndex = std::clamp(static_cast<int>(std::round(project_.currentFrame)),
                                      project_.frameStart,
                                      std::max(project_.frameStart, project_.frameEnd));

    std::filesystem::path outputRoot = project_.outputPath.empty()
        ? std::filesystem::path("bin/sprites")
        : std::filesystem::path(project_.outputPath);
    std::filesystem::create_directories(outputRoot);

    const std::string animName = project_.animationName.empty() ? "anim" : project_.animationName;
    const std::string baseName = project_.exportPrefix.empty() ? animName : project_.exportPrefix;
    const char* viewLabel = exportViewLabel(activePreviewView);

    std::filesystem::path viewRoot = outputRoot / animName / viewLabel;
    std::filesystem::create_directories(viewRoot);

    std::string error;
    auto frame = previewRenderer_.renderToPixmap(project_, activePreviewView, width, height, &error);
    if (!frame)
    {
        AppendLog("[capture] render failed: " + error);
        return;
    }

    std::ostringstream name;
    name << baseName << "_" << viewLabel << "_"
         << std::setw(4) << std::setfill('0') << frameIndex << ".png";
    const std::filesystem::path framePath = viewRoot / name.str();
    frame->Save(framePath.string().c_str());

    AppendLog("[capture] saved " + framePath.string());
}

void SpriteGeneratorApp::BuildAtlasPreview()
{
    if (!previewRenderer_.hasLoadedModel())
    {
        AppendLog("[preview] no model loaded");
        return;
    }

    const int width = std::max(1, project_.spriteWidth);
    const int height = std::max(1, project_.spriteHeight);
    const int startFrame = project_.frameStart;
    const int endFrame = std::max(project_.frameStart, project_.frameEnd);
    const int frameCount = endFrame - startFrame + 1;
    if (frameCount <= 0)
    {
        AppendLog("[preview] invalid frame range");
        return;
    }

    const std::string animName = project_.animationName.empty() ? "anim" : project_.animationName;
    const std::string baseName = project_.exportPrefix.empty() ? animName : project_.exportPrefix;
    const SpritePreviewViewMode mode = activePreviewView;

    std::vector<std::unique_ptr<Pixmap>> frames;
    frames.reserve(frameCount);

    const bool wasPlaying = project_.animationPlaying;
    const float oldFrame = project_.currentFrame;
    project_.animationPlaying = false;

    for (int i = 0; i < frameCount; ++i)
    {
        project_.currentFrame = static_cast<float>(startFrame + i);
        previewRenderer_.update(project_, 0.0f);

        std::string error;
        auto frame = previewRenderer_.renderToPixmap(project_, mode, width, height, &error);
        if (!frame)
        {
            AppendLog("[preview] render failed: " + error);
            project_.animationPlaying = wasPlaying;
            project_.currentFrame = oldFrame;
            return;
        }
        frames.emplace_back(std::move(frame));
    }

    project_.animationPlaying = wasPlaying;
    project_.currentFrame = oldFrame;

    AtlasPackResult pack = packAtlasRects(frameCount, width, height);
    if (pack.width <= 0 || pack.height <= 0)
    {
        AppendLog("[preview] atlas pack failed");
        return;
    }

    Pixmap atlas(pack.width, pack.height, 4);
    atlas.Clear();
    for (int i = 0; i < frameCount; ++i)
    {
        const stbrp_rect& rect = pack.rects[i];
        if (!rect.was_packed)
            continue;
        atlas.DrawPixmap(*frames[i], rect.x, rect.y);
    }

    auto& texMgr = TextureManager::instance();
    texMgr.unload("__spritegen_atlas_preview");
    atlasPreviewTexture = texMgr.createFromPixmap("__spritegen_atlas_preview", atlas);
    atlasPreviewWidth = pack.width;
    atlasPreviewHeight = pack.height;
    showAtlasPreview = true;

    std::ostringstream msg;
    msg << "[preview] atlas " << baseName << " (" << pack.width << "x" << pack.height << ")";
    AppendLog(msg.str());
}

void SpriteGeneratorApp::ShowPreviewPanel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float leftWidth = (showProjectPanel || showViewsPanel || showAnimationPanel) ? 364.0f : 12.0f;
    const float rightWidth = (showRenderPanel || showSurfacesPanel || showAttachmentsPanel || showExportPanel) ? 364.0f : 12.0f;
    const float topOffset = 36.0f;
    const float bottomLogHeight = showLogPanelWindow ? 196.0f : 12.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + leftWidth, viewport->Pos.y + topOffset), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(viewport->Size.x - leftWidth - rightWidth, viewport->Size.y - topOffset - bottomLogHeight),
        ImGuiCond_Always);
    ImGui::Begin("Preview", nullptr, fixedPanelFlags());

    auto openDirectionPopupForMode = [&](SpritePreviewViewMode mode)
    {
        if (mode == SpritePreviewViewMode::Front)
            ImGui::OpenPopup("FrontDirectionPopupPreview");
        else if (mode == SpritePreviewViewMode::Side)
            ImGui::OpenPopup("SideDirectionPopupPreview");
        else if (mode == SpritePreviewViewMode::Top)
            ImGui::OpenPopup("TopDirectionPopupPreview");
    };

    auto drawDirectionPopups = [&]()
    {
        if (ImGui::BeginPopup("FrontDirectionPopupPreview"))
        {
            if (ImGui::Selectable("Front", project_.frontDirection == SpriteFrontDirection::Front))
                project_.frontDirection = SpriteFrontDirection::Front;
            if (ImGui::Selectable("Back", project_.frontDirection == SpriteFrontDirection::Back))
                project_.frontDirection = SpriteFrontDirection::Back;
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("SideDirectionPopupPreview"))
        {
            if (ImGui::Selectable("Left", project_.sideDirection == SpriteSideDirection::Left))
                project_.sideDirection = SpriteSideDirection::Left;
            if (ImGui::Selectable("Right", project_.sideDirection == SpriteSideDirection::Right))
                project_.sideDirection = SpriteSideDirection::Right;
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("TopDirectionPopupPreview"))
        {
            if (ImGui::Selectable("Top", project_.topDirection == SpriteTopDirection::Top))
                project_.topDirection = SpriteTopDirection::Top;
            if (ImGui::Selectable("Bottom", project_.topDirection == SpriteTopDirection::Bottom))
                project_.topDirection = SpriteTopDirection::Bottom;
            ImGui::EndPopup();
        }
    };

    if (ImGui::BeginChild("PreviewToolbar", ImVec2(0.0f, 42.0f), ImGuiChildFlags_Borders))
    {
        if (ImGui::Selectable("Front", activePreviewView == SpritePreviewViewMode::Front, 0, ImVec2(64.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Front;
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            openDirectionPopupForMode(SpritePreviewViewMode::Front);
        ImGui::SameLine();
        if (ImGui::Selectable("Side", activePreviewView == SpritePreviewViewMode::Side, 0, ImVec2(64.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Side;
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            openDirectionPopupForMode(SpritePreviewViewMode::Side);
        ImGui::SameLine();
        if (ImGui::Selectable("Top", activePreviewView == SpritePreviewViewMode::Top, 0, ImVec2(64.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Top;
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            openDirectionPopupForMode(SpritePreviewViewMode::Top);
        ImGui::SameLine();
        if (ImGui::Selectable("Custom", activePreviewView == SpritePreviewViewMode::Custom, 0, ImVec2(72.0f, 24.0f)))
            activePreviewView = SpritePreviewViewMode::Custom;
        ImGui::SameLine();
        ImGui::Checkbox("Quad View", &previewQuadView);
    }
    ImGui::EndChild();
    drawDirectionPopups();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize(
        available.x > 64.0f ? available.x : 64.0f,
        available.y > 64.0f ? available.y : 64.0f);
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextItemAllowOverlap();
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

        const bool hovered =
            io.MousePos.x >= pos.x && io.MousePos.x <= pos.x + size.x &&
            io.MousePos.y >= pos.y && io.MousePos.y <= pos.y + size.y;
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            activePreviewView = mode;
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            activePreviewView = mode;
            openDirectionPopupForMode(mode);
        }
    };

    if (showAtlasPreview && atlasPreviewTexture && atlasPreviewTexture->id != 0)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float scaleX = atlasPreviewWidth > 0 ? (canvasSize.x / static_cast<float>(atlasPreviewWidth)) : 1.0f;
        const float scaleY = atlasPreviewHeight > 0 ? (canvasSize.y / static_cast<float>(atlasPreviewHeight)) : 1.0f;
        const float scale = std::min(scaleX, scaleY);
        const ImVec2 atlasSize(
            static_cast<float>(atlasPreviewWidth) * scale,
            static_cast<float>(atlasPreviewHeight) * scale);
        const ImVec2 atlasPos(
            canvasPos.x + (canvasSize.x - atlasSize.x) * 0.5f,
            canvasPos.y + (canvasSize.y - atlasSize.y) * 0.5f);
        drawList->AddImage(static_cast<ImTextureID>(static_cast<uintptr_t>(atlasPreviewTexture->id)),
                           atlasPos,
                           ImVec2(atlasPos.x + atlasSize.x, atlasPos.y + atlasSize.y),
                           ImVec2(0.0f, 0.0f),
                           ImVec2(1.0f, 1.0f));
        drawPreviewTile(atlasPos, atlasSize, "Atlas Preview", "From export preview", true);
    }
    else if (previewQuadView)
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

    drawDirectionPopups();

    ImVec2 customPos = canvasPos;
    ImVec2 customSize = canvasSize;
    if (previewQuadView)
    {
        const float gap = 10.0f;
        const float tileWidth = (canvasSize.x - gap) * 0.5f;
        const float tileHeight = (canvasSize.y - gap) * 0.5f;
        customPos = ImVec2(canvasPos.x + tileWidth + gap, canvasPos.y + tileHeight + gap);
        customSize = ImVec2(tileWidth, tileHeight);
    }

    const bool customHovered =
        io.MousePos.x >= customPos.x &&
        io.MousePos.x <= customPos.x + customSize.x &&
        io.MousePos.y >= customPos.y &&
        io.MousePos.y <= customPos.y + customSize.y;

    if (activePreviewView == SpritePreviewViewMode::Custom &&
        customHovered &&
        !showAtlasPreview)
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            project_.customPreviewYaw += io.MouseDelta.x * 0.35f;
            project_.customPreviewPitch -= io.MouseDelta.y * 0.25f;
            project_.customPreviewPitch = std::clamp(project_.customPreviewPitch, -89.0f, 89.0f);
        }

        if (io.MouseWheel != 0.0f)
        {
            project_.customPreviewZoom += io.MouseWheel * 0.1f;
            project_.customPreviewZoom = std::clamp(project_.customPreviewZoom, 0.1f, 4.0f);
        }
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
        j["customPreviewYaw"] = project_.customPreviewYaw;
        j["customPreviewPitch"] = project_.customPreviewPitch;
        j["customPreviewZoom"] = project_.customPreviewZoom;
        j["orthoPreviewZoom"] = project_.orthoPreviewZoom;
        j["frontDirection"] = static_cast<int>(project_.frontDirection);
        j["sideDirection"] = static_cast<int>(project_.sideDirection);
        j["topDirection"] = static_cast<int>(project_.topDirection);
        j["clearColor"] = {project_.clearColor.x, project_.clearColor.y, project_.clearColor.z, project_.clearColor.w};
        j["modelPosition"] = {project_.modelPosition.x, project_.modelPosition.y, project_.modelPosition.z};
        j["modelRotation"] = {project_.modelRotation.x, project_.modelRotation.y, project_.modelRotation.z};
        j["modelScale"] = {project_.modelScale.x, project_.modelScale.y, project_.modelScale.z};
        j["lastModelDirectory"] = lastModelDirectory.string();
        j["lastTextureDirectory"] = lastTextureDirectory.string();
        j["lastWeaponDirectory"] = lastWeaponDirectory.string();

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
        project_.customPreviewYaw = j.value("customPreviewYaw", j.value("previewYaw", project_.customPreviewYaw));
        project_.customPreviewPitch = j.value("customPreviewPitch", j.value("previewPitch", project_.customPreviewPitch));
        project_.customPreviewZoom = j.value("customPreviewZoom", j.value("previewZoom", project_.customPreviewZoom));
        project_.orthoPreviewZoom = j.value("orthoPreviewZoom", project_.orthoPreviewZoom);
        project_.frontDirection = static_cast<SpriteFrontDirection>(j.value("frontDirection", static_cast<int>(project_.frontDirection)));
        project_.sideDirection = static_cast<SpriteSideDirection>(j.value("sideDirection", static_cast<int>(project_.sideDirection)));
        project_.topDirection = static_cast<SpriteTopDirection>(j.value("topDirection", static_cast<int>(project_.topDirection)));

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

        if (j.contains("lastModelDirectory"))
            lastModelDirectory = j.value("lastModelDirectory", lastModelDirectory.string());
        if (j.contains("lastTextureDirectory"))
            lastTextureDirectory = j.value("lastTextureDirectory", lastTextureDirectory.string());
        if (j.contains("lastWeaponDirectory"))
            lastWeaponDirectory = j.value("lastWeaponDirectory", lastWeaponDirectory.string());
      

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
