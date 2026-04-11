// Level Editor — main entry point
// A visual editor for placing objects, editing tiles, and configuring layers
// using the GameBu graphics engine (Scene, Entity, GraphLib).

#include "SceneDocument.h"
#include "Viewport.h"
#include "Gizmo2D.h"
#include "Panels.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <raylib.h>
#include <rlgl.h>
#include <engine.hpp>

#include <cstdio>
#include <string>
#include <filesystem>
#include <algorithm>

// ── rlImGui minimal integration ─────────────────────────────────────────────
// We reuse the same rlImGui backend from gameeditor.
#include "rlImGui.h"
// Font Awesome icons (optional, graceful degradation)
#include "ImGuiFontAwesome.h"
// Embedded font
#include "DejaVuSansMono_embedded.h"

// ── Globals ─────────────────────────────────────────────────────────────────
static GraphLib gGraphLib;

// Draw a graph from GraphLib into the viewport (called by Viewport::DrawObjects)
static void DrawGraphCallback(int graphId, float x, float y, float angle,
                               float sx, float sy, Color tint, bool flipX, bool flipY)
{
    if (graphId < 0 || graphId >= (int)gGraphLib.graphs.size())
    {
        // Placeholder
        DrawRectangleLines((int)(x - 16), (int)(y - 16), 32, 32, tint);
        return;
    }

    const Graph& g = gGraphLib.graphs[graphId];
    if (g.texture < 0 || g.texture >= (int)gGraphLib.textures.size())
    {
        DrawRectangleLines((int)(x - 16), (int)(y - 16), 32, 32, tint);
        return;
    }

    const Texture2D& tex = gGraphLib.textures[g.texture];
    float scaleX = sx / 100.0f;
    float scaleY = sy / 100.0f;
    if (flipX) scaleX = -scaleX;
    if (flipY) scaleY = -scaleY;

    float cx = (g.points.size() > 0) ? g.points[0].x : g.clip.width / 2.0f;
    float cy = (g.points.size() > 0) ? g.points[0].y : g.clip.height / 2.0f;

    Rectangle src = g.clip;
    if (flipX) { src.x += src.width; src.width = -src.width; }
    if (flipY) { src.y += src.height; src.height = -src.height; }

    Rectangle dst = {
        x, y,
        g.clip.width * std::abs(scaleX),
        g.clip.height * std::abs(scaleY)
    };

    DrawTexturePro(tex, src, dst, { cx * std::abs(scaleX), cy * std::abs(scaleY) }, angle, tint);
}

// ── Utility ─────────────────────────────────────────────────────────────────

static std::filesystem::path GetExecutableDirectory(const char* argv0)
{
    std::error_code ec;
    auto p = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    if (argv0)
    {
        auto q = std::filesystem::absolute(argv0, ec);
        if (!ec) return q.parent_path();
    }
    return std::filesystem::current_path();
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════
int main(int argc, char** argv)
{
    const std::filesystem::path exe_dir     = GetExecutableDirectory(argc > 0 ? argv[0] : nullptr);
    const std::filesystem::path project_dir = exe_dir.parent_path();

    // ── raylib window ──
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_VSYNC_HINT);
    InitWindow(1440, 900, "BuLevel — Level Editor");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    // ── ImGui ──
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Load embedded font with extended glyph ranges
    static const ImWchar glyph_ranges[] = {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0x0180, 0x024F,
        0x2000, 0x206F,
        0,
    };
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(
        (void*)DejaVuSansMono_ttf_data, DejaVuSansMono_ttf_size, 15.0f, &font_cfg, glyph_ranges);

    if (!rlImGuiSetup(true))
    {
        std::fprintf(stderr, "rlImGuiSetup failed\n");
        ImGui::DestroyContext();
        CloseWindow();
        return 1;
    }

    // ── Scene document ──
    le::SceneDocument doc;
    doc.EnsureDefaultLayers();

    // ── Editor state ──
    le::Viewport           viewport;
    le::HierarchyPanel     hierarchy;
    le::InspectorPanel     inspector;
    le::Toolbar            toolbar;
    le::SceneSettingsPopup sceneSettings;
    le::BottomPanel        bottomPanel;
    le::Gizmo2D            gizmo;

    int      activeLayer = 1;        // "Main" by default
    uint32_t selectedUID = 0;
    std::string status   = "Ready";
    std::string scenePath;
    bool     done        = false;

    // For the "Create" tool — graph ID to stamp
    int createGraphId = 0;

    // Right-click context menu state
    ImVec2 contextMenuWorldPos = {};  // world position where right-click happened
    uint32_t contextMenuTargetUID = 0; // object under cursor when right-clicked (0 = empty space)

    viewport.Init(800, 600);

    // ── Main loop ──
    while (!done)
    {
        if (WindowShouldClose()) done = true;

        // ── Keyboard shortcuts ──
        {
            bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (ctrl && IsKeyPressed(KEY_S))
            {
                if (scenePath.empty())
                {
                    scenePath = (project_dir / "scripts" / (doc.name + ".buscene")).string();
                }
                if (doc.SaveToFile(scenePath))
                    status = "Saved: " + scenePath;
                else
                    status = "Save failed!";
            }
            if (ctrl && IsKeyPressed(KEY_O))
            {
                // Simple open — for now just look for .buscene in scripts/
                // TODO: file dialog
                status = "Use File > Open to load a scene";
            }
            if (ctrl && IsKeyPressed(KEY_N))
            {
                doc = le::SceneDocument();
                doc.EnsureDefaultLayers();
                selectedUID = 0;
                scenePath.clear();
                status = "New scene";
            }
            if (ctrl && IsKeyPressed(KEY_Z))
            {
                status = "Undo not yet implemented";
            }
            // Delete selected
            if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && selectedUID != 0)
            {
                for (auto& layer : doc.layers)
                {
                    auto it = std::find_if(layer.objects.begin(), layer.objects.end(),
                        [&](const le::SceneObject& o) { return o.uid == selectedUID; });
                    if (it != layer.objects.end())
                    {
                        layer.objects.erase(it);
                        selectedUID = 0;
                        doc.dirty = true;
                        status = "Object deleted";
                        break;
                    }
                }
            }
            // Tool shortcuts
            if (IsKeyPressed(KEY_S) && !ctrl) toolbar.activeTool = le::EditorTool::Select;
            if (IsKeyPressed(KEY_G))          toolbar.activeTool = le::EditorTool::Move;
            if (IsKeyPressed(KEY_A))          toolbar.activeTool = le::EditorTool::Create;

            // Gizmo mode shortcuts
            if (IsKeyPressed(KEY_W)) { toolbar.gizmoMode = le::GizmoMode::Translate; status = "Gizmo: Translate"; }
            if (IsKeyPressed(KEY_E)) { toolbar.gizmoMode = le::GizmoMode::Rotate;    status = "Gizmo: Rotate"; }
            if (IsKeyPressed(KEY_R)) { toolbar.gizmoMode = le::GizmoMode::Scale;     status = "Gizmo: Scale"; }
        }

        BeginDrawing();
        ClearBackground({ 30, 30, 35, 255 });
        rlImGuiBegin();

        // ── Menu bar ──
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New", "Ctrl+N"))
                {
                    doc = le::SceneDocument();
                    doc.EnsureDefaultLayers();
                    selectedUID = 0;
                    scenePath.clear();
                    status = "New scene";
                }
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                {
                    status = "File dialog coming soon";
                }
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    if (scenePath.empty())
                        scenePath = (project_dir / "scripts" / (doc.name + ".buscene")).string();
                    if (doc.SaveToFile(scenePath))
                        status = "Saved: " + scenePath;
                    else
                        status = "Save failed!";
                }
                if (ImGui::MenuItem("Save As..."))
                {
                    status = "Save As dialog coming soon";
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Scene Settings..."))
                {
                    sceneSettings.visible = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    done = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z"))
                    status = "Undo not yet implemented";
                if (ImGui::MenuItem("Redo", "Ctrl+Y"))
                    status = "Redo not yet implemented";
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Reset Camera"))
                {
                    viewport.SetCameraTarget(0, 0);
                    viewport.SetCameraZoom(1.0f);
                    status = "Camera reset";
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Asset Browser", nullptr, bottomPanel.visible))
                {
                    bottomPanel.visible = !bottomPanel.visible;
                    status = bottomPanel.visible ? "Asset browser shown" : "Asset browser hidden";
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Assets"))
            {
                if (ImGui::MenuItem("Load Image..."))
                {
                    status = "Drag & drop images onto the window to load";
                }
                ImGui::Text("Loaded graphs: %d", (int)gGraphLib.graphs.size());
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ── Full-window layout ──
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##leveleditor_root", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        // Toolbar
        toolbar.Render();

        // Create tool info
        if (toolbar.activeTool == le::EditorTool::Create)
        {
            ImGui::SameLine();
            ImGui::Text("Graph: %d — click in viewport to place (select in Assets tab below)", createGraphId);
        }

        ImGui::Separator();

        // ── Calculate layout heights ──
        float bottomH = bottomPanel.visible ? bottomPanel.height + 4.0f : 0.0f;
        float statusH = ImGui::GetFrameHeightWithSpacing();
        float topAreaH = std::max(120.0f, ImGui::GetContentRegionAvail().y - bottomH - statusH);

        // ── Top area: Hierarchy + Viewport + Inspector ──
        ImGui::BeginChild("##top_area", ImVec2(-1, topAreaH), false);

        // Left panel: Hierarchy
        if (hierarchy.Render(doc, activeLayer, selectedUID, status))
        {
            // Selection changed via hierarchy — center camera on the selected object if needed
            le::SceneObject* selObj = selectedUID ? doc.FindObject(selectedUID) : nullptr;
            if (selObj)
            {
                ImVec2 objScreen = viewport.WorldToScreen({ (float)selObj->x, (float)selObj->y });
                ImVec2 vpOrigin  = viewport.GetOrigin();
                ImVec2 vpSize    = viewport.GetSize();
                float margin = 60.0f;
                bool outOfView = objScreen.x < vpOrigin.x + margin ||
                                 objScreen.x > vpOrigin.x + vpSize.x - margin ||
                                 objScreen.y < vpOrigin.y + margin ||
                                 objScreen.y > vpOrigin.y + vpSize.y - margin;
                if (outOfView)
                    viewport.SetCameraTarget((float)selObj->x, (float)selObj->y);
            }
        }
        ImGui::SameLine(0, 0);

        // Center: Viewport
        // Sync gizmo mode from toolbar
        gizmo.mode = toolbar.gizmoMode;
        le::SceneObject* gizmoTarget = selectedUID ? doc.FindObject(selectedUID) : nullptr;

        ImGui::BeginChild("##viewport_area", ImVec2(-(inspector.width + 4), -1), false);
        viewport.Render(doc, activeLayer, selectedUID, DrawGraphCallback, &gizmo, gizmoTarget);
        ImGui::EndChild();

        // ── Gizmo input (before tool clicks so gizmo takes priority) ──
        bool gizmoConsumed = false;
        if (viewport.IsHovered() && gizmoTarget &&
            (toolbar.activeTool == le::EditorTool::Select || toolbar.activeTool == le::EditorTool::Move))
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            Vector2 worldMouse = viewport.ScreenToWorld(mousePos);
            bool leftDown     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool leftClicked  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

            gizmoConsumed = gizmo.Update(gizmoTarget, worldMouse, viewport.GetCameraZoom(),
                                         leftDown, leftClicked, leftReleased);
            if (gizmoConsumed && leftDown)
                doc.dirty = true;
        }

        // Handle viewport clicks based on active tool
        if (viewport.IsHovered() && !gizmoConsumed)
        {
            ImVec2 mousePos = ImGui::GetMousePos();

            // Select / Move tool: click to select
            if ((toolbar.activeTool == le::EditorTool::Select || toolbar.activeTool == le::EditorTool::Move) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl)
            {
                le::HitResult hit = viewport.HitTest(doc, mousePos, activeLayer);
                if (hit.hit)
                {
                    selectedUID = hit.uid;
                    activeLayer = hit.layer;
                    status = "Selected: " + std::to_string(hit.uid);

                    // Auto-center camera if the object is far from the visible area
                    le::SceneObject* selObj = doc.FindObject(selectedUID);
                    if (selObj)
                    {
                        ImVec2 objScreen = viewport.WorldToScreen({ (float)selObj->x, (float)selObj->y });
                        ImVec2 vpOrigin  = viewport.GetOrigin();
                        ImVec2 vpSize    = viewport.GetSize();
                        float margin = 60.0f; // pixels of padding from edge
                        bool outOfView = objScreen.x < vpOrigin.x + margin ||
                                         objScreen.x > vpOrigin.x + vpSize.x - margin ||
                                         objScreen.y < vpOrigin.y + margin ||
                                         objScreen.y > vpOrigin.y + vpSize.y - margin;
                        if (outOfView)
                        {
                            viewport.SetCameraTarget((float)selObj->x, (float)selObj->y);
                        }
                    }
                }
                else
                {
                    selectedUID = 0;
                    status = "Deselected";
                }
            }

            // Move tool: drag selected object
            if (toolbar.activeTool == le::EditorTool::Move && selectedUID != 0 &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                le::SceneObject* obj = doc.FindObject(selectedUID);
                if (obj)
                {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    float zoom = viewport.GetCameraZoom();
                    obj->x += delta.x / zoom;
                    obj->y += delta.y / zoom;
                    doc.dirty = true;
                }
            }

            // Create tool: click to place new object
            if (toolbar.activeTool == le::EditorTool::Create &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                Vector2 world = viewport.ScreenToWorld(mousePos);
                le::SceneLayer* layer = doc.FindLayerByIndex(activeLayer);
                if (layer && !layer->locked)
                {
                    le::SceneObject obj;
                    obj.uid   = doc.AllocUID();
                    obj.name  = "object_" + std::to_string(obj.uid);
                    obj.graph = createGraphId;
                    obj.x     = world.x;
                    obj.y     = world.y;
                    layer->objects.push_back(obj);
                    selectedUID = obj.uid;
                    doc.dirty   = true;
                    status = "Created object " + std::to_string(obj.uid);
                }
            }

            // Delete tool: click to delete
            if (toolbar.activeTool == le::EditorTool::Delete &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                le::HitResult hit = viewport.HitTest(doc, mousePos, activeLayer);
                if (hit.hit)
                {
                    for (auto& layer : doc.layers)
                    {
                        auto it = std::find_if(layer.objects.begin(), layer.objects.end(),
                            [&](const le::SceneObject& o) { return o.uid == hit.uid; });
                        if (it != layer.objects.end())
                        {
                            layer.objects.erase(it);
                            if (selectedUID == hit.uid) selectedUID = 0;
                            doc.dirty = true;
                            status = "Deleted object";
                            break;
                        }
                    }
                }
            }

            // Right-click: open context menu
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                Vector2 wp = viewport.ScreenToWorld(mousePos);
                contextMenuWorldPos = { wp.x, wp.y };
                le::HitResult hit = viewport.HitTest(doc, mousePos, activeLayer);
                contextMenuTargetUID = hit.hit ? hit.uid : 0;
                // If we right-clicked on an object, select it
                if (hit.hit)
                {
                    selectedUID = hit.uid;
                    activeLayer = hit.layer;
                }
                ImGui::OpenPopup("##viewport_context");
            }
        }

        // ── Viewport context menu ──────────────────────────────────────
        if (ImGui::BeginPopup("##viewport_context"))
        {
            le::SceneObject* targetObj = contextMenuTargetUID ? doc.FindObject(contextMenuTargetUID) : nullptr;

            if (targetObj)
            {
                // Header: show object name
                ImGui::TextDisabled("%s (ID: %u)", targetObj->name.c_str(), targetObj->uid);
                ImGui::Separator();

                // ── Object actions ──
                if (ImGui::MenuItem("Duplicate"))
                {
                    // Find the layer this object is on
                    for (auto& layer : doc.layers)
                    {
                        auto it = std::find_if(layer.objects.begin(), layer.objects.end(),
                            [&](const le::SceneObject& o) { return o.uid == contextMenuTargetUID; });
                        if (it != layer.objects.end())
                        {
                            le::SceneObject dup = *it;
                            dup.uid  = doc.AllocUID();
                            dup.name = it->name + "_copy";
                            dup.x   += 20;
                            dup.y   += 20;
                            layer.objects.push_back(dup);
                            selectedUID = dup.uid;
                            doc.dirty = true;
                            status = "Duplicated → " + dup.name;
                            break;
                        }
                    }
                }
                if (ImGui::MenuItem("Delete"))
                {
                    for (auto& layer : doc.layers)
                    {
                        auto it = std::find_if(layer.objects.begin(), layer.objects.end(),
                            [&](const le::SceneObject& o) { return o.uid == contextMenuTargetUID; });
                        if (it != layer.objects.end())
                        {
                            layer.objects.erase(it);
                            if (selectedUID == contextMenuTargetUID) selectedUID = 0;
                            doc.dirty = true;
                            status = "Deleted object";
                            break;
                        }
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Bring to Front"))
                {
                    targetObj->z += 10;
                    doc.dirty = true;
                    status = "z = " + std::to_string(targetObj->z);
                }
                if (ImGui::MenuItem("Send to Back"))
                {
                    targetObj->z -= 10;
                    doc.dirty = true;
                    status = "z = " + std::to_string(targetObj->z);
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Flip Horizontal"))
                {
                    targetObj->flip_x = !targetObj->flip_x;
                    doc.dirty = true;
                }
                if (ImGui::MenuItem("Flip Vertical"))
                {
                    targetObj->flip_y = !targetObj->flip_y;
                    doc.dirty = true;
                }
                if (ImGui::MenuItem("Reset Transform"))
                {
                    targetObj->angle  = 0;
                    targetObj->size_x = 100;
                    targetObj->size_y = 100;
                    targetObj->flip_x = false;
                    targetObj->flip_y = false;
                    doc.dirty = true;
                    status = "Transform reset";
                }
            }
            else
            {
                // ── Empty space actions ──
                ImGui::TextDisabled("(%.0f, %.0f)", contextMenuWorldPos.x, contextMenuWorldPos.y);
                ImGui::Separator();

                if (ImGui::MenuItem("Create Object Here"))
                {
                    le::SceneLayer* layer = doc.FindLayerByIndex(activeLayer);
                    if (layer && !layer->locked)
                    {
                        le::SceneObject obj;
                        obj.uid   = doc.AllocUID();
                        obj.name  = "object_" + std::to_string(obj.uid);
                        obj.graph = createGraphId;
                        obj.x     = contextMenuWorldPos.x;
                        obj.y     = contextMenuWorldPos.y;
                        layer->objects.push_back(obj);
                        selectedUID = obj.uid;
                        doc.dirty   = true;
                        status = "Created object " + std::to_string(obj.uid);
                    }
                    else
                    {
                        status = "Layer locked or invalid";
                    }
                }
                if (ImGui::BeginMenu("Create with Graph"))
                {
                    if (gGraphLib.graphs.empty())
                    {
                        ImGui::TextDisabled("No graphs loaded");
                    }
                    else
                    {
                        for (int gi = 0; gi < (int)gGraphLib.graphs.size(); gi++)
                        {
                            const Graph& g = gGraphLib.graphs[gi];
                            char label[64];
                            std::snprintf(label, sizeof(label), "%d: %s", gi, g.name);
                            if (ImGui::MenuItem(label))
                            {
                                le::SceneLayer* layer = doc.FindLayerByIndex(activeLayer);
                                if (layer && !layer->locked)
                                {
                                    le::SceneObject obj;
                                    obj.uid   = doc.AllocUID();
                                    obj.name  = std::string(g.name[0] ? g.name : "object") + "_" + std::to_string(obj.uid);
                                    obj.graph = gi;
                                    obj.x     = contextMenuWorldPos.x;
                                    obj.y     = contextMenuWorldPos.y;
                                    layer->objects.push_back(obj);
                                    selectedUID = obj.uid;
                                    doc.dirty   = true;
                                    status = "Created " + obj.name;
                                }
                            }
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Reset Camera"))
                {
                    viewport.SetCameraTarget(0, 0);
                    viewport.SetCameraZoom(1.0f);
                    status = "Camera reset";
                }
                if (ImGui::MenuItem("Center Camera Here"))
                {
                    viewport.SetCameraTarget(contextMenuWorldPos.x, contextMenuWorldPos.y);
                    status = "Camera centered";
                }
            }

            // ── Common actions ──
            ImGui::Separator();
            if (ImGui::BeginMenu("Switch Tool"))
            {
                if (ImGui::MenuItem("Select (S)", nullptr, toolbar.activeTool == le::EditorTool::Select))
                    toolbar.activeTool = le::EditorTool::Select;
                if (ImGui::MenuItem("Move (G)", nullptr, toolbar.activeTool == le::EditorTool::Move))
                    toolbar.activeTool = le::EditorTool::Move;
                if (ImGui::MenuItem("Create (A)", nullptr, toolbar.activeTool == le::EditorTool::Create))
                    toolbar.activeTool = le::EditorTool::Create;
                if (ImGui::MenuItem("Delete", nullptr, toolbar.activeTool == le::EditorTool::Delete))
                    toolbar.activeTool = le::EditorTool::Delete;
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }

        ImGui::SameLine(0, 0);

        // Right panel: Inspector
        inspector.Render(doc, selectedUID);

        ImGui::EndChild(); // end ##top_area

        // ── Bottom panel: Assets / Tiles / Particles ──
        bottomPanel.Render(gGraphLib, createGraphId, createGraphId, toolbar.activeTool, status);

        // ── Scene settings popup ──
        sceneSettings.Render(doc);

        // ── Status bar ──
        ImGui::Separator();
        const char* toolName =
            toolbar.activeTool == le::EditorTool::Select ? "Select" :
            toolbar.activeTool == le::EditorTool::Move   ? "Move" :
            toolbar.activeTool == le::EditorTool::Create ? "Create" :
            "Delete";
        const char* gizmoName =
            toolbar.gizmoMode == le::GizmoMode::Translate ? "Translate(W)" :
            toolbar.gizmoMode == le::GizmoMode::Rotate    ? "Rotate(E)" :
            "Scale(R)";
        ImGui::Text("Scene: %s | Layer: %d | Objects: %d | Tool: %s | Gizmo: %s | Zoom: %.0f%% | %s%s",
                    doc.name.c_str(),
                    activeLayer,
                    [&]() { int n = 0; for (auto& l : doc.layers) n += (int)l.objects.size(); return n; }(),
                    toolName,
                    gizmoName,
                    viewport.GetCameraZoom() * 100.0f,
                    doc.dirty ? "[Modified] " : "",
                    status.c_str());

        ImGui::End();

        // ── Drag & drop images ──
        if (IsFileDropped())
        {
            FilePathList dropped = LoadDroppedFiles();
            for (unsigned int i = 0; i < dropped.count; i++)
            {
                std::string path = dropped.paths[i];
                std::string ext = std::filesystem::path(path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png" || ext == ".jpg" || ext == ".bmp" || ext == ".tga")
                {
                    std::string name = std::filesystem::path(path).stem().string();
                    int id = gGraphLib.load(name.c_str(), path.c_str());
                    if (id >= 0)
                        status = "Loaded graph " + name + " (ID: " + std::to_string(id) + ")";
                    else
                        status = "Failed to load: " + path;
                }
                else if (ext == ".buscene")
                {
                    le::SceneDocument loaded;
                    if (le::SceneDocument::LoadFromFile(path, loaded))
                    {
                        doc = loaded;
                        scenePath = path;
                        selectedUID = 0;
                        status = "Loaded: " + path;
                    }
                    else
                    {
                        status = "Failed to load scene: " + path;
                    }
                }
            }
            UnloadDroppedFiles(dropped);
        }

        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    ImGui::DestroyContext();
    viewport.Shutdown();
    CloseWindow();
    return 0;
}
