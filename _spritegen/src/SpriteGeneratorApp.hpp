#pragma once

#include <string>
#include <filesystem>

#include "imgui.h"

#include "ImGuiFileDialog.h"
#include "SpritePreviewRenderer.hpp"
#include "SpriteProject.hpp"
#include "SpriteTheme.hpp"

struct Texture;

class SpriteGeneratorApp
{
public:
    SpriteGeneratorApp();

    void RenderFrame(float dt);

private:
    void ShowMenuBar();
    void ShowLeftPanel();
    void ShowRightPanel();
    void ShowPreviewPanel();
    void ShowLogPanel();
    void ShowFileDialog();

    void DrawProjectSection();
    void DrawViewsSection();
    void DrawAnimationSection();
    void DrawRenderSection();
    void DrawSurfacesSection();
    void DrawAttachmentsSection();
    void DrawExportSection();
    void ExportSprites();
    void CaptureCurrentFrame();
    void BuildAtlasPreview();

    enum class FileDialogAction
    {
        None,
        LoadModel,
        LoadTexture,
        LoadWeapon
    };

    void AppendLog(const std::string& line);
    void SaveProjectSettings();
    void LoadProjectSettings();

    bool showProjectPanel = true;
    bool showViewsPanel = true;
    bool showAnimationPanel = true;
    bool showSurfacesPanel = true;
    bool showAttachmentsPanel = true;
    bool showExportPanel = true;
    bool showPreviewPanelWindow = true;
    bool showLogPanelWindow = true;
    bool showRenderPanel = true;
    bool previewQuadView = false;
    bool showAtlasPreview = false;
    int atlasPreviewWidth = 0;
    int atlasPreviewHeight = 0;
    Texture* atlasPreviewTexture = nullptr;
    int textureTargetSurface = -1;
    FileDialogAction fileDialogAction = FileDialogAction::None;
    std::filesystem::path lastModelDirectory;
    std::filesystem::path lastTextureDirectory;
    std::filesystem::path lastWeaponDirectory;
    int attachmentBoneIndex = 0;
    glm::vec3 attachmentPosition = glm::vec3(0.0f);
    glm::vec3 attachmentRotation = glm::vec3(0.0f);
    glm::vec3 attachmentScale = glm::vec3(1.0f);
    int selectedSurface = 0;
    int selectedAttachment = 0;
    bool surfaceVisible = true;
    bool attachmentEnabled = true;
    std::string attachmentBone;
    std::string logText;
    SpriteTheme currentTheme = SpriteTheme::Studio;
    SpriteProject project_;
    SpritePreviewRenderer previewRenderer_;
    SpritePreviewViewMode activePreviewView = SpritePreviewViewMode::Front;

    // File dialog
    ImGuiFileDialog fileDialog_;
};
