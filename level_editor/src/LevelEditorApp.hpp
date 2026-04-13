#pragma once

#include "Camera.hpp"
#include "LevelEditorScene.hpp"
#include "LevelEditorTheme.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ImGuiFileDialog.h"
#include "imgui.h"
#include "Mesh.hpp"

class RenderBatch;
class RenderTarget;
class Shader;

class LevelEditorApp
{
public:
    enum class Tool
    {
        Select,
        Move,
        Scale,
        Rotate
    };

    enum class SelectionMode
    {
        Object,
        Face,
        Edge,
        Vertex
    };

    enum class ViewType
    {
        Top,
        Bottom,
        Front,
        Back,
        Left,
        Right,
        Perspective
    };

    enum class ViewLayout
    {
        Two = 2,
        Three = 3,
        Four = 4
    };

    enum class AssetViewMode
    {
        List,
        Details,
        Grid
    };

    enum class RenderMode
    {
        Solid,
        Wireframe,
        Textured
    };

    enum class DragTool
    {
        None,
        Move,
        Scale,
        Rotate
    };

    LevelEditorApp();
    ~LevelEditorApp();

    void RenderFrame(float deltaTime);

private:
    struct RectI
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        bool contains(const glm::vec2& p) const
        {
            return p.x >= static_cast<float>(x) &&
                   p.y >= static_cast<float>(y) &&
                   p.x < static_cast<float>(x + w) &&
                   p.y < static_cast<float>(y + h);
        }
    };

    struct LevelEditorView
    {
        ViewType type = ViewType::Top;
        const char* label = "";
        RectI rect = {};
        Camera camera;
        glm::vec3 focus = glm::vec3(0.0f);
        glm::vec4 clearColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
        float orthoSize = 256.0f;
        float perspectiveDistance = 720.0f;
        float perspectiveYaw = 45.0f;
        float perspectivePitch = 28.0f;
        RenderMode renderMode = RenderMode::Solid;
    };

    struct AssetEntry
    {
        std::string name;
        std::string path;
    };

    void InitializeViews();
    void LayoutViews(const ImVec2& canvasPos, const ImVec2& canvasSize);
    void UpdateViewCameras();
    void HandleViewportInput(bool viewportHovered);
    void DrawViewTile(const LevelEditorView& view, ImDrawList* drawList);
    void Render3DView(const LevelEditorView& view, ImDrawList* drawList);
    void DrawViewportToolbar();
    void DrawViewportContextMenu();
    void DrawTransformGizmo();
    int PickMeshInPerspectiveView(const LevelEditorView& view, const glm::vec2& mouseScreen) const;
    std::vector<int> PickMeshesInOrthoRect(const LevelEditorView& view, const glm::vec2& startScreen, const glm::vec2& endScreen) const;
    std::vector<int> PickVerticesInOrthoRect(const LevelEditorView& view, const glm::vec2& startScreen, const glm::vec2& endScreen) const;
    std::vector<int> PickFacesInOrthoRect(const LevelEditorView& view, const glm::vec2& startScreen, const glm::vec2& endScreen) const;
    bool ProjectWorldToView(const LevelEditorView& view, const glm::vec3& world, ImVec2& outPoint, float& outDepth) const;
    glm::vec3 OrthoPointFromScreen(const LevelEditorView& view, const glm::vec3& focus, const glm::vec2& mousePos) const;
    glm::vec3 ApplyViewDelta(const glm::vec3& delta, ViewType viewType) const;
    const LevelEditorView* HoveredView() const;
    LevelEditorView* HoveredView();
    bool IsMeshSelected(int index) const;
    bool IsVertexSelected(int index) const;
    void SetSingleSelectedMesh(int index);
    void SyncSelectedMeshes();

    void ShowMenuBar();
    void ShowLeftPanel();
    void ShowCenterPanel();
    void ShowRightPanel();
    void ShowAssetsPanel();
    void ShowStatusBar(float deltaTime);
    void UpdatePanelLayout();
    void PushUndoState();
    bool PerformUndo();
    bool PerformRedo();
    void HandleUndoRedoShortcuts();
    void HandleToolShortcuts();
    void RescanAssets();
    bool SaveSceneToPath(const std::string& path, bool setAsCurrentPath);
    bool LoadSceneFromPath(const std::string& path);
    void HandleFileDialogs();

    LevelEditorScene scene_;
    LevelEditorTheme theme_ = LevelEditorTheme::Studio;
    Tool currentTool_ = Tool::Select;
    SelectionMode selectionMode_ = SelectionMode::Object;
    ViewLayout viewLayout_ = ViewLayout::Four;
    int selectedMeshIndex_ = 0;
    std::vector<int> selectedMeshIndices_;
    std::vector<int> selectedVertexIndices_;
    int selectedFaceIndex_ = -1;
    int selectedEntityIndex_ = 0;
    bool showGrid_ = true;
    bool snapEnabled_ = true;
    bool useTransparency_ = true;
    bool vertexFrontOnly_ = true;
    float transparency_ = 0.45f;
    float gridSize_ = 16.0f;
    std::array<LevelEditorView, 4> views_ = {};
    int activeViewIndex_ = 3;
    int activeViewCount_ = 4;
    ImVec2 viewportCanvasPos_ = ImVec2(0.0f, 0.0f);
    ImVec2 viewportCanvasSize_ = ImVec2(0.0f, 0.0f);
    ImVec2 leftPanelPos_ = ImVec2(0.0f, 0.0f);
    ImVec2 leftPanelSize_ = ImVec2(320.0f, 720.0f);
    ImVec2 centerPanelPos_ = ImVec2(0.0f, 0.0f);
    ImVec2 centerPanelSize_ = ImVec2(860.0f, 720.0f);
    ImVec2 assetPanelPos_ = ImVec2(0.0f, 0.0f);
    ImVec2 assetPanelSize_ = ImVec2(860.0f, 200.0f);
    ImVec2 rightPanelPos_ = ImVec2(0.0f, 0.0f);
    ImVec2 rightPanelSize_ = ImVec2(360.0f, 720.0f);
    std::vector<LevelEditorScene> undoStack_;
    std::vector<LevelEditorScene> redoStack_;
    int maxUndoStates_ = 64;
    std::string assetRoot_ = "assets";
    std::string assetFilter_;
    std::string selectedAssetPath_;
    std::vector<AssetEntry> assets_;
    AssetViewMode assetViewMode_ = AssetViewMode::Details;
    std::string currentTexturePath_;
    float faceExtrudeDistance_ = 16.0f;
    float hollowWallThickness_ = 16.0f;
    int csgClipAxis_ = 0;
    float csgClipOffset_ = 0.0f;
    bool csgClipKeepFront_ = true;
    glm::vec3 vertexBakeTranslate_ = glm::vec3(0.0f);
    glm::vec3 vertexBakeRotate_ = glm::vec3(0.0f);
    glm::vec3 vertexBakeScale_ = glm::vec3(1.0f);
    std::string scenePath_;
    std::string sceneStatusMessage_;
    bool sceneDirty_ = false;
    ImGuiFileDialog assetFolderDialog_;
    ImGuiFileDialog sceneDialog_;
    ImGuiFileDialog importMeshDialog_;
    int contextViewIndex_ = -1;
    bool gizmoWasUsing_ = false;
    bool draggingObjectInView_ = false;
    DragTool dragTool_ = DragTool::None;
    ViewType dragViewType_ = ViewType::Top;
    bool boxSelecting_ = false;
    int boxSelectViewIndex_ = -1;
    bool boxSelectFaces_ = false;
    bool boxSelectVertices_ = false;
    bool boxSelectAdditive_ = false;
    bool boxSelectToggle_ = false;
    glm::vec2 boxSelectStart_ = glm::vec2(0.0f);
    glm::vec2 boxSelectCurrent_ = glm::vec2(0.0f);
    bool draggingVerticesInView_ = false;
    bool draggingFaceInView_ = false;
    bool draggingFaceExtrudeInView_ = false;
    glm::vec2 dragStartMouse_ = glm::vec2(0.0f);
    glm::vec3 dragStartWorld_ = glm::vec3(0.0f);
    glm::vec3 dragStartObjectPosition_ = glm::vec3(0.0f);
    glm::vec3 dragStartObjectRotation_ = glm::vec3(0.0f);
    glm::vec3 dragStartObjectScale_ = glm::vec3(1.0f);
    glm::vec3 dragFaceNormalLocal_ = glm::vec3(0.0f, 1.0f, 0.0f);
    std::vector<glm::vec3> dragStartVertexPositions_;
    std::vector<int> dragFaceVertexIndices_;
    bool dragUndoPushed_ = false;

    std::unique_ptr<RenderBatch> viewBatch_;
    std::unique_ptr<RenderTarget> viewRT_;
    int viewRTWidth_ = 0;
    int viewRTHeight_ = 0;

    Shader* solidShader_ = nullptr;

    // Cached GPU mesh buffers — invalidated when sceneDirty_
    struct CachedMeshGPU
    {
        MeshBuffer buffer;
        std::vector<std::pair<uint32_t, uint32_t>> faceRanges; // (indexOffset, indexCount) per face
    };
    CachedMeshGPU* meshGPUCache_ = nullptr;
    std::size_t    meshGPUCacheCount_ = 0;
    bool meshCacheValid_ = false;
    void InvalidateMeshCache();
    void RebuildMeshCache();
};
