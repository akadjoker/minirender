#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Core
#include "Batch.hpp"
#include "Camera.hpp"
#include "Device.hpp"
#include "Input.hpp"
#include "RenderState.hpp"
#include "ViewportWidget.hpp"

// BuGUI
#include <BasicWidgets.hpp>
#include <DockPanel.hpp>
#include <GizmoWidgets.hpp>
#include <InputWidgets.hpp>
#include <LayoutWidgets.hpp>
#include <MenuWidgets.hpp>
#include <ScrollWidgets.hpp>
#include <WidgetApp.hpp>

// Editor data & ops (reused from editor/, no ImGui)
#include "EditorBrushGeometryOps.hpp"
#include "EditorBrushOps.hpp"
#include "EditorConvexBrushOps.hpp"
#include "EditorData.hpp"
#include "EditorSceneIO.hpp"
#include "EditorViewOps.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
// Device is a singleton — access via Device::Instance()
static Device& s_device = Device::Instance();

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport — per-view state coupling a ViewportWidget to a Camera
// ─────────────────────────────────────────────────────────────────────────────
struct Viewport
{
    ViewportWidget* widget   = nullptr;   // owned by BuGUI tree
    EditorView      view;                 // camera + focus + orthoSize etc.

    void updateCamera();
};

// ─────────────────────────────────────────────────────────────────────────────
//  EditorApp — all editor state
// ─────────────────────────────────────────────────────────────────────────────
struct EditorApp
{
    // ── BuGUI widgets ─────────────────────────────────────────────────────
    BuGUI::Widget*      stageRoot   = nullptr;
    BuGUI::DockPanel*   dock        = nullptr;
    BuGUI::ListBox*     entityList  = nullptr;
    BuGUI::Label*       statusLabel = nullptr;
    BuGUI::Button*      toolBtns[7] = {};

    // ── Viewports ─────────────────────────────────────────────────────────
    // [0] Perspective   [1] Top   [2] Front   [3] Right
    static constexpr int kMaxViewports = 4;
    Viewport    vps[kMaxViewports];
    int         activeVP     = 0;       // viewport receiving camera inputs
    int         vpCount      = 2;       // how many are shown initially

    // ── Gizmo3D overlay (perspective viewport) ─────────────────────────
    BuGUI::Gizmo3D* gizmo3D = nullptr;
    glm::vec3 gizmoPos   = {0, 0, 0};
    glm::vec3 gizmoRot   = {0, 0, 0};
    glm::vec3 gizmoScale = {1, 1, 1};

    // ── Editor state ──────────────────────────────────────────────────────
    EditorEntity            worldspawn;
    std::vector<EditorEntity> entities;
    EditorTool              currentTool    = EditorTool::Select;
    int                     selectedEntity = -1;   // -1 = worldspawn
    int                     selectedBrush  = -1;
    std::string             currentFile;
    bool                    isDirty        = false;

    // ── Rendering ─────────────────────────────────────────────────────────
    RenderBatch batch;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    bool setup();
    void update(float dt);
    void renderViewports();
    void buildGui();
    void shutdown();

private:
    // ── Layout helpers ────────────────────────────────────────────────────
    void buildMenuBar(BuGUI::Widget* parent);
    void buildToolBar(BuGUI::Widget* parent);
    void buildDock(BuGUI::Widget* parent);

    // ── Rendering helpers ─────────────────────────────────────────────────
    void renderViewport(Viewport& vp);
    void renderGrid(const Viewport& vp, float cellSize, int halfCells);
    void renderBrushWireframes(const Viewport& vp);
    void renderAxes(const Viewport& vp);

    // ── Commands ──────────────────────────────────────────────────────────
    void cmdNew();
    void cmdOpen();
    void cmdSave();
    void cmdSaveAs();
    void cmdNewBrush();           ///< Create a default box brush at focus
    void setTool(EditorTool tool);
    void rebuildEntityList();
    void setStatus(const char* msg);

    // ── Brush helpers ─────────────────────────────────────────────────────
    static void translateBrush(EditorBrush& brush, const glm::vec3& delta);
    EditorBrush* selectedBrushPtr();   ///< nullptr if nothing selected

    // ── Camera input ──────────────────────────────────────────────────────
    void handleCameraInput(Viewport& vp, float dt);
};

// ─── setup ───────────────────────────────────────────────────────────────────
bool EditorApp::setup()
{
    batch.Init();

    // Viewport types
    vps[0].view.type  = EditorViewType::Perspective;
    vps[0].view.label = "Perspective";
    vps[0].view.clearColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
    vps[0].view.perspectiveYaw   = 45.0f;
    vps[0].view.perspectivePitch = 28.0f;
    vps[0].view.perspectiveDistance = 720.0f;

    vps[1].view.type  = EditorViewType::Top;
    vps[1].view.label = "Top";
    vps[1].view.clearColor = glm::vec4(0.10f, 0.10f, 0.12f, 1.0f);
    vps[1].view.orthoSize = 512.0f;

    vps[2].view.type  = EditorViewType::Front;
    vps[2].view.label = "Front";
    vps[2].view.clearColor = glm::vec4(0.10f, 0.10f, 0.12f, 1.0f);
    vps[2].view.orthoSize = 512.0f;

    vps[3].view.type  = EditorViewType::Right;
    vps[3].view.label = "Right";
    vps[3].view.clearColor = glm::vec4(0.10f, 0.10f, 0.12f, 1.0f);
    vps[3].view.orthoSize = 512.0f;

    // ── Stage root ────────────────────────────────────────────────────────
    stageRoot = new BuGUI::Widget();
    BuGUI::WidgetApp::instance().setRoot(stageRoot);

    // Vertical box: [MenuBar] [ToolBar] [DockPanel]
    auto* vbox = stageRoot->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Vertical);
    vbox->setStretch(1);
    vbox->setSpacing(0.0f);

    buildMenuBar(vbox);
    buildToolBar(vbox);
    buildDock(vbox);

    // ── Init data ─────────────────────────────────────────────────────────
    worldspawn.classname = "worldspawn";
    worldspawn.name      = "worldspawn";
    rebuildEntityList();

    return true;
}

// ─── buildMenuBar ─────────────────────────────────────────────────────────────
void EditorApp::buildMenuBar(BuGUI::Widget* parent)
{
    auto* bar = parent->createChild<BuGUI::MenuBar>();

    // File
    auto* file = bar->addMenu("File");
    file->addAction("New",     [this]() { cmdNew(); })->setShortcut("Ctrl+N");
    file->addAction("Open…",   [this]() { cmdOpen(); })->setShortcut("Ctrl+O");
    file->addSeparator();
    file->addAction("Save",    [this]() { cmdSave(); })->setShortcut("Ctrl+S");
    file->addAction("Save As…",[this]() { cmdSaveAs(); })->setShortcut("Ctrl+Shift+S");
    file->addSeparator();
    file->addAction("Quit",    []() { s_device.SetShouldClose(true); })->setShortcut("Alt+F4");

    // Edit
    auto* edit = bar->addMenu("Edit");
    edit->addAction("Undo")->setShortcut("Ctrl+Z");
    edit->addAction("Redo")->setShortcut("Ctrl+Y");
    edit->addSeparator();
    edit->addAction("Select All")->setShortcut("Ctrl+A");
    edit->addAction("Deselect All")->setShortcut("Alt+A");
    edit->addSeparator();
    edit->addAction("Delete Selected")->setShortcut("Del");

    // View
    auto* view = bar->addMenu("View");
    view->addAction("Reset All Cameras", [this]() {
        for (int i = 0; i < vpCount; ++i) {
            vps[i].view.focus = glm::vec3(0.0f);
            vps[i].view.orthoSize = 512.0f;
            vps[i].view.perspectiveDistance = 720.0f;
            vps[i].view.perspectiveYaw   = 45.0f;
            vps[i].view.perspectivePitch = 28.0f;
        }
    });
    view->addSeparator();
    view->addCheckable("Show Grid", true);
    view->addCheckable("Show Axes", true);

    // Build
    auto* build = bar->addMenu("Build");
    build->addAction("Compile Map")->setShortcut("F7");
    build->addAction("Run Map")->setShortcut("F5");
}

// ─── buildToolBar ─────────────────────────────────────────────────────────────
void EditorApp::buildToolBar(BuGUI::Widget* parent)
{
    auto* row = parent->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    row->setSpacing(2.0f);
    row->setPadding(3.0f);

    static const struct { EditorTool tool; const char* label; const char* tip; } kTools[] = {
        { EditorTool::Select, "Select",    "Select brushes  [Q]"    },
        { EditorTool::Move,   "Move",      "Move selection  [W]"    },
        { EditorTool::Rotate, "Rotate",    "Rotate selection [E]"   },
        { EditorTool::Scale,  "Scale",     "Scale selection  [R]"   },
        { EditorTool::Face,   "Face",      "Edit faces      [F]"    },
        { EditorTool::Clip,   "Clip",      "Clip brush      [C]"    },
        { EditorTool::Brush,  "New Brush", "Draw new brush  [B]"    },
    };

    for (int i = 0; i < 7; ++i)
    {
        toolBtns[i] = row->createChild<BuGUI::Button>(kTools[i].label);
        const EditorTool t = kTools[i].tool;
        if (t == EditorTool::Brush)
            toolBtns[i]->clicked.connect([this]() { cmdNewBrush(); });
        else
            toolBtns[i]->clicked.connect([this, t]() { setTool(t); });
    }

    // Spacer + status
    row->createChild<BuGUI::Label>(" | ");
    statusLabel = row->createChild<BuGUI::Label>("Ready");
}

// ─── buildDock ────────────────────────────────────────────────────────────────
void EditorApp::buildDock(BuGUI::Widget* parent)
{
    dock = parent->createChild<BuGUI::DockPanel>();
    dock->setStretch(1);

    // ── Perspective viewport (main 3D) ────────────────────────────────────
    vps[0].widget = dock->addPanel<ViewportWidget>("Perspective");
    vps[0].widget->onResized.connect([this](int w, int h) {
        vps[0].view.camera.setViewport(0, 0, w, h);
        vps[0].view.camera.setProjectionType(ProjectionType::Perspective);
        vps[0].view.camera.setFov(60.0f);
        vps[0].view.camera.setViewPlanes(1.0f, 65536.0f);
        vps[0].view.camera.setAspect(w, h);
        vps[0].view.camera.updateMatrices();
    });

    // ── Top ortho viewport ────────────────────────────────────────────────
    vps[1].widget = dock->addPanel<ViewportWidget>("Top");
    vps[1].widget->onResized.connect([this](int w, int h) {
        vps[1].view.camera.setViewport(0, 0, w, h);
        vps[1].view.camera.setProjectionType(ProjectionType::Orthographic);
    });

    // ── Scene / entity list ───────────────────────────────────────────────
    entityList = dock->addPanel<BuGUI::ListBox>("Scene");
    entityList->selectionChanged.connect([this](int idx) {
        selectedEntity = idx - 1;  // idx 0 = worldspawn → -1, rest = entities[n]
        selectedBrush  = -1;
    });

    // ── Properties panel ──────────────────────────────────────────────────
    auto* props = dock->addPanel<BuGUI::BoxLayout>("Properties", BuGUI::LayoutDir::Vertical);
    props->setSpacing(6.0f);
    props->setPadding(6.0f);
    props->createChild<BuGUI::Label>("Properties");
    props->createChild<BuGUI::Label>("");
    props->createChild<BuGUI::Label>("Select an entity or brush");
    props->createChild<BuGUI::Label>("to view its properties.");

    // ── Initial dock split (Blender-style) ────────────────────────────────
    dock->splitOff("Scene",      BuGUI::DockSide::Left,   0.17f);
    dock->splitOff("Top",        BuGUI::DockSide::Bottom, 0.40f);
    dock->splitOff("Properties", BuGUI::DockSide::Right,  0.22f);

    // ── Gizmo3D as child of the perspective viewport ──────────────────────
    // This scopes it to the viewport rect so it doesn't block the whole window.
    gizmo3D = vps[0].widget->createChild<BuGUI::Gizmo3D>();
    gizmo3D->setMode(BuGUI::GizmoMode3D::Translate);
    gizmo3D->setSnapTranslate(8.0f);   // 8-unit grid snap
    gizmo3D->setVisible(false);        // hidden until a brush is selected

    // When the gizmo reports a world-space translation delta, move the brush
    gizmo3D->onTranslate3D.connect([this](BuGUI::Vec3f d)
    {
        EditorBrush* sel = selectedBrushPtr();
        if (!sel) return;
        translateBrush(*sel, glm::vec3(d.x, d.y, d.z));
        isDirty = true;
    });
}

// ─── update ──────────────────────────────────────────────────────────────────
void EditorApp::update(float dt)
{
    // Keyboard shortcuts for tool selection
    if (Input::IsKeyPressed(KeyCode::KEY_Q)) setTool(EditorTool::Select);
    if (Input::IsKeyPressed(KeyCode::KEY_W)) setTool(EditorTool::Move);
    if (Input::IsKeyPressed(KeyCode::KEY_E)) setTool(EditorTool::Rotate);
    if (Input::IsKeyPressed(KeyCode::KEY_R)) setTool(EditorTool::Scale);
    if (Input::IsKeyPressed(KeyCode::KEY_F)) setTool(EditorTool::Face);
    if (Input::IsKeyPressed(KeyCode::KEY_C)) setTool(EditorTool::Clip);
    if (Input::IsKeyPressed(KeyCode::KEY_B)) cmdNewBrush();

    // Camera navigation per viewport
    for (int i = 0; i < vpCount; ++i)
        handleCameraInput(vps[i], dt);

    // Update gizmo3D position to follow selected brush (if any)
    if (gizmo3D)
    {
        const Viewport& vp = vps[0];  // perspective
        if (vp.widget && vp.widget->isReady())
        {
            gizmo3D->setViewProjection(
                glm::value_ptr(vp.view.camera.view),
                glm::value_ptr(vp.view.camera.projection),
                vp.widget->rtWidth(), vp.widget->rtHeight());

            // Sync gizmo position from the selected brush's interior point
            EditorBrush* sel = selectedBrushPtr();
            if (sel)
            {
                gizmoPos = convexBrushInteriorPoint(*sel);
                gizmo3D->setTarget(&gizmoPos.x, &gizmoRot.x, &gizmoScale.x);
                gizmo3D->setVisible(true);
            }
            else
            {
                gizmo3D->setVisible(false);
            }
        }
    }
}

// ─── renderViewports ─────────────────────────────────────────────────────────
//  Called BEFORE BuGUIEnd() so ViewportWidget::paint() can read the FBO.
void EditorApp::renderViewports()
{
    for (int i = 0; i < vpCount; ++i)
        renderViewport(vps[i]);
}

void EditorApp::renderViewport(Viewport& vp)
{
    if (!vp.widget || !vp.widget->isReady()) return;

    vp.updateCamera();

    RenderTarget* rt = vp.widget->renderTarget();
    rt->bind();

    const glm::vec4& bg = vp.view.clearColor;
    glClearColor(bg.r, bg.g, bg.b, bg.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, vp.widget->rtWidth(), vp.widget->rtHeight());

    renderGrid(vp, 64.0f, 16);
    renderAxes(vp);
    renderBrushWireframes(vp);

    rt->unbind();
}

void EditorApp::renderGrid(const Viewport& vp, float cellSize, int halfCells)
{
    const float extent = cellSize * (float)halfCells;

    batch.SetMatrix(vp.view.camera.viewProjection);

    // Minor grid lines
    batch.SetColor(0.20f, 0.20f, 0.22f);
    for (int i = -halfCells; i <= halfCells; ++i)
    {
        const float f = (float)i * cellSize;
        batch.Line3D({f, 0.f, -extent}, {f, 0.f,  extent});
        batch.Line3D({-extent, 0.f, f}, { extent, 0.f, f});
    }

    // Major axes on the grid plane (brighter)
    batch.SetColor(0.35f, 0.15f, 0.15f);
    batch.Line3D({-extent, 0.f, 0.f}, {extent, 0.f, 0.f});  // X axis
    batch.SetColor(0.15f, 0.15f, 0.35f);
    batch.Line3D({0.f, 0.f, -extent}, {0.f, 0.f, extent});  // Z axis

    batch.Render();
}

void EditorApp::renderAxes(const Viewport& vp)
{
    batch.SetMatrix(vp.view.camera.viewProjection);

    // World origin XYZ axes
    batch.SetColor(0.80f, 0.10f, 0.10f); batch.Line3D({0,0,0}, {128,0,0});   // +X red
    batch.SetColor(0.10f, 0.80f, 0.10f); batch.Line3D({0,0,0}, {0,128,0});   // +Y green
    batch.SetColor(0.10f, 0.10f, 0.80f); batch.Line3D({0,0,0}, {0,0,128});   // +Z blue

    batch.Render();
}

void EditorApp::renderBrushWireframes(const Viewport& vp)
{
    if (worldspawn.convexBrushes.empty() && entities.empty()) return;

    batch.SetMatrix(vp.view.camera.viewProjection);

    auto drawBrush = [&](const EditorBrush& brush, bool selected)
    {
        if (brush.hidden) return;
        const glm::vec3& c = brush.color;
        batch.SetColor(selected ? 1.0f : c.r,
                       selected ? 0.6f : c.g,
                       selected ? 0.0f : c.b);

        for (const auto& polygon : buildConvexFacePolygons(brush))
        {
            for (size_t i = 0; i < polygon.vertices.size(); ++i)
            {
                const glm::vec3& a = polygon.vertices[i];
                const glm::vec3& b = polygon.vertices[(i + 1) % polygon.vertices.size()];
                batch.Line3D(a, b);
            }
        }
    };

    for (size_t i = 0; i < worldspawn.convexBrushes.size(); ++i)
        drawBrush(worldspawn.convexBrushes[i], (selectedEntity == -1 && (int)i == selectedBrush));

    for (size_t ei = 0; ei < entities.size(); ++ei)
        for (size_t bi = 0; bi < entities[ei].convexBrushes.size(); ++bi)
            drawBrush(entities[ei].convexBrushes[bi],
                      ((int)ei == selectedEntity && (int)bi == selectedBrush));

    batch.Render();
}

// ─── buildGui ────────────────────────────────────────────────────────────────
//  Update BuGUI widget state (called each frame, BEFORE renderViewports).
void EditorApp::buildGui()
{
    if (statusLabel)
    {
        const char* toolNames[] = { "Select", "Move", "Rotate", "Scale", "Face", "Clip", "New Brush" };
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s  |  %s",
                      toolNames[(int)currentTool],
                      isDirty ? "● unsaved" : "Saved");
        statusLabel->setText(buf);
    }
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void EditorApp::shutdown()
{
    gizmo3D = nullptr;  // owned by stageRoot tree, freed when setRoot(nullptr) below

    stageRoot  = nullptr;
    dock       = nullptr;
    entityList = nullptr;
    statusLabel = nullptr;
    for (int i = 0; i < 7; ++i) toolBtns[i] = nullptr;
    for (int i = 0; i < kMaxViewports; ++i) vps[i].widget = nullptr;

    BuGUI::WidgetApp::instance().setRoot(nullptr);
    batch.Release();
}

// ─── Brush helpers ───────────────────────────────────────────────────────────

void EditorApp::translateBrush(EditorBrush& brush, const glm::vec3& delta)
{
    for (auto& face : brush.faces)
        for (auto& pt : face.planePoints)
            pt += delta;
}

EditorBrush* EditorApp::selectedBrushPtr()
{
    if (selectedEntity == -1)
    {
        if (selectedBrush >= 0 && selectedBrush < (int)worldspawn.convexBrushes.size())
            return &worldspawn.convexBrushes[selectedBrush];
    }
    else if (selectedEntity >= 0 && selectedEntity < (int)entities.size())
    {
        auto& ent = entities[selectedEntity];
        if (selectedBrush >= 0 && selectedBrush < (int)ent.convexBrushes.size())
            return &ent.convexBrushes[selectedBrush];
    }
    return nullptr;
}

// ─── Commands ────────────────────────────────────────────────────────────────
void EditorApp::cmdNew()
{
    worldspawn = {};
    worldspawn.classname = "worldspawn";
    worldspawn.name      = "worldspawn";
    entities.clear();
    currentFile.clear();
    selectedEntity = -1;
    selectedBrush  = -1;
    isDirty        = false;
    rebuildEntityList();
    setStatus("New scene");
}

void EditorApp::cmdOpen()
{
    // TODO: file dialog (BuGUI FileDialog widget when ready)
    setStatus("Open: use file dialog (coming soon)");
}

void EditorApp::cmdSave()
{
    if (currentFile.empty()) { cmdSaveAs(); return; }

    // Combine worldspawn + entities for save
    std::vector<EditorEntity> all;
    all.push_back(worldspawn);
    all.insert(all.end(), entities.begin(), entities.end());
    glm::vec3 focusForSave = vps[0].view.focus;
    std::string saveError;
    if (saveEditorScene(currentFile, all, focusForSave, std::string{}, saveError))
    {
        isDirty = false;
        setStatus("Saved.");
    }
    else
        setStatus("Save failed!");
}

void EditorApp::cmdSaveAs()
{
    // TODO: file dialog
    setStatus("Save As: use file dialog (coming soon)");
}

void EditorApp::cmdNewBrush()
{
    setTool(EditorTool::Brush);

    // Create a 128-unit box centred on the focus of the perspective viewport
    const glm::vec3& focus = vps[0].view.focus;
    const float half = 64.0f;
    const glm::vec3 mins = focus - glm::vec3(half, 0.0f,  half);
    const glm::vec3 maxs = focus + glm::vec3(half, half * 2.0f, half);

    EditorBrush brush = makeBoxConvexBrush(mins, maxs,
                                           defaultBrushName((int)worldspawn.convexBrushes.size()));
    brush.color = randomBrushColor();

    worldspawn.convexBrushes.push_back(std::move(brush));
    selectedEntity = -1;
    selectedBrush  = (int)worldspawn.convexBrushes.size() - 1;
    isDirty        = true;
    setStatus("New box brush created");
}

void EditorApp::setTool(EditorTool tool)
{
    currentTool = tool;
    if (gizmo3D)
    {
        switch (tool)
        {
        case EditorTool::Move:   gizmo3D->setMode(BuGUI::GizmoMode3D::Translate); break;
        case EditorTool::Rotate: gizmo3D->setMode(BuGUI::GizmoMode3D::Rotate);    break;
        case EditorTool::Scale:  gizmo3D->setMode(BuGUI::GizmoMode3D::Scale);     break;
        default: break;
        }
    }
}

void EditorApp::rebuildEntityList()
{
    if (!entityList) return;
    entityList->clearItems();
    entityList->addItem("[World] " + worldspawn.name);
    for (size_t i = 0; i < entities.size(); ++i)
    {
        const std::string name = entities[i].name.empty()
            ? entities[i].classname + " " + std::to_string((int)i + 1)
            : entities[i].name;
        entityList->addItem(name);
    }
}

void EditorApp::setStatus(const char* msg)
{
    if (statusLabel) statusLabel->setText(msg);
}

// ─── Camera input ─────────────────────────────────────────────────────────────
void EditorApp::handleCameraInput(Viewport& vp, float dt)
{
    if (!vp.widget) return;

    // Only process mouse input when not captured by a BuGUI widget
    if (Input::IsGuiBlocked()) return;

    const glm::vec2 delta = Input::GetMouseDelta();
    const float     wheel = Input::GetMouseWheelMove().y;
    EditorView&     ev    = vp.view;

    if (ev.type == EditorViewType::Perspective)
    {
        if (Input::IsMouseDown(MouseButton::MIDDLE))
        {
            if (Input::IsKeyDown(KeyCode::KEY_LEFT_SHIFT))
                panFocusInPerspective(ev.focus, ev, delta);
            else
            {
                ev.perspectiveYaw   += delta.x * 0.4f;
                ev.perspectivePitch -= delta.y * 0.4f;
                ev.perspectivePitch  = glm::clamp(ev.perspectivePitch, -89.0f, 89.0f);
            }
        }
        if (wheel != 0.0f)
            ev.perspectiveDistance = glm::max(1.0f, ev.perspectiveDistance - wheel * ev.perspectiveDistance * 0.1f);
    }
    else
    {
        if (Input::IsMouseDown(MouseButton::MIDDLE))
            panFocusInOrthoView(ev.focus, ev, delta);
        if (wheel != 0.0f)
            ev.orthoSize = glm::max(8.0f, ev.orthoSize - wheel * ev.orthoSize * 0.1f);
    }
}

// Override Viewport::updateCamera to call updateCameras (takes std::array<EditorView,4>)
void Viewport::updateCamera()
{
    if (!widget || !widget->isReady()) return;
    const int w = widget->rtWidth();
    const int h = widget->rtHeight();
    if (w < 1 || h < 1) return;

    view.rect = {0, 0, w, h};
    view.camera.setViewport(0, 0, w, h);

    std::array<EditorView, 4> arr = {view, view, view, view};
    updateCameras(arr, view.focus);
    view.camera = arr[0].camera;

    view.camera.updateMatrices();
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    (void)argc; (void)argv;

    if (!s_device.Create(1280, 720, "Level Editor", true))
        return 1;

    s_device.BuGUIInit();

    EditorApp app;
    if (!app.setup())
    {
        s_device.Close();
        return 1;
    }

    while (s_device.Run())
    {
        const float dt = s_device.GetFrameTime();

        // Clear the default framebuffer each frame (dark background)
        {
            const BuGUI::IO& io = BuGUI::GetIO();
            const GLsizei dw = static_cast<GLsizei>(io.displayWidth  * io.framebufferScaleX);
            const GLsizei dh = static_cast<GLsizei>(io.displayHeight * io.framebufferScaleY);
            glViewport(0, 0, dw, dh);
            glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        s_device.BuGUIBegin();
        app.update(dt);
        app.buildGui();
        app.renderViewports();
        // Restore the full-window GL viewport.
        // renderViewports() leaves glViewport set to the last FBO's size,
        // which causes BuGUI to render only in a portion of the screen.
        {
            const BuGUI::IO& io = BuGUI::GetIO();
            glViewport(0, 0,
                       static_cast<GLsizei>(io.displayWidth  * io.framebufferScaleX),
                       static_cast<GLsizei>(io.displayHeight * io.framebufferScaleY));
        }
        s_device.BuGUIEnd();
        s_device.Flip();         // BuGUI renderer → screen + SDL swap
    }

    app.shutdown();
    s_device.Close();
    return 0;
}
