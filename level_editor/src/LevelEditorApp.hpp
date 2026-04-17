#pragma once

#include "Camera.hpp"
#include "LevelEditorScene.hpp"
#include "LevelEditorTheme.hpp"

#include <array>
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ImGuiFileDialog.h"
#include "imgui.h"
#include "Mesh.hpp"
#include "LightmapBaker.hpp"

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
        One = 1,
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
        RenderTarget* rt = nullptr;
        int rtWidth = 0;
        int rtHeight = 0;
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
    bool Section(const char* label, bool defaultOpen = true);
    void PushUndoState();
    bool PerformUndo();
    bool PerformRedo();
    void HandleUndoRedoShortcuts();
    void HandleToolShortcuts();
    void RescanAssets();
    bool SaveSceneToPath(const std::string& path, bool setAsCurrentPath);
    bool LoadSceneFromPath(const std::string& path);
    void HandleFileDialogs();
    void SaveEditorSettings();
    void LoadEditorSettings();
    bool ExportSceneOBJ(const std::string& path);
    bool ExportSceneH3D(const std::string& path);

    enum class RefPlaneAxis { Front, Back, Left, Right, Top, Bottom };
    struct ReferencePlane
    {
        RefPlaneAxis axis = RefPlaneAxis::Front;
        std::string imagePath;
        float offset = 0.0f;
        float scale = 256.0f;
        float opacity = 0.5f;
        bool visible = true;
        std::string textureName; // key in TextureManager
    };

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
    bool snapToGeometry_ = false;
    bool useTransparency_ = true;
    bool vertexFrontOnly_ = true;
    bool placeVertexMode_ = false;
    float transparency_ = 0.45f;
    float gridSize_ = 16.0f;
    float perspGridSize_ = 16.0f;
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
    // Collapsing header open/closed states (persisted in settings)
    std::unordered_map<std::string, bool> sectionOpen_;
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
    float faceInsetAmount_ = 8.0f;
    float hollowWallThickness_ = 16.0f;
    int csgClipAxis_ = 0;
    float csgClipOffset_ = 0.0f;
    bool csgClipKeepFront_ = true;

    // Primitive creation
    enum class PrimitiveType { Box, Cylinder, Sphere, Plane, Wedge, Stairs, SpiralStairs, Text };
    PrimitiveType primitiveType_ = PrimitiveType::Box;
    glm::vec3 primSize_ = glm::vec3(128.0f, 128.0f, 128.0f);
    float primRadius_ = 64.0f;
    float primHeight_ = 128.0f;
    int primSegments_ = 16;
    int primRings_ = 8;
    float primPlaneW_ = 256.0f;
    float primPlaneD_ = 256.0f;
    int primSubdivX_ = 1;
    int primSubdivZ_ = 1;
    int primPlaneOrient_ = 0; // 0=Top 1=Bottom 2=Front 3=Back 4=Left 5=Right
    int primStairSteps_ = 8;
    float primInnerRadius_ = 32.0f;
    float primOuterRadius_ = 96.0f;
    float primSpiralAngle_ = 360.0f;
    // Text primitive
    std::string primText_ = "Hello";
    std::string primFontPath_ = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    float primTextSize_ = 64.0f;
    float primTextExtrude_ = 8.0f;
    int primTextCurveQuality_ = 4;
    glm::vec3 vertexBakeTranslate_ = glm::vec3(0.0f);
    glm::vec3 vertexBakeRotate_ = glm::vec3(0.0f);
    glm::vec3 vertexBakeScale_ = glm::vec3(1.0f);
    std::string scenePath_;
    std::string sceneStatusMessage_;
    std::string lastImportDir_;
    bool sceneDirty_ = false;
    ImGuiFileDialog assetFolderDialog_;
    ImGuiFileDialog sceneDialog_;
    ImGuiFileDialog importMeshDialog_;
    ImGuiFileDialog refPlaneImageDialog_;
    ImGuiFileDialog exportDialog_;
    int refPlaneDialogTarget_ = -1;
    std::vector<ReferencePlane> referencePlanes_;
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

    Shader* solidShader_ = nullptr;

    // Cached GPU mesh buffers — invalidated when sceneDirty_
    struct MaterialRange
    {
        std::string materialName;
        uint32_t indexStart;
        uint32_t indexCount;
    };
    struct CachedMeshGPU
    {
        MeshBuffer buffer;
        std::vector<MaterialRange> materialRanges;  // grouped by material for batched draw
        std::vector<std::pair<uint32_t, uint32_t>> faceRanges; // per face (for highlight)
    };
    CachedMeshGPU* meshGPUCache_ = nullptr;
    std::unordered_set<std::string> failedTextureLoads_;
    std::size_t    meshGPUCacheCount_ = 0;
    bool meshCacheValid_ = false;
    void InvalidateMeshCache();
    void RebuildMeshCache();

    // Debug visualization
    bool debugDrawNormals_ = false;
    bool debugDrawTangents_ = false;
    float debugNormalLength_ = 10.0f;

    // Lightmap
    LightmapResult lightmapResult_;
    LightmapSettings lightmapSettings_;
    GLuint lightmapTexture_ = 0;
    bool useLightmap_ = false;
    void BakeAndUploadLightmap();

    // Async bake
    std::atomic<float> bakeProgress_{0.0f};
    bool bakeRunning_ = false;
    std::unique_ptr<std::thread> bakeThread_;
    LightmapResult bakeResult_;
    LevelEditorScene bakeSceneCopy_;
    void StartBakeAsync();
    void FinishBakeAsync();
};
