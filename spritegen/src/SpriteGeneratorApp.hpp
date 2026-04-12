#pragma once

#include <string>

#include "ImGuiFileDialog.h"
#include "SpritePreviewRenderer.hpp"
#include "SpriteProject.hpp"
#include "SpriteTheme.hpp"

class SpriteGeneratorApp
{
public:
    SpriteGeneratorApp();

    void RenderFrame(float dt);

private:
    void ShowMenuBar();
    void ShowProjectPanel();
    void ShowViewsPanel();
    void ShowAnimationPanel();
    void ShowSurfacesPanel();
    void ShowAttachmentsPanel();
    void ShowExportPanel();
    void ShowPreviewPanel();
    void ShowLogPanel();
    void ShowFileDialog();
    void ShowRenderPanel();

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
