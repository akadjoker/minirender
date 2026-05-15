// spritegen/src/main.cpp — BuGUI-based Sprite Generator
// Replaces the ImGui SpriteGeneratorApp with native BuGUI widgets.
// SpritePreviewRenderer and SpriteProject are reused unchanged.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// Core
#include "Device.hpp"
#include "Input.hpp"
#include "Material.hpp"   // Texture::id
#include "Opengl.hpp"     // glViewport, glClear, glClearColor

// BuGUI
#include <BasicWidgets.hpp>
#include <ComboBox.hpp>
#include <DialogWidgets.hpp>
#include <DockPanel.hpp>
#include <FileDialog.hpp>
#include <IconAtlas.hpp>
#include <InputWidgets.hpp>
#include <LayoutWidgets.hpp>
#include <MenuWidgets.hpp>
#include <ScrollWidgets.hpp>
#include <TextInputWidgets.hpp>
#include <ViewWidgets.hpp>
#include <WidgetApp.hpp>

// _spritegen (reused, no ImGui deps)
#include "SpritePreviewRenderer.hpp"
#include "SpriteProject.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
static Device& s_device = Device::Instance();

// Display a GL texture (Y-flipped, as FBOs have origin at bottom-left).
class PreviewImageWidget : public BuGUI::Widget
{
public:
    void setTex(BuGUI::TextureHandle t) { tex_ = t; markDirty(); }

    void paint(BuGUI::PaintContext& ctx) override
    {
        if (tex_)
        {
            ctx.drawImage(tex_, rect_, {0.0f, 1.0f, 1.0f, -1.0f});
        }
        else
        {
            ctx.fill.SetColor(24, 24, 28, 255);
            ctx.fillRect(rect_.x, rect_.y, rect_.w, rect_.h);
            ctx.fill.SetColor(120, 120, 130, 255);
            ctx.drawTextAligned(rect_, "No model loaded",
                                BuGUI::AlignX::Center, BuGUI::AlignY::Middle);
        }
    }

private:
    BuGUI::TextureHandle tex_ = {};
};

// ─────────────────────────────────────────────────────────────────────────────
//  SpriteGenApp
// ─────────────────────────────────────────────────────────────────────────────
struct SpriteGenApp
{
    // ── Data ──────────────────────────────────────────────────────────────
    SpriteProject          project_;
    SpritePreviewRenderer  renderer_;
    std::string            logText_;
    BuGUI::IconAtlas       iconAtlas_;    // for FileDialog folder/file icons

    // ── BuGUI widgets (owned by widget tree) ──────────────────────────────
    BuGUI::Widget*        root_         = nullptr;
    BuGUI::DockPanel*     dock_         = nullptr;
    PreviewImageWidget*   previewWid_   = nullptr;
    BuGUI::Label*         statusLabel_  = nullptr;
    BuGUI::Label*         logLabel_     = nullptr;
    BuGUI::ComboBox*      animClipCb_   = nullptr;
    BuGUI::ListBox*       surfaceList_  = nullptr;
    BuGUI::Label*         modelPathLbl_ = nullptr;
    BuGUI::FileDialog*    fileDialog_   = nullptr;

    // ── Slider pointers for model transform (updated when reset) ──────────
    BuGUI::SpinBox* mdlPosX_ = nullptr; BuGUI::SpinBox* mdlPosY_ = nullptr;
    BuGUI::SpinBox* mdlPosZ_ = nullptr;
    BuGUI::SpinBox* mdlRotX_ = nullptr; BuGUI::SpinBox* mdlRotY_ = nullptr;
    BuGUI::SpinBox* mdlRotZ_ = nullptr;
    BuGUI::SpinBox* mdlScaX_ = nullptr; BuGUI::SpinBox* mdlScaY_ = nullptr;
    BuGUI::SpinBox* mdlScaZ_ = nullptr;

    // ── State ─────────────────────────────────────────────────────────────
    enum class FileAction { None, LoadModel, LoadWeapon, LoadTexture };
    FileAction     fileAction_         = FileAction::None;
    bool           pendingShowDialog_  = false;   // deferred: create/show outside signal
    int            textureTargetSurf   = -1;
    SpritePreviewViewMode activeView_ = SpritePreviewViewMode::Front;
    size_t         lastAnimCount_     = 0;   // detect when model loads

    // ── Lifecycle ─────────────────────────────────────────────────────────
    bool setup();
    void update(float dt);
    void renderPreview();
    void shutdown();

private:
    // ── Layout helpers ────────────────────────────────────────────────────
    void buildMenuBar(BuGUI::Widget* parent);
    void buildSidebar(BuGUI::Widget* parent);
    void buildControlsPanel(BuGUI::Widget* parent);
    void buildLogPanel(BuGUI::Widget* parent);

    // ── Section helpers ───────────────────────────────────────────────────
    void sectionHeader(BuGUI::BoxLayout* col, const char* title);
    BuGUI::SpinBox* labeledSpin(BuGUI::BoxLayout* col, const char* lbl,
                                float minV, float maxV, float val, float step,
                                int dec, std::function<void(float)> cb);
    BuGUI::CheckBox* labeledCheck(BuGUI::BoxLayout* col, const char* lbl,
                                  bool val, std::function<void(bool)> cb);

    // ── Commands ──────────────────────────────────────────────────────────
    void openFileDialog(FileAction action);
    void appendLog(const std::string& line);
    void rebuildAnimCombo();
    void rebuildSurfaceList();
};

// ─── setup ───────────────────────────────────────────────────────────────────
bool SpriteGenApp::setup()
{
    appendLog("[ui] BuGUI sprite generator ready");

    root_ = new BuGUI::Widget();
    BuGUI::WidgetApp::instance().setRoot(root_);

    auto* vbox = root_->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Vertical);
    vbox->setStretch(1);
    vbox->setSpacing(0.0f);

    buildMenuBar(vbox);

    dock_ = vbox->createChild<BuGUI::DockPanel>();
    dock_->setStretch(1);

    // ── Add panels ────────────────────────────────────────────────────────
    auto* sidebarRoot = dock_->addPanel<BuGUI::BoxLayout>("Sidebar", BuGUI::LayoutDir::Vertical);
    buildSidebar(sidebarRoot);

    previewWid_ = dock_->addPanel<PreviewImageWidget>("Preview");

    auto* controlsRoot = dock_->addPanel<BuGUI::BoxLayout>("Controls", BuGUI::LayoutDir::Vertical);
    buildControlsPanel(controlsRoot);

    auto* logRoot = dock_->addPanel<BuGUI::BoxLayout>("Log", BuGUI::LayoutDir::Vertical);
    buildLogPanel(logRoot);

    // ── Split layout ──────────────────────────────────────────────────────
    dock_->splitOff("Sidebar",   BuGUI::DockSide::Left,   0.22f);
    dock_->splitOff("Controls",  BuGUI::DockSide::Right,  0.28f);
    dock_->splitOff("Log",       BuGUI::DockSide::Bottom, 0.15f);

    // ── IconAtlas — build sprite sheet and upload to GPU ──────────────────
    {
        BuGUI::BuImage* img = iconAtlas_.buildImage(24);
        if (img && img->IsValid())
        {
            BuGUI::TextureHandle h = s_device.BuGUICreateTexture(
                img->width, img->height, img->pixels);
            iconAtlas_.setTexture(h, img->width, img->height);
            delete img;
        }
    }

    return true;
}

// ─── buildMenuBar ─────────────────────────────────────────────────────────────
void SpriteGenApp::buildMenuBar(BuGUI::Widget* parent)
{
    auto* bar = parent->createChild<BuGUI::MenuBar>();

    auto* file = bar->addMenu("File");
    file->addAction("Load Model…",   [this]() { openFileDialog(FileAction::LoadModel); });
    file->addAction("Load Weapon…",  [this]() { openFileDialog(FileAction::LoadWeapon); });
    file->addSeparator();
    file->addAction("Quit", []() { s_device.SetShouldClose(true); })->setShortcut("Alt+F4");

    auto* view = bar->addMenu("View");
    view->addAction("Front",  [this]() { activeView_ = SpritePreviewViewMode::Front; });
    view->addAction("Side",   [this]() { activeView_ = SpritePreviewViewMode::Side; });
    view->addAction("Top",    [this]() { activeView_ = SpritePreviewViewMode::Top; });
    view->addAction("Custom", [this]() { activeView_ = SpritePreviewViewMode::Custom; });

    auto* help = bar->addMenu("Help");
    help->addAction("About", []() {
        BuGUI::AlertDialog::show("Sprite Generator", "BuGUI-based sprite generator.\nLoad an animated model and export sprites.");
    });
}

// ─── section/widget helpers ───────────────────────────────────────────────────
void SpriteGenApp::sectionHeader(BuGUI::BoxLayout* col, const char* title)
{
    auto* lbl = col->createChild<BuGUI::Label>(std::string("─── ") + title + " ───");
    lbl->setColor(BuGUI::Color(180, 195, 220, 255));
}

BuGUI::SpinBox* SpriteGenApp::labeledSpin(BuGUI::BoxLayout* col, const char* lbl,
                                           float minV, float maxV, float val, float step,
                                           int dec, std::function<void(float)> cb)
{
    auto* row  = col->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    row->setSpacing(4.0f);
    auto* label = row->createChild<BuGUI::Label>(lbl);
    (void)label;
    auto* spin  = row->createChild<BuGUI::SpinBox>(minV, maxV, val, step);
    spin->setDecimals(dec);
    spin->setStretch(1);
    spin->onValueChanged.connect(std::move(cb));
    return spin;
}

BuGUI::CheckBox* SpriteGenApp::labeledCheck(BuGUI::BoxLayout* col, const char* lbl,
                                              bool val, std::function<void(bool)> cb)
{
    auto* cb_w = col->createChild<BuGUI::CheckBox>(lbl);
    cb_w->setChecked(val);
    cb_w->toggled.connect(std::move(cb));
    return cb_w;
}

// ─── buildSidebar (left) ──────────────────────────────────────────────────────
void SpriteGenApp::buildSidebar(BuGUI::Widget* parent)
{
    auto* sv  = parent->createChild<BuGUI::ScrollView>();
    sv->setStretch(1);
    auto* col = sv->setContent<BuGUI::BoxLayout>(BuGUI::LayoutDir::Vertical);
    col->setSpacing(4.0f);
    col->setPadding(6.0f);

    // ── Project ───────────────────────────────────────────────────────────
    sectionHeader(col, "Project");

    col->createChild<BuGUI::Label>("Project Name:");
    auto* nameTI = col->createChild<BuGUI::TextInput>(project_.projectName);
    nameTI->submitted.connect([this](const std::string& s) { project_.projectName = s; });

    col->createChild<BuGUI::Label>("Model:");
    modelPathLbl_ = col->createChild<BuGUI::Label>(
        project_.modelPath.empty() ? "(none)"
            : std::filesystem::path(project_.modelPath).filename().string());

    auto* loadRow = col->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    loadRow->setSpacing(4.0f);
    auto* loadBtn = loadRow->createChild<BuGUI::Button>("Load Model…");
    loadBtn->clicked.connect([this]() { openFileDialog(FileAction::LoadModel); });
    auto* reloadBtn = loadRow->createChild<BuGUI::Button>("Reload");
    reloadBtn->clicked.connect([this]()
    {
        std::string msg;
        if (renderer_.reloadModel(project_, &msg))
        {
            rebuildAnimCombo();
            rebuildSurfaceList();
            appendLog("[model] " + msg);
        }
        else appendLog("[error] " + msg);
    });

    col->createChild<BuGUI::Label>("Output:");
    auto* outTI = col->createChild<BuGUI::TextInput>(project_.outputPath);
    outTI->submitted.connect([this](const std::string& s) { project_.outputPath = s; });

    statusLabel_ = col->createChild<BuGUI::Label>("No model loaded");

    // ── Views ─────────────────────────────────────────────────────────────
    sectionHeader(col, "Views");

    static const struct { const char* label; SpritePreviewViewMode mode; } kViews[] = {
        { "Front", SpritePreviewViewMode::Front },
        { "Side",  SpritePreviewViewMode::Side  },
        { "Top",   SpritePreviewViewMode::Top   },
        { "Custom",SpritePreviewViewMode::Custom },
    };
    auto* viewRow = col->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    viewRow->setSpacing(4.0f);
    for (const auto& v : kViews)
    {
        auto* btn = viewRow->createChild<BuGUI::Button>(v.label);
        const SpritePreviewViewMode m = v.mode;
        btn->clicked.connect([this, m]() { activeView_ = m; });
    }

    labeledCheck(col, "Export Front",  project_.viewFront,
                 [this](bool v) { project_.viewFront = v; });
    labeledCheck(col, "Export Side",   project_.viewSide,
                 [this](bool v) { project_.viewSide = v; });
    labeledCheck(col, "Export Top",    project_.viewTop,
                 [this](bool v) { project_.viewTop = v; });
    labeledCheck(col, "Export Custom", project_.viewCustom,
                 [this](bool v) { project_.viewCustom = v; });

    labeledSpin(col, "Yaw",        -180.0f, 180.0f, project_.customPreviewYaw,   1.0f, 0,
                [this](float v) { project_.customPreviewYaw = v; });
    labeledSpin(col, "Pitch",       -90.0f,  90.0f, project_.customPreviewPitch, 1.0f, 0,
                [this](float v) { project_.customPreviewPitch = v; });
    labeledSpin(col, "Zoom",        0.1f,    4.0f,  project_.customPreviewZoom,  0.05f, 2,
                [this](float v) { project_.customPreviewZoom = v; });
    labeledSpin(col, "Ortho Zoom",  0.1f,    4.0f,  project_.orthoPreviewZoom,   0.05f, 2,
                [this](float v) { project_.orthoPreviewZoom = v; });

    // ── Animation ─────────────────────────────────────────────────────────
    sectionHeader(col, "Animation");

    col->createChild<BuGUI::Label>("Clip:");
    animClipCb_ = col->createChild<BuGUI::ComboBox>();
    animClipCb_->addItem("(no model loaded)");
    animClipCb_->selectionChanged.connect([this](int idx)
    {
        const auto& names = renderer_.animationNames();
        if (idx >= 0 && idx < (int)names.size())
        {
            project_.animationName = names[idx];
            project_.frameStart    = 0;
            project_.frameEnd      = renderer_.animationFrameMax(project_.animationName);
            project_.currentFrame  = 0.0f;
            appendLog("[anim] clip " + project_.animationName);
        }
    });

    auto* playRow = col->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    playRow->setSpacing(4.0f);
    labeledCheck(playRow, "Play", project_.animationPlaying,
                 [this](bool v) { project_.animationPlaying = v; });
    labeledCheck(playRow, "Loop", project_.animationLoop,
                 [this](bool v) { project_.animationLoop = v; });

    labeledSpin(col, "FPS",     1.0f,  60.0f, project_.animationFps, 1.0f, 1,
                [this](float v) { project_.animationFps = v; });
    labeledSpin(col, "Start",   0.0f, 999.0f, (float)project_.frameStart, 1.0f, 0,
                [this](float v) { project_.frameStart = (int)v; });
    labeledSpin(col, "End",     0.0f, 999.0f, (float)project_.frameEnd,   1.0f, 0,
                [this](float v) { project_.frameEnd = (int)v; });
    labeledSpin(col, "Frame",   0.0f, 999.0f, project_.currentFrame, 1.0f, 0,
                [this](float v) { project_.currentFrame = v; project_.animationPlaying = false; });

    col->createChild<BuGUI::Label>("Transform channels:");
    auto* chanRow = col->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    chanRow->setSpacing(4.0f);
    labeledCheck(chanRow, "Pos", project_.usePositionChannel,
                 [this](bool v) { project_.usePositionChannel = v; });
    labeledCheck(chanRow, "Rot", project_.useRotationChannel,
                 [this](bool v) { project_.useRotationChannel = v; });
    labeledCheck(chanRow, "Sca", project_.useScaleChannel,
                 [this](bool v) { project_.useScaleChannel = v; });
}

// ─── buildControlsPanel (right) ───────────────────────────────────────────────
void SpriteGenApp::buildControlsPanel(BuGUI::Widget* parent)
{
    auto* sv  = parent->createChild<BuGUI::ScrollView>();
    sv->setStretch(1);
    auto* col = sv->setContent<BuGUI::BoxLayout>(BuGUI::LayoutDir::Vertical);
    col->setSpacing(4.0f);
    col->setPadding(6.0f);

    // ── Render / Model Transform ──────────────────────────────────────────
    sectionHeader(col, "Render");

    col->createChild<BuGUI::Label>("Model Position:");
    mdlPosX_ = labeledSpin(col, "X", -500.0f, 500.0f, project_.modelPosition.x, 0.1f, 2,
                            [this](float v) { project_.modelPosition.x = v; });
    mdlPosY_ = labeledSpin(col, "Y", -500.0f, 500.0f, project_.modelPosition.y, 0.1f, 2,
                            [this](float v) { project_.modelPosition.y = v; });
    mdlPosZ_ = labeledSpin(col, "Z", -500.0f, 500.0f, project_.modelPosition.z, 0.1f, 2,
                            [this](float v) { project_.modelPosition.z = v; });

    col->createChild<BuGUI::Label>("Model Rotation (deg):");
    mdlRotX_ = labeledSpin(col, "X", -360.0f, 360.0f, project_.modelRotation.x, 1.0f, 0,
                            [this](float v) { project_.modelRotation.x = v; });
    mdlRotY_ = labeledSpin(col, "Y", -360.0f, 360.0f, project_.modelRotation.y, 1.0f, 0,
                            [this](float v) { project_.modelRotation.y = v; });
    mdlRotZ_ = labeledSpin(col, "Z", -360.0f, 360.0f, project_.modelRotation.z, 1.0f, 0,
                            [this](float v) { project_.modelRotation.z = v; });

    col->createChild<BuGUI::Label>("Model Scale:");
    mdlScaX_ = labeledSpin(col, "X", 0.01f, 10.0f, project_.modelScale.x, 0.01f, 2,
                            [this](float v) { project_.modelScale.x = v; });
    mdlScaY_ = labeledSpin(col, "Y", 0.01f, 10.0f, project_.modelScale.y, 0.01f, 2,
                            [this](float v) { project_.modelScale.y = v; });
    mdlScaZ_ = labeledSpin(col, "Z", 0.01f, 10.0f, project_.modelScale.z, 0.01f, 2,
                            [this](float v) { project_.modelScale.z = v; });

    // ── Surfaces ──────────────────────────────────────────────────────────
    sectionHeader(col, "Surfaces");

    surfaceList_ = col->createChild<BuGUI::ListBox>();

    auto* surfBtnRow = col->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    surfBtnRow->setSpacing(4.0f);
    auto* pickTexBtn = surfBtnRow->createChild<BuGUI::Button>("Pick Texture…");
    pickTexBtn->clicked.connect([this]()
    {
        textureTargetSurf = surfaceList_ ? surfaceList_->selectedIndex() : -1;
        openFileDialog(FileAction::LoadTexture);
    });
    auto* resetTexBtn = surfBtnRow->createChild<BuGUI::Button>("Reset");
    resetTexBtn->clicked.connect([this]()
    {
        const int idx = surfaceList_ ? surfaceList_->selectedIndex() : -1;
        std::string msg;
        if (!renderer_.resetSurfaceTexture(idx, &msg))
            appendLog("[error] " + msg);
    });

    // ── Attachments ───────────────────────────────────────────────────────
    sectionHeader(col, "Attachments");

    auto* wepRow = col->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    wepRow->setSpacing(4.0f);
    auto* loadWepBtn = wepRow->createChild<BuGUI::Button>("Load Weapon…");
    loadWepBtn->clicked.connect([this]() { openFileDialog(FileAction::LoadWeapon); });

    // ── Export ────────────────────────────────────────────────────────────
    sectionHeader(col, "Export");

    labeledSpin(col, "Width",  1.0f, 4096.0f, (float)project_.spriteWidth,  1.0f, 0,
                [this](float v) { project_.spriteWidth = (int)v; });
    labeledSpin(col, "Height", 1.0f, 4096.0f, (float)project_.spriteHeight, 1.0f, 0,
                [this](float v) { project_.spriteHeight = (int)v; });

    labeledCheck(col, "Transparent Background", project_.transparentBackground,
                 [this](bool v) { project_.transparentBackground = v; });
    labeledCheck(col, "Export Frames",  project_.exportFrames,
                 [this](bool v) { project_.exportFrames = v; });
    labeledCheck(col, "Export Atlas",   project_.exportAtlas,
                 [this](bool v) { project_.exportAtlas = v; });
    labeledCheck(col, "Export JSON",    project_.exportJson,
                 [this](bool v) { project_.exportJson = v; });

    col->createChild<BuGUI::Label>("Prefix:");
    auto* prefixTI = col->createChild<BuGUI::TextInput>(project_.exportPrefix);
    prefixTI->submitted.connect([this](const std::string& s) { project_.exportPrefix = s; });

    auto* expBtn = col->createChild<BuGUI::Button>("Export Sprites");
    expBtn->clicked.connect([this]()
    {
        if (!renderer_.hasLoadedModel())
        { appendLog("[export] no model loaded"); return; }
        appendLog("[export] export not yet wired to pipeline");
    });
}

// ─── buildLogPanel (bottom) ───────────────────────────────────────────────────
void SpriteGenApp::buildLogPanel(BuGUI::Widget* parent)
{
    auto* sv = parent->createChild<BuGUI::ScrollView>();
    sv->setStretch(1);
    logLabel_ = sv->setContent<BuGUI::Label>("(log)");
}

// ─── update ──────────────────────────────────────────────────────────────────
void SpriteGenApp::update(float dt)
{
    renderer_.update(project_, dt);

    // ── Deferred file dialog (must NOT be created inside a signal callback) ──
    if (pendingShowDialog_)
    {
        pendingShowDialog_ = false;
        if (!fileDialog_)
        {
            fileDialog_ = BuGUI::WidgetApp::instance().addFloat<BuGUI::FileDialog>("Open File");
            fileDialog_->setFloatSize(700, 480);
            fileDialog_->setFloatPos(200, 120);
            fileDialog_->setVisible(false);
            fileDialog_->accepted.connect([this](const std::string& path)
            {
                fileDialog_->setVisible(false);
                std::string msg;
                if (fileAction_ == FileAction::LoadModel)
                {
                    project_.modelPath = path;
                    if (renderer_.loadModel(project_, &msg))
                    {
                        rebuildAnimCombo();
                        rebuildSurfaceList();
                        if (modelPathLbl_)
                            modelPathLbl_->setText(std::filesystem::path(path).filename().string());
                        appendLog("[model] " + msg);
                    }
                    else appendLog("[error] " + msg);
                }
                else if (fileAction_ == FileAction::LoadWeapon)
                {
                    if (renderer_.loadWeapon(path, &msg))
                        appendLog("[weapon] loaded");
                    else appendLog("[error] " + msg);
                }
                else if (fileAction_ == FileAction::LoadTexture)
                {
                    if (renderer_.setSurfaceTexture(textureTargetSurf, path, &msg))
                        appendLog("[surface] texture set");
                    else appendLog("[error] " + msg);
                }
                fileAction_ = FileAction::None;
            });
            fileDialog_->cancelled.connect([this]()
            {
                fileDialog_->setVisible(false);
                fileAction_ = FileAction::None;
            });
        }
        fileDialog_->setMode(BuGUI::FileDialog::Mode::Open);
        if (fileAction_ == FileAction::LoadTexture)
            fileDialog_->setFilter("*.png;*.jpg;*.jpeg;*.tga;*.bmp");
        else
            fileDialog_->setFilter("*.h3d;*.md3;*.md2;*.iqm;*.b3d;*.obj");
        fileDialog_->setPath(std::filesystem::current_path().string());
        fileDialog_->setVisible(true);
    }

    // Update status label
    if (statusLabel_)
    {
        const std::string& s = renderer_.statusText();
        statusLabel_->setText(s.empty() ? "No model loaded" : s);
    }

    // Keyboard shortcuts (only when GUI is not capturing input)
    if (!Input::IsGuiBlocked())
    {
        if (Input::IsKeyPressed(KeyCode::KEY_SPACE))
            project_.animationPlaying = !project_.animationPlaying;
        if (Input::IsKeyPressed(KeyCode::KEY_LEFT))
        {
            project_.animationPlaying = false;
            project_.currentFrame = std::max((float)project_.frameStart, project_.currentFrame - 1.0f);
        }
        if (Input::IsKeyPressed(KeyCode::KEY_RIGHT))
        {
            project_.animationPlaying = false;
            project_.currentFrame = std::min((float)project_.frameEnd, project_.currentFrame + 1.0f);
        }
    }
}

// ─── renderPreview ────────────────────────────────────────────────────────────
void SpriteGenApp::renderPreview()
{
    if (!previewWid_) return;

    const int w = std::max(1, (int)previewWid_->rect().w);
    const int h = std::max(1, (int)previewWid_->rect().h);

    Texture* tex = renderer_.renderView(project_, activeView_, w, h);
    if (tex && tex->id)
        previewWid_->setTex(BuGUI::TextureHandle{(uintptr_t)tex->id});
    else
        previewWid_->setTex({});
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void SpriteGenApp::shutdown()
{
    if (fileDialog_)
    {
        BuGUI::WidgetApp::instance().removeFloat(fileDialog_);
        fileDialog_ = nullptr;
    }

    BuGUI::WidgetApp::instance().setRoot(nullptr);
}

// ─── internal helpers ─────────────────────────────────────────────────────────
void SpriteGenApp::openFileDialog(FileAction action)
{
    // Do NOT create or show the dialog here — we are inside a signal callback
    // (WidgetApp::dispatchMouseRelease). Creating a FloatWindow during signal
    // dispatch corrupts its internal vectors. Defer to update().
    fileAction_         = action;
    pendingShowDialog_  = true;
}

void SpriteGenApp::appendLog(const std::string& line)
{
    logText_ += line + "\n";
    if (logLabel_)
        logLabel_->setText(logText_);
}

void SpriteGenApp::rebuildAnimCombo()
{
    if (!animClipCb_) return;
    const auto& names = renderer_.animationNames();
    animClipCb_->clear();
    if (names.empty())
        animClipCb_->addItem("(no clips)");
    else
        for (const auto& n : names)
            animClipCb_->addItem(n);
    animClipCb_->setSelectedIndex(0);
    lastAnimCount_ = names.size();
}

void SpriteGenApp::rebuildSurfaceList()
{
    if (!surfaceList_) return;
    surfaceList_->clearItems();
    for (const auto& s : renderer_.surfaceLabels())
        surfaceList_->addItem(s);
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    (void)argc; (void)argv;

    if (!s_device.Create(1440, 900, "MiniRender Sprite Generator", true))
        return 1;

    s_device.BuGUIInit();

    SpriteGenApp app;
    if (!app.setup())
    {
        s_device.Close();
        return 1;
    }

    while (s_device.Run())
    {
        const float dt = s_device.GetFrameTime();

        // Clear default framebuffer
        {
            const BuGUI::IO& io = BuGUI::GetIO();
            glViewport(0, 0,
                       (GLsizei)(io.displayWidth  * io.framebufferScaleX),
                       (GLsizei)(io.displayHeight * io.framebufferScaleY));
            glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        s_device.BuGUIBegin();
        app.update(dt);

        // Render into the preview FBO before BuGUIEnd reads the texture
        app.renderPreview();

        // Restore viewport for BuGUI rendering
        {
            const BuGUI::IO& io = BuGUI::GetIO();
            glViewport(0, 0,
                       (GLsizei)(io.displayWidth  * io.framebufferScaleX),
                       (GLsizei)(io.displayHeight * io.framebufferScaleY));
        }

        // BuGUIEnd manually so we can pass the IconAtlas (for FileDialog icons)
        {
            const BuGUI::FontAtlas* fa = s_device.GetBuGUIFontAtlas();
            BuGUI::WidgetApp::instance().paint(*BuGUI::GetDrawData(),
                fa ? &fa->defaultFont() : nullptr,
                &app.iconAtlas_);
            BuGUI::Render();
        }
        s_device.Flip();
    }

    app.shutdown();
    s_device.Close();
    return 0;
}
