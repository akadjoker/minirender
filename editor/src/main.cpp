#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "Batch.hpp"
#include "Camera.hpp"
#include "Device.hpp"
#include "EditorData.hpp"
#include "Input.hpp"
#include "ImGuiConsole.h"
#include "ImGuiFileDialog.h"
#include "ImGuiFontAwesome.h"
#include "ImGuiPropertyGrid.h"
#include "ImGuiSplitter.h"
#include "Manager.hpp"
#include "Material.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <json.hpp>

// Custom serialization for glm types
namespace nlohmann {
    template <>
    struct adl_serializer<glm::vec3> {
        static void to_json(json& j, const glm::vec3& v) {
            j = json{v.x, v.y, v.z};
        }

        static void from_json(const json& j, glm::vec3& v) {
            j.at(0).get_to(v.x);
            j.at(1).get_to(v.y);
            j.at(2).get_to(v.z);
        }
    };

    template <>
    struct adl_serializer<glm::vec2> {
        static void to_json(json& j, const glm::vec2& v) {
            j = json{v.x, v.y};
        }

        static void from_json(const json& j, glm::vec2& v) {
            j.at(0).get_to(v.x);
            j.at(1).get_to(v.y);
        }
    };

    template <>
    struct adl_serializer<BrushVolume::FaceUV> {
        static void to_json(json& j, const BrushVolume::FaceUV& uv) {
            j = json{
                {"offset", uv.offset},
                {"scale", uv.scale},
                {"rotation", uv.rotation}
            };
        }

        static void from_json(const json& j, BrushVolume::FaceUV& uv) {
            uv.offset = j.value("offset", glm::vec2(0.0f));
            uv.scale = j.value("scale", glm::vec2(1.0f));
            uv.rotation = j.value("rotation", 0.0f);
        }
    };

    template <>
    struct adl_serializer<BrushVolume> {
        static void to_json(json& j, const BrushVolume& brush) {
            j = json{
                {"mins", brush.mins},
                {"maxs", brush.maxs},
                {"color", brush.color},
                {"hidden", brush.hidden},
                {"texturePath", brush.texturePath},
                {"faceTextures", brush.faceTextures},
                {"faceUV", brush.faceUV},
                {"uvOffset", brush.uvOffset},
                {"uvScale", brush.uvScale},
                {"uvRotation", brush.uvRotation}
            };
        }

        static void from_json(const json& j, BrushVolume& brush) {
            brush.mins = j.value("mins", glm::vec3(0.0f));
            brush.maxs = j.value("maxs", glm::vec3(0.0f));
            brush.color = j.value("color", glm::vec3(0.47f, 0.82f, 1.0f));
            brush.hidden = j.value("hidden", false);
            brush.texturePath = j.value("texturePath", std::string());
            brush.faceTextures = j.value("faceTextures", std::array<std::string, 6>{});
            brush.faceUV = j.value("faceUV", std::array<BrushVolume::FaceUV, 6>{});
            brush.uvOffset = j.value("uvOffset", glm::vec2(0.0f));
            brush.uvScale = j.value("uvScale", glm::vec2(1.0f));
            brush.uvRotation = j.value("uvRotation", 0.0f);
        }
    };

    void to_json(json& j, const EditorLayoutMode& m) {
        j = static_cast<int>(m);
    }

    void from_json(const json& j, EditorLayoutMode& m) {
        m = static_cast<EditorLayoutMode>(j.get<int>());
    }

    void to_json(json& j, const EditorRenderingMode& m) {
        j = static_cast<int>(m);
    }

    void from_json(const json& j, EditorRenderingMode& m) {
        m = static_cast<EditorRenderingMode>(j.get<int>());
    }
} // nlohmann

struct EditorSceneData
{
    int version = 1;
    glm::vec3 focus = glm::vec3(0.0f);
    std::string currentTexturePath;
    std::vector<BrushVolume> brushes;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EditorSceneData, version, focus, currentTexturePath, brushes)

// Editor settings structure
struct EditorSettings
{
    std::string assetRoot = "assets";
    std::string currentTexturePath = "assets/wall.jpg";
    glm::vec3 focus = glm::vec3(0.0f);
    float gridStep = 32.0f;
    float snapSize = 16.0f;
    EditorLayoutMode layoutMode = EditorLayoutMode::FourViews;
    float defaultBrushThickness = 128.0f;
    float defaultBrushHeight = 128.0f;
    bool showGrid = true;
    bool showAxes = true;
    bool snapEnabled = true;
    float sidebarTopHeight = 520.0f;
    int assetPanelHeight = 200;
    int sidebarWidth = 340;
    bool assetViewAsGrid = false;
    EditorRenderingMode renderingMode = EditorRenderingMode::Solid;

    // View settings
    struct ViewSettings
    {
        float orthoSize = 256.0f;
        float perspectiveDistance = 720.0f;
        float perspectiveYaw = 45.0f;
        float perspectivePitch = 28.0f;
    };
    std::array<ViewSettings, 4> views;
};

// Serialization for ViewSettings
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EditorSettings::ViewSettings, orthoSize, perspectiveDistance, perspectiveYaw, perspectivePitch)

// Serialization for EditorSettings
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EditorSettings, assetRoot, currentTexturePath, focus, gridStep, snapSize,
                                   layoutMode, defaultBrushThickness, defaultBrushHeight, showGrid, showAxes,
                                   snapEnabled, sidebarTopHeight, assetPanelHeight, sidebarWidth, assetViewAsGrid, 
                                   renderingMode, views)

// Save/Load functions
void saveEditorSettings(const EditorSettings& settings, const std::string& filename = "editor_settings.json")
{
    nlohmann::json j = settings;
    std::ofstream file(filename);
    if (file.is_open())
    {
        file << j.dump(4);
        file.close();
    }
}

bool loadEditorSettings(EditorSettings& settings, const std::string& filename = "editor_settings.json")
{
    std::ifstream file(filename);
    if (!file.is_open())
        return false;
    
    try
    {
        nlohmann::json j;
        file >> j;
        settings = j.get<EditorSettings>();
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

static std::filesystem::path ensureSceneExtension(const std::filesystem::path &path)
{
    if (path.extension() == ".mred")
        return path;
    std::filesystem::path out = path;
    out.replace_extension(".mred");
    return out;
}

static std::string normalizePathSlashes(std::string path)
{
    for (char &c : path)
    {
        if (c == '\\')
            c = '/';
    }
    return path;
}

static bool pathExistsFile(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

static std::string makePortablePathForSave(const std::string &rawPath)
{
    if (rawPath.empty())
        return rawPath;

    const std::filesystem::path path = std::filesystem::path(rawPath).lexically_normal();
    if (!path.is_absolute())
        return normalizePathSlashes(path.generic_string());

    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec).lexically_normal();
    if (!ec)
    {
        std::error_code relEc;
        const std::filesystem::path rel = std::filesystem::relative(path, cwd, relEc);
        if (!relEc && !rel.empty())
        {
            const std::string relStr = rel.generic_string();
            if (!(relStr.rfind("..", 0) == 0))
                return normalizePathSlashes(relStr);
        }
    }

    return normalizePathSlashes(path.generic_string());
}

static std::string resolveTexturePathForLoad(const std::string &rawPath)
{
    if (rawPath.empty())
        return rawPath;

    const std::string normalized = normalizePathSlashes(rawPath);
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec).lexically_normal();
    const std::filesystem::path cwdParent = cwd.parent_path();

    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(std::filesystem::path(normalized));
    if (!ec)
    {
        candidates.emplace_back((cwd / normalized).lexically_normal());
        if (!cwdParent.empty())
            candidates.emplace_back((cwdParent / normalized).lexically_normal());
    }

    const std::string marker = "assets/";
    const size_t assetsPos = normalized.find(marker);
    if (assetsPos != std::string::npos && !ec)
    {
        const std::string assetsTail = normalized.substr(assetsPos);
        candidates.emplace_back((cwd / assetsTail).lexically_normal());
        if (!cwdParent.empty())
            candidates.emplace_back((cwdParent / assetsTail).lexically_normal());
    }

    for (const auto &candidate : candidates)
    {
        if (pathExistsFile(candidate))
            return normalizePathSlashes(candidate.generic_string());
    }

    return normalized;
}

static bool saveEditorScene(const std::filesystem::path &path,
                            const std::vector<BrushVolume> &brushes,
                            const glm::vec3 &focus,
                            const std::string &currentTexturePath,
                            std::string &error)
{
    error.clear();
    std::error_code ec;
    const std::filesystem::path finalPath = ensureSceneExtension(path);
    const std::filesystem::path parent = finalPath.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);

    EditorSceneData scene;
    scene.version = 1;
    scene.focus = focus;
    scene.currentTexturePath = makePortablePathForSave(currentTexturePath);
    scene.brushes = brushes;
    for (BrushVolume &brush : scene.brushes)
    {
        brush.texturePath = makePortablePathForSave(brush.texturePath);
        for (std::string &faceTexture : brush.faceTextures)
            faceTexture = makePortablePathForSave(faceTexture);
    }

    try
    {
        nlohmann::json j = scene;
        std::ofstream file(finalPath);
        if (!file.is_open())
        {
            error = "Nao foi possivel abrir ficheiro para escrita: " + finalPath.string();
            return false;
        }
        file << j.dump(2);
        file.close();
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}

static bool loadEditorScene(const std::filesystem::path &path,
                            std::vector<BrushVolume> &brushes,
                            glm::vec3 &focus,
                            std::string &currentTexturePath,
                            std::string &error)
{
    error.clear();
    try
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            error = "Nao foi possivel abrir ficheiro: " + path.string();
            return false;
        }

        nlohmann::json j;
        file >> j;

        EditorSceneData scene = j.get<EditorSceneData>();
        brushes = scene.brushes;
        focus = scene.focus;
        if (!scene.currentTexturePath.empty())
            currentTexturePath = resolveTexturePathForLoad(scene.currentTexturePath);

        int fixedPaths = 0;
        for (BrushVolume &brush : brushes)
        {
            const std::string oldTexturePath = brush.texturePath;
            brush.texturePath = resolveTexturePathForLoad(brush.texturePath);
            if (brush.texturePath != oldTexturePath)
                ++fixedPaths;

            for (std::string &faceTexture : brush.faceTextures)
            {
                const std::string oldFace = faceTexture;
                faceTexture = resolveTexturePathForLoad(faceTexture);
                if (faceTexture != oldFace)
                    ++fixedPaths;
            }
        }

        if (fixedPaths > 0)
        {
            error = "fixed_paths=" + std::to_string(fixedPaths);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}

static const char *viewTypeName(EditorViewType type)
{
    switch (type)
    {
    case EditorViewType::Top: return "Top";
    case EditorViewType::Bottom: return "Bottom";
    case EditorViewType::Front: return "Front";
    case EditorViewType::Back: return "Back";
    case EditorViewType::Left: return "Left";
    case EditorViewType::Right: return "Right";
    case EditorViewType::Perspective: return "3D";
    }
    return "View";
}

static int viewTypeToComboIndex(EditorViewType type)
{
    switch (type)
    {
    case EditorViewType::Top: return 0;
    case EditorViewType::Bottom: return 1;
    case EditorViewType::Front: return 2;
    case EditorViewType::Back: return 3;
    case EditorViewType::Left: return 4;
    case EditorViewType::Right: return 5;
    case EditorViewType::Perspective: return 6;
    }
    return 0;
}

static EditorViewType comboIndexToViewType(int index)
{
    switch (index)
    {
    case 0: return EditorViewType::Top;
    case 1: return EditorViewType::Bottom;
    case 2: return EditorViewType::Front;
    case 3: return EditorViewType::Back;
    case 4: return EditorViewType::Left;
    case 5: return EditorViewType::Right;
    case 6: return EditorViewType::Perspective;
    default: break;
    }
    return EditorViewType::Top;
}

static int layoutViewCount(EditorLayoutMode mode)
{
    return static_cast<int>(mode);
}

static const char *toolName(EditorTool tool)
{
    switch (tool)
    {
    case EditorTool::Select: return "Select";
    case EditorTool::Move: return "Move";
    case EditorTool::Scale: return "Scale";
    case EditorTool::Face: return "Face";
    case EditorTool::Brush: return "Brush";
    }
    return "Tool";
}

static const char *toolShortcut(EditorTool tool)
{
    switch (tool)
    {
    case EditorTool::Select: return "1";
    case EditorTool::Move: return "2";
    case EditorTool::Scale: return "3";
    case EditorTool::Face: return "4";
    case EditorTool::Brush: return "5";
    }
    return "";
}

static const char *toolIcon(EditorTool tool)
{
    switch (tool)
    {
    case EditorTool::Select: return ImGuiFontAwesome::kCode;
    case EditorTool::Move: return ImGuiFontAwesome::kTerminal;
    case EditorTool::Scale: return ImGuiFontAwesome::kChevronUp;
    case EditorTool::Face: return ImGuiFontAwesome::kFileCode;
    case EditorTool::Brush: return ImGuiFontAwesome::kGears;
    }
    return "?";
}

static bool drawToolIconButton(EditorTool tool, EditorTool currentTool)
{
    const bool active = (tool == currentTool);
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.40f, 0.80f, 1.0f));
    }

    const bool clicked = ImGui::SmallButton(toolIcon(tool));

    if (active)
        ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s (%s)", toolName(tool), toolShortcut(tool));

    return clicked;
}

static void drawViewToolPalettes(const std::array<EditorView, 4> &views,
                                 int activeViews,
                                 EditorTool &currentTool)
{
    static const EditorTool kTools[] = {
        EditorTool::Select,
        EditorTool::Move,
        EditorTool::Scale,
        EditorTool::Face,
        EditorTool::Brush,
    };

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoNav;

    for (int i = 0; i < activeViews; ++i)
    {
        const EditorView &view = views[i];
        if (view.rect.w < 160 || view.rect.h < 80)
            continue;

        ImGui::SetNextWindowPos(ImVec2((float)view.rect.x + 6.0f, (float)view.rect.y + 24.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.58f);
        const std::string name = "##view_tools_" + std::to_string(i);
        if (ImGui::Begin(name.c_str(), nullptr, flags))
        {
            for (int t = 0; t < (int)(sizeof(kTools) / sizeof(kTools[0])); ++t)
            {
                if (t > 0)
                    ImGui::SameLine();
                ImGui::PushID(t);
                if (drawToolIconButton(kTools[t], currentTool))
                    currentTool = kTools[t];
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
}

static const char *brushFaceName(int faceIndex)
{
    static const char *kNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    if (faceIndex < 0 || faceIndex >= 6)
        return "+X";
    return kNames[faceIndex];
}

static int faceLeft() { return 1; }   // -X
static int faceRight() { return 0; }  // +X
static int faceTop() { return 2; }    // +Y
static int faceBottom() { return 3; } // -Y
static int faceFront() { return 4; }  // +Z
static int faceBack() { return 5; }   // -Z

static int defaultFaceForView(EditorViewType viewType, int fallback)
{
    switch (viewType)
    {
    case EditorViewType::Top: return faceTop();
    case EditorViewType::Bottom: return faceBottom();
    case EditorViewType::Front: return faceFront();
    case EditorViewType::Back: return faceBack();
    case EditorViewType::Left: return faceLeft();
    case EditorViewType::Right: return faceRight();
    case EditorViewType::Perspective: break;
    }
    return fallback;
}

static void centerFocusForView(glm::vec3 &focus,
                               EditorViewType viewType,
                               const glm::vec3 &target)
{
    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        focus.x = target.x;
        focus.z = target.z;
        break;
    case EditorViewType::Front:
    case EditorViewType::Back:
        focus.x = target.x;
        focus.y = target.y;
        break;
    case EditorViewType::Left:
    case EditorViewType::Right:
        focus.z = target.z;
        focus.y = target.y;
        break;
    case EditorViewType::Perspective:
        focus = target;
        break;
    }
}

static bool selectionContains(const std::vector<int> &selection, int index)
{
    for (int selected : selection)
    {
        if (selected == index)
            return true;
    }
    return false;
}

static void selectionAddUnique(std::vector<int> &selection, int index)
{
    if (!selectionContains(selection, index))
        selection.push_back(index);
}

static void selectionRemove(std::vector<int> &selection, int index)
{
    for (auto it = selection.begin(); it != selection.end(); ++it)
    {
        if (*it == index)
        {
            selection.erase(it);
            return;
        }
    }
}

static std::vector<int> buildSortedUniqueValidSelection(const std::vector<int> &selection, int brushCount)
{
    std::vector<int> result;
    result.reserve(selection.size());
    for (int index : selection)
    {
        if (index >= 0 && index < brushCount)
            result.push_back(index);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

static std::vector<int> cloneSelectedBrushes(std::vector<BrushVolume> &brushes,
                                             const std::vector<int> &selection,
                                             float offset)
{
    const std::vector<int> source = buildSortedUniqueValidSelection(selection, (int)brushes.size());
    const glm::vec3 delta(offset, 0.0f, offset);

    std::vector<int> newSelection;
    newSelection.reserve(source.size());
    for (int index : source)
    {
        BrushVolume copy = brushes[(size_t)index];
        copy.mins += delta;
        copy.maxs += delta;
        brushes.push_back(copy);
        newSelection.push_back((int)brushes.size() - 1);
    }
    return newSelection;
}

static std::vector<BrushVolume> copySelectedBrushes(const std::vector<BrushVolume> &brushes,
                                                    const std::vector<int> &selection)
{
    const std::vector<int> source = buildSortedUniqueValidSelection(selection, (int)brushes.size());
    std::vector<BrushVolume> clipboard;
    clipboard.reserve(source.size());
    for (int index : source)
        clipboard.push_back(brushes[(size_t)index]);
    return clipboard;
}

static std::vector<int> pasteBrushClipboard(std::vector<BrushVolume> &brushes,
                                            const std::vector<BrushVolume> &clipboard,
                                            float offset)
{
    if (clipboard.empty())
        return {};

    const glm::vec3 delta(offset, 0.0f, offset);
    std::vector<int> newSelection;
    newSelection.reserve(clipboard.size());
    for (const BrushVolume &source : clipboard)
    {
        BrushVolume copy = source;
        copy.mins += delta;
        copy.maxs += delta;
        brushes.push_back(copy);
        newSelection.push_back((int)brushes.size() - 1);
    }
    return newSelection;
}

struct BrushSelectionRect
{
    bool active = false;
    bool additive = false;
    int viewIndex = -1;
    EditorViewType viewType = EditorViewType::Top;
    glm::vec2 startMouse = glm::vec2(0.0f);
    glm::vec2 endMouse = glm::vec2(0.0f);
    glm::vec3 startWorld = glm::vec3(0.0f);
    glm::vec3 endWorld = glm::vec3(0.0f);
};

static void swapBrushFaces(BrushVolume &brush, int a, int b)
{
    if (a < 0 || a >= 6 || b < 0 || b >= 6 || a == b)
        return;
    std::swap(brush.faceTextures[(size_t)a], brush.faceTextures[(size_t)b]);
    std::swap(brush.faceUV[(size_t)a], brush.faceUV[(size_t)b]);
}

static void rotateBrushY90(BrushVolume &brush, int turns)
{
    int steps = turns % 4;
    if (steps < 0)
        steps += 4;
    for (int i = 0; i < steps; ++i)
    {
        const glm::vec3 center = (brush.mins + brush.maxs) * 0.5f;
        glm::vec3 half = (brush.maxs - brush.mins) * 0.5f;
        std::swap(half.x, half.z);
        brush.mins = center - half;
        brush.maxs = center + half;

        // +X <- +Z <- -X <- -Z <- +X
        const std::string oldPX = brush.faceTextures[(size_t)faceRight()];
        const std::string oldNX = brush.faceTextures[(size_t)faceLeft()];
        const std::string oldPZ = brush.faceTextures[(size_t)faceFront()];
        const std::string oldNZ = brush.faceTextures[(size_t)faceBack()];
        brush.faceTextures[(size_t)faceRight()] = oldPZ;
        brush.faceTextures[(size_t)faceBack()] = oldPX;
        brush.faceTextures[(size_t)faceLeft()] = oldNZ;
        brush.faceTextures[(size_t)faceFront()] = oldNX;

        const BrushVolume::FaceUV oldUvPX = brush.faceUV[(size_t)faceRight()];
        const BrushVolume::FaceUV oldUvNX = brush.faceUV[(size_t)faceLeft()];
        const BrushVolume::FaceUV oldUvPZ = brush.faceUV[(size_t)faceFront()];
        const BrushVolume::FaceUV oldUvNZ = brush.faceUV[(size_t)faceBack()];
        brush.faceUV[(size_t)faceRight()] = oldUvPZ;
        brush.faceUV[(size_t)faceBack()] = oldUvPX;
        brush.faceUV[(size_t)faceLeft()] = oldUvNZ;
        brush.faceUV[(size_t)faceFront()] = oldUvNX;
    }
}

static glm::vec3 randomBrushColor()
{
    static std::mt19937 rng(1337u);
    static std::uniform_real_distribution<float> hueDist(0.0f, 1.0f);
    const float h = hueDist(rng);
    const float s = 0.45f;
    const float v = 0.95f;

    const float hh = h * 6.0f;
    const int i = (int)std::floor(hh) % 6;
    const float f = hh - std::floor(hh);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);

    switch (i)
    {
    case 0: return glm::vec3(v, t, p);
    case 1: return glm::vec3(q, v, p);
    case 2: return glm::vec3(p, v, t);
    case 3: return glm::vec3(p, q, v);
    case 4: return glm::vec3(t, p, v);
    case 5: return glm::vec3(v, p, q);
    default: break;
    }
    return glm::vec3(0.47f, 0.82f, 1.0f);
}

static void syncViewLabels(std::array<EditorView, 4> &views)
{
    for (EditorView &view : views)
        view.label = viewTypeName(view.type);
}

static bool hasImageExtension(const std::string &path)
{
    const char *extensions[] = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga"
    };

    std::string lower = path;
    for (char &c : lower)
        c = (char)std::tolower((unsigned char)c);

    for (const char *ext : extensions)
    {
        const size_t len = std::strlen(ext);
        if (lower.size() >= len && lower.compare(lower.size() - len, len, ext) == 0)
            return true;
    }
    return false;
}

static bool containsInsensitive(const std::string &text, const std::string &pattern)
{
    if (pattern.empty())
        return true;

    std::string lowerText = text;
    std::string lowerPattern = pattern;
    for (char &c : lowerText)
        c = (char)std::tolower((unsigned char)c);
    for (char &c : lowerPattern)
        c = (char)std::tolower((unsigned char)c);

    return lowerText.find(lowerPattern) != std::string::npos;
}

static std::filesystem::path resolveAssetRootPath(const std::string &assetRoot)
{
    std::filesystem::path path = assetRoot.empty() ? std::filesystem::path(".") : std::filesystem::path(assetRoot);
    if (path.is_relative())
        path = std::filesystem::current_path() / path;
    return path.lexically_normal();
}

static std::string makeAssetRootDisplayPath(const std::filesystem::path &path)
{
    const std::filesystem::path normalized = path.lexically_normal();
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec)
    {
        std::error_code relEc;
        const std::filesystem::path rel = std::filesystem::relative(normalized, cwd, relEc);
        if (!relEc && !rel.empty())
        {
            const std::string relStr = rel.generic_string();
            if (!(relStr.size() >= 2 && relStr[0] == '.' && relStr[1] == '.'))
                return relStr;
        }
    }
    return normalized.generic_string();
}

#if defined(_WIN32)
static void scanAssetDirectoryRecursive(const std::string &root, std::vector<AssetEntry> &outAssets)
{
    std::string pattern = root;
    if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/')
        pattern += "\\*";
    else
        pattern += "*";

    WIN32_FIND_DATAA findData;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &findData);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const char *name = findData.cFileName;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
            continue;

        std::string fullPath = root;
        if (!fullPath.empty() && fullPath.back() != '\\' && fullPath.back() != '/')
            fullPath += "/";
        fullPath += name;

        const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDirectory)
        {
            scanAssetDirectoryRecursive(fullPath, outAssets);
        }
        else if (hasImageExtension(fullPath))
        {
            outAssets.push_back({name, fullPath});
        }
    } while (FindNextFileA(handle, &findData));

    FindClose(handle);
}
#else
static void scanAssetDirectoryRecursive(const std::string &root, std::vector<AssetEntry> &outAssets)
{
    DIR *dir = opendir(root.c_str());
    if (!dir)
        return;

    while (dirent *entry = readdir(dir))
    {
        const char *name = entry->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
            continue;

        std::string fullPath = root;
        if (!fullPath.empty() && fullPath.back() != '/')
            fullPath += "/";
        fullPath += name;

        struct stat info;
        if (stat(fullPath.c_str(), &info) != 0)
            continue;

        if (S_ISDIR(info.st_mode))
        {
            scanAssetDirectoryRecursive(fullPath, outAssets);
        }
        else if (hasImageExtension(fullPath))
        {
            outAssets.push_back({name, fullPath});
        }
    }

    closedir(dir);
}
#endif

static void rescanAssets(const std::string &root, std::vector<AssetEntry> &assets)
{
    assets.clear();
    scanAssetDirectoryRecursive(root, assets);
    std::sort(assets.begin(), assets.end(), [](const AssetEntry &a, const AssetEntry &b)
    {
        return a.path < b.path;
    });
}

static glm::vec3 snapPoint(const glm::vec3 &p, float snapSize, bool enabled)
{
    if (!enabled || snapSize <= 1e-4f)
        return p;

    auto snapValue = [snapSize](float v) -> float
    {
        return std::round(v / snapSize) * snapSize;
    };

    return glm::vec3(snapValue(p.x), snapValue(p.y), snapValue(p.z));
}

static glm::mat4 screenOrthoMatrix(int width, int height)
{
    return glm::ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);
}

static void seedTestBrushes(std::vector<BrushVolume> &brushes)
{
    brushes.clear();

    // Add some test brushes
    BrushVolume brush1;
    brush1.mins = glm::vec3(-50.0f, 0.0f, -50.0f);
    brush1.maxs = glm::vec3(50.0f, 100.0f, 50.0f);
    brush1.color = randomBrushColor();
    brush1.texturePath = "textures/default.png";
    brushes.push_back(brush1);

    BrushVolume brush2;
    brush2.mins = glm::vec3(-20.0f, 100.0f, -20.0f);
    brush2.maxs = glm::vec3(20.0f, 150.0f, 20.0f);
    brush2.color = randomBrushColor();
    brush2.texturePath = "textures/default.png";
    brushes.push_back(brush2);

    BrushVolume brush3;
    brush3.mins = glm::vec3(60.0f, 0.0f, 60.0f);
    brush3.maxs = glm::vec3(120.0f, 80.0f, 120.0f);
    brush3.color = randomBrushColor();
    brush3.texturePath = "textures/default.png";
    brushes.push_back(brush3);
}

static void appendConsoleLine(ImGuiConsole &console, const std::string &line)
{
    std::string text = console.GetText();
    if (!text.empty())
        text += "\n";
    text += line;
    console.SetText(text);
}

static glm::vec2 projectWorldToViewPlane(const glm::vec3 &p, EditorViewType viewType)
{
    switch (viewType)
    {
    case EditorViewType::Top:
        return glm::vec2(p.x, p.z);
    case EditorViewType::Bottom:
        return glm::vec2(p.x, -p.z);
    case EditorViewType::Front:
        return glm::vec2(p.x, p.y);
    case EditorViewType::Back:
        return glm::vec2(-p.x, p.y);
    case EditorViewType::Left:
        return glm::vec2(p.z, p.y);
    case EditorViewType::Right:
        return glm::vec2(-p.z, p.y);
    case EditorViewType::Perspective:
        break;
    }
    return glm::vec2(0.0f);
}

static glm::vec3 applyViewDelta(const glm::vec3 &delta, EditorViewType viewType)
{
    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        return glm::vec3(delta.x, 0.0f, delta.z);
    case EditorViewType::Front:
    case EditorViewType::Back:
        return glm::vec3(delta.x, delta.y, 0.0f);
    case EditorViewType::Left:
    case EditorViewType::Right:
        return glm::vec3(0.0f, delta.y, delta.z);
    case EditorViewType::Perspective:
        break;
    }
    return glm::vec3(0.0f);
}

static BrushVolume makeBrushFromDrag(EditorViewType viewType,
                                     const glm::vec3 &start,
                                     const glm::vec3 &end,
                                     float defaultThickness,
                                     float defaultHeight,
                                     const glm::vec3 &focus,
                                     const std::string &texturePath)
{
    BrushVolume brush;
    brush.color = randomBrushColor();
    brush.texturePath = texturePath;

    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        brush.mins.x = glm::min(start.x, end.x);
        brush.maxs.x = glm::max(start.x, end.x);
        brush.mins.z = glm::min(start.z, end.z);
        brush.maxs.z = glm::max(start.z, end.z);
        brush.mins.y = focus.y;
        brush.maxs.y = focus.y + glm::max(defaultHeight, 1.0f);
        break;
    case EditorViewType::Front:
    case EditorViewType::Back:
        brush.mins.x = glm::min(start.x, end.x);
        brush.maxs.x = glm::max(start.x, end.x);
        brush.mins.y = glm::min(start.y, end.y);
        brush.maxs.y = glm::max(start.y, end.y);
        brush.mins.z = focus.z - defaultThickness * 0.5f;
        brush.maxs.z = focus.z + defaultThickness * 0.5f;
        break;
    case EditorViewType::Left:
    case EditorViewType::Right:
        brush.mins.z = glm::min(start.z, end.z);
        brush.maxs.z = glm::max(start.z, end.z);
        brush.mins.y = glm::min(start.y, end.y);
        brush.maxs.y = glm::max(start.y, end.y);
        brush.mins.x = focus.x - defaultThickness * 0.5f;
        brush.maxs.x = focus.x + defaultThickness * 0.5f;
        break;
    case EditorViewType::Perspective:
        break;
    }

    if (brush.maxs.x - brush.mins.x < 1.0f)
        brush.maxs.x = brush.mins.x + 1.0f;
    if (brush.maxs.y - brush.mins.y < 1.0f)
        brush.maxs.y = brush.mins.y + 1.0f;
    if (brush.maxs.z - brush.mins.z < 1.0f)
        brush.maxs.z = brush.mins.z + 1.0f;

    return brush;
}

static float distancePointToRect2D(const glm::vec2 &p, const glm::vec2 &mins, const glm::vec2 &maxs)
{
    const float dx = glm::max(glm::max(mins.x - p.x, 0.0f), p.x - maxs.x);
    const float dy = glm::max(glm::max(mins.y - p.y, 0.0f), p.y - maxs.y);
    return std::sqrt(dx * dx + dy * dy);
}

static int findBrushAtPoint(const std::vector<BrushVolume> &brushes,
                            EditorViewType viewType,
                            const glm::vec3 &worldPoint,
                            float maxDistance)
{
    int bestIndex = -1;
    float bestDistance = maxDistance;
    const glm::vec2 point2 = projectWorldToViewPlane(worldPoint, viewType);

    for (int i = 0; i < (int)brushes.size(); ++i)
    {
        if (brushes[i].hidden)
            continue;
        const glm::vec2 a = projectWorldToViewPlane(brushes[i].mins, viewType);
        const glm::vec2 b = projectWorldToViewPlane(brushes[i].maxs, viewType);
        const glm::vec2 mins(glm::min(a.x, b.x), glm::min(a.y, b.y));
        const glm::vec2 maxs(glm::max(a.x, b.x), glm::max(a.y, b.y));
        const float distance = distancePointToRect2D(point2, mins, maxs);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    return bestIndex;
}

static bool brushIntersectsSelectionRect(const BrushVolume &brush,
                                         EditorViewType viewType,
                                         const glm::vec3 &rectStartWorld,
                                         const glm::vec3 &rectEndWorld)
{
    const glm::vec2 r0 = projectWorldToViewPlane(rectStartWorld, viewType);
    const glm::vec2 r1 = projectWorldToViewPlane(rectEndWorld, viewType);
    const glm::vec2 rectMins(glm::min(r0.x, r1.x), glm::min(r0.y, r1.y));
    const glm::vec2 rectMaxs(glm::max(r0.x, r1.x), glm::max(r0.y, r1.y));

    const glm::vec2 a = projectWorldToViewPlane(brush.mins, viewType);
    const glm::vec2 b = projectWorldToViewPlane(brush.maxs, viewType);
    const glm::vec2 brushMins(glm::min(a.x, b.x), glm::min(a.y, b.y));
    const glm::vec2 brushMaxs(glm::max(a.x, b.x), glm::max(a.y, b.y));

    const bool overlapX = !(brushMaxs.x < rectMins.x || brushMins.x > rectMaxs.x);
    const bool overlapY = !(brushMaxs.y < rectMins.y || brushMins.y > rectMaxs.y);
    return overlapX && overlapY;
}

static bool raycastBrushFace(const BrushVolume &brush,
                             const Ray &ray,
                             float &outT,
                             int &outFace)
{
    constexpr float eps = 1e-7f;
    float tMin = 0.0f;
    float tMax = 1e30f;
    int enterFace = -1;
    int exitFace = -1;

    const int minFaces[3] = {1, 3, 5}; // -X, -Y, -Z
    const int maxFaces[3] = {0, 2, 4}; // +X, +Y, +Z

    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = ray.origin[axis];
        const float dir = ray.direction[axis];
        const float bMin = brush.mins[axis];
        const float bMax = brush.maxs[axis];

        if (std::fabs(dir) <= eps)
        {
            if (origin < bMin || origin > bMax)
                return false;
            continue;
        }

        float invDir = 1.0f / dir;
        float t1 = (bMin - origin) * invDir;
        float t2 = (bMax - origin) * invDir;
        int face1 = minFaces[axis];
        int face2 = maxFaces[axis];
        if (t1 > t2)
        {
            std::swap(t1, t2);
            std::swap(face1, face2);
        }

        if (t1 > tMin)
        {
            tMin = t1;
            enterFace = face1;
        }
        if (t2 < tMax)
        {
            tMax = t2;
            exitFace = face2;
        }
        if (tMin > tMax)
            return false;
    }

    if (tMax < 0.0f)
        return false;

    if (tMin >= 0.0f)
    {
        outT = tMin;
        outFace = enterFace;
    }
    else
    {
        outT = tMax;
        outFace = exitFace;
    }
    return outFace >= 0;
}

static bool pickBrushWithRay(const std::vector<BrushVolume> &brushes,
                             const Ray &ray,
                             int &outBrush,
                             int &outFace,
                             float maxDistance = 1e30f)
{
    outBrush = -1;
    outFace = -1;
    float bestT = maxDistance;

    for (int i = 0; i < (int)brushes.size(); ++i)
    {
        if (brushes[i].hidden)
            continue;
        float t = 0.0f;
        int face = -1;
        if (!raycastBrushFace(brushes[i], ray, t, face))
            continue;
        if (t < 0.0f || t > bestT)
            continue;
        bestT = t;
        outBrush = i;
        outFace = face;
    }

    return outBrush >= 0;
}

enum class BrushScaleAxis
{
    None,
    X,
    Y,
    Z
};

static BrushScaleAxis chooseBrushScaleAxis(EditorViewType viewType, const glm::vec2 &viewDelta)
{
    const bool useFirst = std::fabs(viewDelta.x) >= std::fabs(viewDelta.y);
    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        return useFirst ? BrushScaleAxis::X : BrushScaleAxis::Z;
    case EditorViewType::Front:
    case EditorViewType::Back:
        return useFirst ? BrushScaleAxis::X : BrushScaleAxis::Y;
    case EditorViewType::Left:
    case EditorViewType::Right:
        return useFirst ? BrushScaleAxis::Z : BrushScaleAxis::Y;
    case EditorViewType::Perspective:
        break;
    }
    return BrushScaleAxis::None;
}

static float brushAxisValue(const glm::vec3 &v, BrushScaleAxis axis)
{
    switch (axis)
    {
    case BrushScaleAxis::X: return v.x;
    case BrushScaleAxis::Y: return v.y;
    case BrushScaleAxis::Z: return v.z;
    case BrushScaleAxis::None: break;
    }
    return 0.0f;
}

static int brushFaceForScaleAxis(BrushScaleAxis axis, bool positiveFace)
{
    switch (axis)
    {
    case BrushScaleAxis::X: return positiveFace ? 0 : 1;
    case BrushScaleAxis::Y: return positiveFace ? 2 : 3;
    case BrushScaleAxis::Z: return positiveFace ? 4 : 5;
    case BrushScaleAxis::None: break;
    }
    return 2;
}

static BrushVolume scaleBrushFaceAlongAxis(const BrushVolume &original,
                                           BrushScaleAxis axis,
                                           bool positiveFace,
                                           float delta)
{
    BrushVolume result = original;
    constexpr float minSize = 1.0f;

    switch (axis)
    {
    case BrushScaleAxis::X:
        if (positiveFace)
            result.maxs.x = glm::max(original.maxs.x + delta, original.mins.x + minSize);
        else
            result.mins.x = glm::min(original.mins.x + delta, original.maxs.x - minSize);
        break;
    case BrushScaleAxis::Y:
        if (positiveFace)
            result.maxs.y = glm::max(original.maxs.y + delta, original.mins.y + minSize);
        else
            result.mins.y = glm::min(original.mins.y + delta, original.maxs.y - minSize);
        break;
    case BrushScaleAxis::Z:
        if (positiveFace)
            result.maxs.z = glm::max(original.maxs.z + delta, original.mins.z + minSize);
        else
            result.mins.z = glm::min(original.mins.z + delta, original.maxs.z - minSize);
        break;
    case BrushScaleAxis::None:
        break;
    }

    return result;
}

static glm::vec3 orthoPointFromScreen(const EditorView &view,
                                      const glm::vec3 &focus,
                                      const glm::vec2 &mousePos)
{
    const float aspect = (view.rect.h > 0) ? ((float)view.rect.w / (float)view.rect.h) : 1.0f;
    const float halfH = view.orthoSize;
    const float halfW = halfH * aspect;

    const float localX = ((mousePos.x - (float)view.rect.x) / (float)view.rect.w) * 2.0f - 1.0f;
    const float localY = 1.0f - ((mousePos.y - (float)view.rect.y) / (float)view.rect.h) * 2.0f;

    switch (view.type)
    {
    case EditorViewType::Top:
        return glm::vec3(focus.x + localX * halfW,
                         focus.y,
                         focus.z - localY * halfH);
    case EditorViewType::Bottom:
        return glm::vec3(focus.x + localX * halfW,
                         focus.y,
                         focus.z + localY * halfH);
    case EditorViewType::Front:
        return glm::vec3(focus.x + localX * halfW,
                         focus.y + localY * halfH,
                         focus.z);
    case EditorViewType::Back:
        return glm::vec3(focus.x - localX * halfW,
                         focus.y + localY * halfH,
                         focus.z);
    case EditorViewType::Left:
        return glm::vec3(focus.x,
                         focus.y + localY * halfH,
                         focus.z + localX * halfW);
    case EditorViewType::Right:
        return glm::vec3(focus.x,
                         focus.y + localY * halfH,
                         focus.z - localX * halfW);
    case EditorViewType::Perspective:
        break;
    }

    return focus;
}

static void panFocusInOrthoView(glm::vec3 &focus,
                                const EditorView &view,
                                const glm::vec2 &mouseDelta)
{
    if (view.rect.w <= 0 || view.rect.h <= 0)
        return;

    const float aspect = (float)view.rect.w / (float)view.rect.h;
    const float halfH = view.orthoSize;
    const float halfW = halfH * aspect;
    const float worldPerPixelX = (2.0f * halfW) / (float)view.rect.w;
    const float worldPerPixelY = (2.0f * halfH) / (float)view.rect.h;

    switch (view.type)
    {
    case EditorViewType::Top:
        focus.x -= mouseDelta.x * worldPerPixelX;
        focus.z += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Bottom:
        focus.x -= mouseDelta.x * worldPerPixelX;
        focus.z -= mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Front:
        focus.x -= mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Back:
        focus.x += mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Left:
        focus.z -= mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Right:
        focus.z += mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Perspective:
        break;
    }
}

static void panFocusInPerspective(glm::vec3 &focus,
                                  const EditorView &view,
                                  const glm::vec2 &mouseDelta)
{
    const float yaw = glm::radians(view.perspectiveYaw);
    const float pitch = glm::radians(view.perspectivePitch);
    const glm::vec3 offset(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(yaw));

    const glm::vec3 forward = glm::normalize(-offset);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    if (glm::length2(right) < 1e-8f)
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    const float panScale = glm::max(view.perspectiveDistance * 0.0015f, 0.01f);
    focus += (-right * mouseDelta.x + up * mouseDelta.y) * panScale;
}

static void setupViewLayout(std::array<EditorView, 4> &views,
                            int screenWidth,
                            int screenHeight,
                            int sidebarWidth,
                            int assetPanelHeight,
                            EditorLayoutMode layoutMode,
                            int topInset,
                            int margin,
                            int gap)
{
    const int layoutX = sidebarWidth + margin;
    const int layoutY = topInset + margin;
    const int layoutW = screenWidth - layoutX - margin;
    const int layoutH = screenHeight - layoutY - margin - assetPanelHeight - gap;
    for (EditorView &view : views)
        view.rect = {};

    if (layoutMode == EditorLayoutMode::TwoViews)
    {
        const int halfW = (layoutW - gap) / 2;
        views[0].rect = {layoutX, layoutY, halfW, layoutH};
        views[1].rect = {layoutX + halfW + gap, layoutY, halfW, layoutH};
        return;
    }

    if (layoutMode == EditorLayoutMode::ThreeViews)
    {
        const int leftW = (layoutW - gap) * 3 / 5;
        const int rightW = layoutW - gap - leftW;
        const int halfH = (layoutH - gap) / 2;

        views[0].rect = {layoutX, layoutY, leftW, layoutH};
        views[1].rect = {layoutX + leftW + gap, layoutY, rightW, halfH};
        views[2].rect = {layoutX + leftW + gap, layoutY + halfH + gap, rightW, halfH};
        return;
    }

    const int halfW = (layoutW - gap) / 2;
    const int halfH = (layoutH - gap) / 2;
    views[0].rect = {layoutX, layoutY, halfW, halfH};
    views[1].rect = {layoutX + halfW + gap, layoutY, halfW, halfH};
    views[2].rect = {layoutX, layoutY + halfH + gap, halfW, halfH};
    views[3].rect = {layoutX + halfW + gap, layoutY + halfH + gap, halfW, halfH};
}

static void updateCameras(std::array<EditorView, 4> &views,
                          const glm::vec3 &focus)
{
    constexpr float orthoDistance = 1024.0f;

    for (EditorView &view : views)
    {
        view.camera.setViewport(0, 0, view.rect.w, view.rect.h);
        view.camera.setViewPlanes(0.1f, 8192.0f);

        if (view.type == EditorViewType::Perspective)
        {
            view.camera.setProjectionType(ProjectionType::Perspective);
            view.camera.setFov(60.0f);

            const float yaw = glm::radians(view.perspectiveYaw);
            const float pitch = glm::radians(view.perspectivePitch);
            const glm::vec3 offset(
                std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                std::cos(pitch) * std::cos(yaw));
            view.camera.setPosition(focus + offset * view.perspectiveDistance);
            view.camera.lookAt(focus, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        else
        {
            view.camera.setProjectionType(ProjectionType::Orthographic);
            view.camera.orthoSize = view.orthoSize;

            switch (view.type)
            {
            case EditorViewType::Top:
                view.camera.setPosition(focus + glm::vec3(0.0f, orthoDistance, 0.0f));
                view.camera.lookAt(focus, glm::vec3(0.0f, 0.0f, -1.0f));
                break;
            case EditorViewType::Bottom:
                view.camera.setPosition(focus + glm::vec3(0.0f, -orthoDistance, 0.0f));
                view.camera.lookAt(focus, glm::vec3(0.0f, 0.0f, 1.0f));
                break;
            case EditorViewType::Front:
                view.camera.setPosition(focus + glm::vec3(0.0f, 0.0f, orthoDistance));
                view.camera.lookAt(focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Back:
                view.camera.setPosition(focus + glm::vec3(0.0f, 0.0f, -orthoDistance));
                view.camera.lookAt(focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Left:
                view.camera.setPosition(focus + glm::vec3(-orthoDistance, 0.0f, 0.0f));
                view.camera.lookAt(focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Right:
                view.camera.setPosition(focus + glm::vec3(orthoDistance, 0.0f, 0.0f));
                view.camera.lookAt(focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Perspective:
                break;
            }
        }

        view.camera.updateMatrices();
    }
}

static void drawGridForView(RenderBatch &batch,
                            EditorViewType type,
                            const glm::vec3 &focus,
                            float extent,
                            float gridStep)
{
    const int lineCount = (int)std::ceil(extent / gridStep);

    batch.SetColor(70, 74, 80, 255);
    for (int i = -lineCount; i <= lineCount; ++i)
    {
        const float offset = (float)i * gridStep;

        switch (type)
        {
        case EditorViewType::Top:
        case EditorViewType::Bottom:
            batch.Line3D(glm::vec3(focus.x + offset, focus.y, focus.z - extent),
                         glm::vec3(focus.x + offset, focus.y, focus.z + extent));
            batch.Line3D(glm::vec3(focus.x - extent, focus.y, focus.z + offset),
                         glm::vec3(focus.x + extent, focus.y, focus.z + offset));
            break;
        case EditorViewType::Front:
        case EditorViewType::Back:
            batch.Line3D(glm::vec3(focus.x + offset, focus.y - extent, focus.z),
                         glm::vec3(focus.x + offset, focus.y + extent, focus.z));
            batch.Line3D(glm::vec3(focus.x - extent, focus.y + offset, focus.z),
                         glm::vec3(focus.x + extent, focus.y + offset, focus.z));
            break;
        case EditorViewType::Left:
        case EditorViewType::Right:
            batch.Line3D(glm::vec3(focus.x, focus.y - extent, focus.z + offset),
                         glm::vec3(focus.x, focus.y + extent, focus.z + offset));
            batch.Line3D(glm::vec3(focus.x, focus.y + offset, focus.z - extent),
                         glm::vec3(focus.x, focus.y + offset, focus.z + extent));
            break;
        case EditorViewType::Perspective:
            batch.Line3D(glm::vec3(offset, 0.0f, -extent),
                         glm::vec3(offset, 0.0f, extent));
            batch.Line3D(glm::vec3(-extent, 0.0f, offset),
                         glm::vec3(extent, 0.0f, offset));
            break;
        }
    }
}

static void drawAxes(RenderBatch &batch, float axisLength)
{
    batch.SetColor(235, 85, 85, 255);
    batch.Line3D(glm::vec3(0.0f), glm::vec3(axisLength, 0.0f, 0.0f));

    batch.SetColor(110, 230, 110, 255);
    batch.Line3D(glm::vec3(0.0f), glm::vec3(0.0f, axisLength, 0.0f));

    batch.SetColor(90, 150, 255, 255);
    batch.Line3D(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, axisLength));
}

enum class BrushFaceAxis
{
    X,
    Y,
    Z
};

struct BrushFaceGeometry
{
    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;
    glm::vec3 p3;
    BrushFaceAxis axis = BrushFaceAxis::X;
    int faceIndex = 0; // +X, -X, +Y, -Y, +Z, -Z
};

static std::array<BrushFaceGeometry, 6> buildBrushFaces(const BrushVolume &brush)
{
    const glm::vec3 &mins = brush.mins;
    const glm::vec3 &maxs = brush.maxs;

    std::array<BrushFaceGeometry, 6> faces = {{
        // +X
        {glm::vec3(maxs.x, mins.y, mins.z),
         glm::vec3(maxs.x, maxs.y, mins.z),
         glm::vec3(maxs.x, maxs.y, maxs.z),
         glm::vec3(maxs.x, mins.y, maxs.z),
         BrushFaceAxis::X, 0},
        // -X
        {glm::vec3(mins.x, mins.y, mins.z),
         glm::vec3(mins.x, mins.y, maxs.z),
         glm::vec3(mins.x, maxs.y, maxs.z),
         glm::vec3(mins.x, maxs.y, mins.z),
         BrushFaceAxis::X, 1},
        // +Y
        {glm::vec3(mins.x, maxs.y, mins.z),
         glm::vec3(mins.x, maxs.y, maxs.z),
         glm::vec3(maxs.x, maxs.y, maxs.z),
         glm::vec3(maxs.x, maxs.y, mins.z),
         BrushFaceAxis::Y, 2},
        // -Y
        {glm::vec3(mins.x, mins.y, mins.z),
         glm::vec3(maxs.x, mins.y, mins.z),
         glm::vec3(maxs.x, mins.y, maxs.z),
         glm::vec3(mins.x, mins.y, maxs.z),
         BrushFaceAxis::Y, 3},
        // +Z
        {glm::vec3(mins.x, mins.y, maxs.z),
         glm::vec3(maxs.x, mins.y, maxs.z),
         glm::vec3(maxs.x, maxs.y, maxs.z),
         glm::vec3(mins.x, maxs.y, maxs.z),
         BrushFaceAxis::Z, 4},
        // -Z
        {glm::vec3(mins.x, mins.y, mins.z),
         glm::vec3(mins.x, maxs.y, mins.z),
         glm::vec3(maxs.x, maxs.y, mins.z),
         glm::vec3(maxs.x, mins.y, mins.z),
         BrushFaceAxis::Z, 5},
    }};

    return faces;
}

static std::string resolveBrushFaceTexturePath(const BrushVolume &brush, int faceIndex)
{
    if (faceIndex >= 0 && faceIndex < (int)brush.faceTextures.size())
    {
        const std::string &faceTex = brush.faceTextures[(size_t)faceIndex];
        if (!faceTex.empty())
            return faceTex;
    }
    return brush.texturePath;
}

static Texture *loadEditorTexture(const std::string &path)
{
    auto &texMgr = TextureManager::instance();
    if (path.empty())
        return texMgr.getWhite();

    const std::string name = "editor::" + path;
    if (Texture *cached = texMgr.get(name))
        return cached;

    if (Texture *loaded = texMgr.load(name, path))
        return loaded;

    return texMgr.getWhite();
}

static glm::vec2 computeBrushUV(const BrushVolume &brush,
                                const BrushFaceGeometry &face,
                                const glm::vec3 &p,
                                float texWidth,
                                float texHeight)
{
    const BrushVolume::FaceUV &faceUv = brush.faceUV[(size_t)face.faceIndex];
    const glm::vec2 combinedScale(
        brush.uvScale.x * faceUv.scale.x,
        brush.uvScale.y * faceUv.scale.y);
    const glm::vec2 combinedOffset(
        brush.uvOffset.x + faceUv.offset.x,
        brush.uvOffset.y + faceUv.offset.y);
    const float combinedRotation = brush.uvRotation + faceUv.rotation;

    // Face-local axes from geometry normal (avoids hardcoded face-index mapping).
    const glm::vec3 edge1 = face.p1 - face.p0;
    const glm::vec3 edge2 = face.p2 - face.p0;
    glm::vec3 normal = glm::cross(edge1, edge2);
    if (glm::length2(normal) <= 1e-8f)
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
    else
        normal = glm::normalize(normal);
    const glm::vec3 absN = glm::abs(normal);

    glm::vec3 uAxis(1.0f, 0.0f, 0.0f);
    glm::vec3 vAxis(0.0f, 0.0f, 1.0f);
    if (absN.y >= absN.x && absN.y >= absN.z)
    {
        uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        vAxis = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    else if (absN.x >= absN.z)
    {
        uAxis = glm::vec3(0.0f, 0.0f, 1.0f);
        vAxis = glm::vec3(0.0f, -1.0f, 0.0f);
    }
    else
    {
        uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        vAxis = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    const float angle = glm::radians(combinedRotation);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const glm::vec3 uRot = uAxis * c - vAxis * s;
    const glm::vec3 vRot = uAxis * s + vAxis * c;

    const float safeTexW = glm::max(texWidth, 1.0f);
    const float safeTexH = glm::max(texHeight, 1.0f);
    const float safeScaleU = std::fabs(combinedScale.x) > 1e-6f ? combinedScale.x : 1.0f;
    const float safeScaleV = std::fabs(combinedScale.y) > 1e-6f ? combinedScale.y : 1.0f;

    return glm::vec2(
        glm::dot(p, uRot) / (safeTexW * safeScaleU) + combinedOffset.x,
        glm::dot(p, vRot) / (safeTexH * safeScaleV) + combinedOffset.y);
}

static glm::vec2 brushFaceWorldSize(const BrushVolume &brush, int faceIndex)
{
    const glm::vec3 size = glm::max(brush.maxs - brush.mins, glm::vec3(1.0f));
    switch (faceIndex)
    {
    case 0: // +X
    case 1: // -X
        return glm::vec2(size.z, size.y);
    case 2: // +Y
    case 3: // -Y
        return glm::vec2(size.x, size.z);
    case 4: // +Z
    case 5: // -Z
        return glm::vec2(size.x, size.y);
    default:
        break;
    }
    return glm::vec2(size.x, size.y);
}

static void fitBrushFaceUvToTexture(BrushVolume &brush, int faceIndex, const Texture *texture)
{
    if (!texture || texture->width <= 0 || texture->height <= 0 || faceIndex < 0 || faceIndex >= 6)
        return;

    const glm::vec2 worldSize = brushFaceWorldSize(brush, faceIndex);
    const float targetU = worldSize.x / (float)texture->width;
    const float targetV = worldSize.y / (float)texture->height;

    const float baseU = std::fabs(brush.uvScale.x) > 1e-6f ? brush.uvScale.x : 1.0f;
    const float baseV = std::fabs(brush.uvScale.y) > 1e-6f ? brush.uvScale.y : 1.0f;

    BrushVolume::FaceUV &faceUv = brush.faceUV[(size_t)faceIndex];
    faceUv.scale.x = targetU / baseU;
    faceUv.scale.y = targetV / baseV;
}

static void drawBrushWireframe(RenderBatch &batch, const BrushVolume &brush)
{
    const glm::vec3 &mins = brush.mins;
    const glm::vec3 &maxs = brush.maxs;

    batch.Line3D(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z));
    batch.Line3D(glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z));
    batch.Line3D(glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(mins.x, mins.y, maxs.z));
    batch.Line3D(glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(mins.x, mins.y, mins.z));

    batch.Line3D(glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z));
    batch.Line3D(glm::vec3(maxs.x, maxs.y, mins.z), glm::vec3(maxs.x, maxs.y, maxs.z));
    batch.Line3D(glm::vec3(maxs.x, maxs.y, maxs.z), glm::vec3(mins.x, maxs.y, maxs.z));
    batch.Line3D(glm::vec3(mins.x, maxs.y, maxs.z), glm::vec3(mins.x, maxs.y, mins.z));

    batch.Line3D(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, maxs.y, mins.z));
    batch.Line3D(glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z));
    batch.Line3D(glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z));
    batch.Line3D(glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(mins.x, maxs.y, maxs.z));
}

static void drawBrushFaceOutline(RenderBatch &batch, const BrushVolume &brush, int faceIndex)
{
    if (faceIndex < 0 || faceIndex >= 6)
        return;

    const auto faces = buildBrushFaces(brush);
    for (const BrushFaceGeometry &face : faces)
    {
        if (face.faceIndex != faceIndex)
            continue;

        glm::vec3 edge1 = face.p1 - face.p0;
        glm::vec3 edge2 = face.p2 - face.p0;
        glm::vec3 normal = glm::cross(edge1, edge2);
        if (glm::length2(normal) <= 1e-8f)
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
        else
            normal = glm::normalize(normal);

        const float offset = 0.05f;
        const glm::vec3 p0 = face.p0 + normal * offset;
        const glm::vec3 p1 = face.p1 + normal * offset;
        const glm::vec3 p2 = face.p2 + normal * offset;
        const glm::vec3 p3 = face.p3 + normal * offset;

        batch.Line3D(p0, p1);
        batch.Line3D(p1, p2);
        batch.Line3D(p2, p3);
        batch.Line3D(p3, p0);
        break;
    }
}

static void drawBrushSolid(RenderBatch &batch, const BrushVolume &brush)
{
    const auto faces = buildBrushFaces(brush);
    for (const BrushFaceGeometry &face : faces)
    {
        batch.Triangle(face.p0, face.p1, face.p2);
        batch.Triangle(face.p0, face.p2, face.p3);
    }
}

static void drawBrushTextured(RenderBatch &batch, const BrushVolume &brush)
{
    const auto faces = buildBrushFaces(brush);
    for (const BrushFaceGeometry &face : faces)
    {
        const std::string texturePath = resolveBrushFaceTexturePath(brush, face.faceIndex);
        Texture *texture = loadEditorTexture(texturePath);
        batch.SetTexture(texture ? texture->id : 0u);
        const float texW = (texture && texture->width > 0) ? (float)texture->width : 1.0f;
        const float texH = (texture && texture->height > 0) ? (float)texture->height : 1.0f;

        const glm::vec2 uv0 = computeBrushUV(brush, face, face.p0, texW, texH);
        const glm::vec2 uv1 = computeBrushUV(brush, face, face.p1, texW, texH);
        const glm::vec2 uv2 = computeBrushUV(brush, face, face.p2, texW, texH);
        const glm::vec2 uv3 = computeBrushUV(brush, face, face.p3, texW, texH);

        batch.Triangle(face.p0, face.p1, face.p2, uv0, uv1, uv2);
        batch.Triangle(face.p0, face.p2, face.p3, uv0, uv2, uv3);
    }
}

static bool brushVisibleInOrthoView(const BrushVolume &brush,
                                    const EditorView &view,
                                    const glm::vec3 &focus)
{
    if (view.type == EditorViewType::Perspective)
        return true;

    const float aspect = (view.rect.h > 0) ? ((float)view.rect.w / (float)view.rect.h) : 1.0f;
    const float halfH = view.orthoSize;
    const float halfW = halfH * aspect;

    const auto overlaps = [](float minA, float maxA, float minB, float maxB) -> bool
    {
        return !(maxA < minB || minA > maxB);
    };

    switch (view.type)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        return overlaps(brush.mins.x, brush.maxs.x, focus.x - halfW, focus.x + halfW) &&
               overlaps(brush.mins.z, brush.maxs.z, focus.z - halfH, focus.z + halfH);
    case EditorViewType::Front:
    case EditorViewType::Back:
        return overlaps(brush.mins.x, brush.maxs.x, focus.x - halfW, focus.x + halfW) &&
               overlaps(brush.mins.y, brush.maxs.y, focus.y - halfH, focus.y + halfH);
    case EditorViewType::Left:
    case EditorViewType::Right:
        return overlaps(brush.mins.z, brush.maxs.z, focus.z - halfW, focus.z + halfW) &&
               overlaps(brush.mins.y, brush.maxs.y, focus.y - halfH, focus.y + halfH);
    case EditorViewType::Perspective:
        break;
    }
    return true;
}

static bool brushInFrustum(const BrushVolume &brush, const Camera &cam)
{
    if (cam.frustum.isInfinite())
        return true;

    for (const Plane &plane : cam.frustum.planes)
    {
        glm::vec3 positiveVertex = brush.mins;
        if (plane.normal.x >= 0.0f) positiveVertex.x = brush.maxs.x;
        if (plane.normal.y >= 0.0f) positiveVertex.y = brush.maxs.y;
        if (plane.normal.z >= 0.0f) positiveVertex.z = brush.maxs.z;

        if (glm::dot(plane.normal, positiveVertex) + plane.d < 0.0f)
            return false;
    }
    return true;
}

static void drawBrushes(RenderBatch &batch,
                        const EditorView &view,
                        const std::vector<BrushVolume> &brushes,
                        const std::vector<int> &selectedBrushes,
                        int primarySelectedBrush,
                        int selectedFace,
                        const PendingBrush &pending,
                        EditorViewType activePreviewView,
                        const glm::vec3 &previewEnd,
                        float defaultBrushThickness,
                        float defaultBrushHeight,
                        const glm::vec3 &focus,
                        const std::string &currentTexturePath,
                        EditorRenderingMode renderingMode)
{
    for (int i = 0; i < (int)brushes.size(); ++i)
    {
        const BrushVolume &brush = brushes[i];
        if (brush.hidden)
            continue;
        const bool isSelected = selectionContains(selectedBrushes, i);
        if (!isSelected)
        {
            if (view.type == EditorViewType::Perspective)
            {
                if (!brushInFrustum(brush, view.camera))
                    continue;
            }
            else if (!brushVisibleInOrthoView(brush, view, focus))
            {
                continue;
            }
        }

        if (renderingMode == EditorRenderingMode::Wireframe)
        {
            if (isSelected)
                batch.SetColor(255, 170, 70, 255);
            else
                batch.SetColor((u8)glm::clamp(brush.color.x * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.y * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.z * 255.0f, 0.0f, 255.0f),
                               255);
            drawBrushWireframe(batch, brush);
        }
        else if (renderingMode == EditorRenderingMode::Textured)
        {
            batch.SetColor(255, 255, 255, 255);
            drawBrushTextured(batch, brush);
            if (isSelected)
            {
                batch.SetColor(255, 180, 70, 255);
                drawBrushWireframe(batch, brush);
            }
        }
        else
        {
            if (isSelected)
                batch.SetColor(255, 180, 70, 255);
            else
                batch.SetColor((u8)glm::clamp(brush.color.x * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.y * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.z * 255.0f, 0.0f, 255.0f),
                               255);
            drawBrushSolid(batch, brush);
            if (isSelected)
            {
                batch.SetColor(255, 185, 80, 255);
                drawBrushWireframe(batch, brush);
            }
        }

        if (isSelected && i == primarySelectedBrush)
        {
            batch.SetColor(255, 235, 80, 255);
            drawBrushFaceOutline(batch, brush, selectedFace);
        }
    }

    if (pending.active && pending.view == activePreviewView)
    {
        const BrushVolume preview = makeBrushFromDrag(
            pending.view,
            pending.start,
            previewEnd,
            defaultBrushThickness,
            defaultBrushHeight,
            focus,
            currentTexturePath);
        batch.SetColor(255, 210, 80, 255);
        if (renderingMode == EditorRenderingMode::Wireframe)
        {
            drawBrushWireframe(batch, preview);
        }
        else if (renderingMode == EditorRenderingMode::Textured)
        {
            batch.SetColor(255, 255, 255, 255);
            drawBrushTextured(batch, preview);
            batch.SetColor(255, 210, 80, 255);
            drawBrushWireframe(batch, preview);
        }
        else
        {
            drawBrushSolid(batch, preview);
            batch.SetColor(255, 210, 80, 255);
            drawBrushWireframe(batch, preview);
        }
    }
}

static void renderEditorView(RenderBatch &batch,
                             const EditorView &view,
                             int screenHeight,
                             const std::vector<BrushVolume> &brushes,
                             const std::vector<int> &selectedBrushes,
                             int primarySelectedBrush,
                             int selectedFace,
                             const PendingBrush &pendingBrush,
                             bool showGrid,
                             bool showAxes,
                             float gridStep,
                             float defaultBrushThickness,
                             float defaultBrushHeight,
                             const glm::vec3 &focus,
                             const glm::vec3 &hoverWorld,
                             const std::string &currentTexturePath,
                             EditorRenderingMode renderingMode)
{
    EditorRenderingMode effectiveMode = renderingMode;
    if (view.type != EditorViewType::Perspective)
        effectiveMode = EditorRenderingMode::Wireframe;

    const int viewportX = view.rect.x;
    const int viewportY = screenHeight - view.rect.y - view.rect.h;

    glEnable(GL_SCISSOR_TEST);
    glViewport(viewportX, viewportY, view.rect.w, view.rect.h);
    glScissor(viewportX, viewportY, view.rect.w, view.rect.h);
    glClearColor(view.clearColor.r, view.clearColor.g, view.clearColor.b, view.clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Material::applyDefaultStates();
    batch.SetMatrix(view.camera.viewProjection);

    if (showGrid)
    {
        const float extent = (view.type == EditorViewType::Perspective)
                                 ? glm::max(512.0f, view.perspectiveDistance)
                                 : glm::max(view.orthoSize * 1.5f, 128.0f);
        drawGridForView(batch, view.type, focus, extent, gridStep);
    }

    if (showAxes)
        drawAxes(batch, glm::max(64.0f, gridStep * 2.0f));

    drawBrushes(batch,
                view,
                brushes,
                selectedBrushes,
                primarySelectedBrush,
                selectedFace,
                pendingBrush,
                view.type,
                hoverWorld,
                defaultBrushThickness,
                defaultBrushHeight,
                focus,
                currentTexturePath,
                effectiveMode);

    batch.Render();

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, 0, 0);
}

static void drawScreenOverlay(RenderBatch &batch,
                              int screenWidth,
                              int screenHeight,
                              const std::array<EditorView, 4> &views,
                              EditorViewType hoveredView,
                              bool hasHoveredView)
{
    batch.SetMatrix(screenOrthoMatrix(screenWidth, screenHeight));

    for (const EditorView &view : views)
    {
        const bool hovered = hasHoveredView && view.type == hoveredView;
        batch.SetColor(hovered ? 255 : 135,
                       hovered ? 225 : 135,
                       hovered ? 100 : 135,
                       255);
        batch.Rectangle(view.rect.x, view.rect.y, view.rect.w, view.rect.h, false);
    }

    batch.Render();
}

int main()
{
    Device &device = Device::Instance();
    if (!device.Create(1600, 900, "MiniRender Editor", true))
        return 1;

    device.ImGuiInit();

    RenderBatch worldBatch;
    RenderBatch overlayBatch;
    worldBatch.Init();
    overlayBatch.Init();

    std::array<EditorView, 4> views = {{
        {EditorViewType::Top, "Top"},
        {EditorViewType::Front, "Front"},
        {EditorViewType::Right, "Right"},
        {EditorViewType::Perspective, "3D"},
    }};
    syncViewLabels(views);

    std::vector<BrushVolume> brushes;
    std::vector<BrushVolume> brushClipboard;
    ImGuiConsole console;
    console.SetVisible(true);
    console.SetText("[editor] boot");
    std::vector<AssetEntry> assets;
    ImGuiFileDialog folderDialog;
    ImGuiFileDialog sceneDialog;
    std::string scenePath = "scenes/untitled.mred";
    
    // Load editor settings
    EditorSettings settings;
    if (loadEditorSettings(settings))
    {
        appendConsoleLine(console, "[editor] loaded settings from editor_settings.json");
    }
    else
    {
        appendConsoleLine(console, "[editor] using default settings");
    }
    
    // Apply loaded settings
    std::string assetRoot = settings.assetRoot;
    std::string assetFilter;
    std::string currentTexturePath = settings.currentTexturePath;
    bool assetViewAsGrid = settings.assetViewAsGrid;
    rescanAssets(assetRoot, assets);
    appendConsoleLine(console, "[editor] scanned assets folder");

    glm::vec3 focus = settings.focus;
    PendingBrush pendingBrush;
    int selectedBrush = -1;
    std::vector<int> selectedBrushes;
    int selectedBrushFace = 2; // +Y by default
    BrushSelectionRect selectionRect;
    int orthoPopupViewIndex = -1;
    glm::vec3 orthoPopupWorld(0.0f);
    EditorTool currentTool = EditorTool::Select;
    bool draggingSelection = false;
    EditorTool dragTool = EditorTool::Move;
    EditorViewType dragView = EditorViewType::Top;
    BrushScaleAxis dragScaleAxis = BrushScaleAxis::None;
    bool dragScalePositiveFace = true;
    glm::vec3 dragStartWorld(0.0f);
    BrushVolume dragOriginalBrush;

    // Apply view settings
    for (int i = 0; i < 4; ++i)
    {
        views[i].orthoSize = settings.views[i].orthoSize;
        views[i].perspectiveDistance = settings.views[i].perspectiveDistance;
        views[i].perspectiveYaw = settings.views[i].perspectiveYaw;
        views[i].perspectivePitch = settings.views[i].perspectivePitch;
    }

    const int margin = 12;
    const int gap = 8;

    while (device.Run())
    {
        const float dt = device.GetFrameTime();
        (void)dt;

        device.ImGuiBegin();
        ImGuiIO &io = ImGui::GetIO();
        const int topInset = (int)std::ceil(ImGui::GetFrameHeight());

        setupViewLayout(views, device.GetWidth(), device.GetHeight(), settings.sidebarWidth, settings.assetPanelHeight, settings.layoutMode, topInset, margin, gap);
        updateCameras(views, focus);

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Rescan Assets"))
                {
                    rescanAssets(assetRoot, assets);
                    appendConsoleLine(console, "[assets] rescanned " + assetRoot);
                }
                ImGui::Separator();
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kFloppyDisk) + " Save").c_str(), "Ctrl+S"))
                {
                    std::string error;
                    if (saveEditorScene(scenePath, brushes, focus, currentTexturePath, error))
                        appendConsoleLine(console, "[scene] saved " + ensureSceneExtension(scenePath).string());
                    else
                        appendConsoleLine(console, "[scene] save failed: " + error);
                }
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kFloppyDisk) + " Save As...").c_str(), "Ctrl+Shift+S"))
                {
                    std::filesystem::path startDir = std::filesystem::current_path();
                    std::filesystem::path sceneFs(scenePath);
                    if (!sceneFs.parent_path().empty())
                        startDir = sceneFs.parent_path();
                    sceneDialog.Open(ImGuiFileDialog::Mode::SaveFile, startDir, sceneFs.filename().string());
                }
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kFolderOpen) + " Open...").c_str(), "Ctrl+O"))
                {
                    std::filesystem::path startDir = std::filesystem::current_path();
                    std::filesystem::path sceneFs(scenePath);
                    if (!sceneFs.parent_path().empty())
                        startDir = sceneFs.parent_path();
                    sceneDialog.Open(ImGuiFileDialog::Mode::OpenFile, startDir, sceneFs.filename().string());
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                const bool twoViews = settings.layoutMode == EditorLayoutMode::TwoViews;
                const bool threeViews = settings.layoutMode == EditorLayoutMode::ThreeViews;
                const bool fourViews = settings.layoutMode == EditorLayoutMode::FourViews;
                if (ImGui::MenuItem("2 Views", nullptr, twoViews))
                    settings.layoutMode = EditorLayoutMode::TwoViews;
                if (ImGui::MenuItem("3 Views", nullptr, threeViews))
                    settings.layoutMode = EditorLayoutMode::ThreeViews;
                if (ImGui::MenuItem("4 Views", nullptr, fourViews))
                    settings.layoutMode = EditorLayoutMode::FourViews;
                ImGui::Separator();
                ImGui::MenuItem("Show Grid", nullptr, &settings.showGrid);
                ImGui::MenuItem("Show Axes", nullptr, &settings.showAxes);
                ImGui::MenuItem("Snap", nullptr, &settings.snapEnabled);
                ImGui::Separator();
                if (ImGui::MenuItem("Solid", nullptr, settings.renderingMode == EditorRenderingMode::Solid))
                    settings.renderingMode = EditorRenderingMode::Solid;
                if (ImGui::MenuItem("Wireframe", nullptr, settings.renderingMode == EditorRenderingMode::Wireframe))
                    settings.renderingMode = EditorRenderingMode::Wireframe;
                if (ImGui::MenuItem("Textured", nullptr, settings.renderingMode == EditorRenderingMode::Textured))
                    settings.renderingMode = EditorRenderingMode::Textured;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools"))
            {
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kCode) + " Select").c_str(), "1", currentTool == EditorTool::Select))
                    currentTool = EditorTool::Select;
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kTerminal) + " Move").c_str(), "2", currentTool == EditorTool::Move))
                    currentTool = EditorTool::Move;
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kChevronUp) + " Scale").c_str(), "3", currentTool == EditorTool::Scale))
                    currentTool = EditorTool::Scale;
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kFileCode) + " Face").c_str(), "4", currentTool == EditorTool::Face))
                    currentTool = EditorTool::Face;
                if (ImGui::MenuItem((std::string(ImGuiFontAwesome::kGears) + " Brush").c_str(), "5", currentTool == EditorTool::Brush))
                    currentTool = EditorTool::Brush;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Layout"))
            {
                const char *viewItems[] = {"Top", "Bottom", "Front", "Back", "Left", "Right", "3D"};
                const int activeViews = layoutViewCount(settings.layoutMode);
                for (int i = 0; i < activeViews; ++i)
                {
                    int current = viewTypeToComboIndex(views[i].type);
                    std::string label = "View " + std::to_string(i + 1);
                    if (ImGui::Combo(label.c_str(), &current, viewItems, IM_ARRAYSIZE(viewItems)))
                    {
                        views[i].type = comboIndexToViewType(current);
                        syncViewLabels(views);
                    }
                }
                if (ImGui::MenuItem("Reset Layout"))
                {
                    views[0].type = EditorViewType::Top;
                    views[1].type = EditorViewType::Front;
                    views[2].type = EditorViewType::Right;
                    views[3].type = EditorViewType::Perspective;
                    settings.layoutMode = EditorLayoutMode::FourViews;
                    syncViewLabels(views);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        ImGui::SetNextWindowPos(ImVec2(12.0f, (float)topInset + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)settings.sidebarWidth - 12.0f, (float)device.GetHeight() - (float)topInset - 24.0f), ImGuiCond_Always);
        if (ImGui::Begin("Editor"))
        {
            ImGui::TextWrapped("Editor de brushes com 4 views. Use Select/Move/Scale/Face/Brush nas vistas ortograficas e 3D.");
            ImGui::Separator();

            if (ImGui::Button((std::string(ImGuiFontAwesome::kCode) + " Select").c_str()))
                currentTool = EditorTool::Select;
            ImGui::SameLine();
            if (ImGui::Button((std::string(ImGuiFontAwesome::kTerminal) + " Move").c_str()))
                currentTool = EditorTool::Move;
            ImGui::SameLine();
            if (ImGui::Button((std::string(ImGuiFontAwesome::kChevronUp) + " Scale").c_str()))
                currentTool = EditorTool::Scale;
            ImGui::SameLine();
            if (ImGui::Button((std::string(ImGuiFontAwesome::kFileCode) + " Face").c_str()))
                currentTool = EditorTool::Face;
            ImGui::SameLine();
            if (ImGui::Button((std::string(ImGuiFontAwesome::kGears) + " Brush").c_str()))
                currentTool = EditorTool::Brush;
            ImGui::Text("Current Tool: %s", toolName(currentTool));
            ImGui::Separator();

            const float panelWidth = ImGui::GetContentRegionAvail().x;
            const float totalHeight = ImGui::GetContentRegionAvail().y;
            const float minTop = 260.0f;
            const float minBottom = 120.0f;
            if (settings.sidebarTopHeight > totalHeight - minBottom)
                settings.sidebarTopHeight = totalHeight - minBottom;
            if (settings.sidebarTopHeight < minTop)
                settings.sidebarTopHeight = minTop;

            ImGui::BeginChild("##editor_properties", ImVec2(-1.0f, settings.sidebarTopHeight), false);
            if (ImGui::Button("Seed Test Brushes"))
            {
                seedTestBrushes(brushes);
                selectedBrushes.clear();
                selectedBrush = brushes.empty() ? -1 : 0;
                if (selectedBrush >= 0)
                    selectionAddUnique(selectedBrushes, selectedBrush);
                appendConsoleLine(console, "[editor] reseeded test brushes");
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Brushes"))
            {
                brushes.clear();
                selectedBrushes.clear();
                selectedBrush = -1;
                pendingBrush.active = false;
                appendConsoleLine(console, "[editor] cleared all brushes");
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset View"))
            {
                focus = glm::vec3(0.0f);
                for (EditorView &view : views)
                {
                    view.orthoSize = 256.0f;
                    view.perspectiveDistance = 720.0f;
                    view.perspectiveYaw = 45.0f;
                    view.perspectivePitch = 28.0f;
                }
                pendingBrush.active = false;
                appendConsoleLine(console, "[editor] reset camera and focus");
            }

            if (ImGuiPropertyGrid::Section("Viewport", true) && ImGuiPropertyGrid::Begin("viewport_grid"))
            {
                ImGuiPropertyGrid::Checkbox("Show Grid", &settings.showGrid);
                ImGuiPropertyGrid::Checkbox("Show Axes", &settings.showAxes);
                ImGuiPropertyGrid::Checkbox("Snap", &settings.snapEnabled);
                ImGuiPropertyGrid::DragFloat("Grid Step", &settings.gridStep, 1.0f, 4.0f, 256.0f, "%.1f");
                ImGuiPropertyGrid::DragFloat("Snap Size", &settings.snapSize, 1.0f, 1.0f, 256.0f, "%.1f");
                const int activeViewsForGrid = layoutViewCount(settings.layoutMode);
                for (int i = 0; i < activeViewsForGrid; ++i)
                {
                    EditorView &view = views[i];
                    if (view.type == EditorViewType::Perspective)
                    {
                        ImGuiPropertyGrid::DragFloat((std::string("3D Distance ") + std::to_string(i + 1)).c_str(),
                                                     &view.perspectiveDistance, 2.0f, 32.0f, 4096.0f, "%.1f");
                        ImGuiPropertyGrid::DragFloat((std::string("3D Yaw ") + std::to_string(i + 1)).c_str(),
                                                     &view.perspectiveYaw, 0.25f, -360.0f, 360.0f, "%.1f");
                        ImGuiPropertyGrid::DragFloat((std::string("3D Pitch ") + std::to_string(i + 1)).c_str(),
                                                     &view.perspectivePitch, 0.25f, -89.0f, 89.0f, "%.1f");
                    }
                    else
                    {
                        ImGuiPropertyGrid::DragFloat((std::string(view.label) + " Zoom").c_str(),
                                                     &view.orthoSize, 1.0f, 16.0f, 2048.0f, "%.1f");
                    }
                }
                ImGuiPropertyGrid::End();
            }

            if (ImGuiPropertyGrid::Section("Scene", true) && ImGuiPropertyGrid::Begin("scene_grid"))
            {
                ImGuiPropertyGrid::Label("Brushes");
                ImGui::Text("%d", (int)brushes.size());
                ImGuiPropertyGrid::Label("Pending");
                ImGui::TextUnformatted(pendingBrush.active ? "yes" : "no");
                ImGuiPropertyGrid::Label("Views");
                ImGui::Text("%d", layoutViewCount(settings.layoutMode));
                ImGuiPropertyGrid::Label("Sidebar");
                ImGui::Text("%d px", settings.sidebarWidth);
                ImGuiPropertyGrid::Label("Texture");
                ImGui::TextWrapped("%s", currentTexturePath.empty() ? "(none)" : currentTexturePath.c_str());
                ImGuiPropertyGrid::Label("Scene");
                ImGui::TextWrapped("%s", scenePath.c_str());
                ImGuiPropertyGrid::Label("Tool");
                ImGui::TextUnformatted(toolName(currentTool));
                ImGuiPropertyGrid::Label("Selected Brush");
                ImGui::Text("%d", selectedBrush);
                ImGuiPropertyGrid::Label("Selection");
                ImGui::Text("%d", (int)selectedBrushes.size());
                ImGuiPropertyGrid::End();
            }

            if (ImGuiPropertyGrid::Section("Brush List", true))
            {
                ImGui::BeginChild("##brush_list", ImVec2(-1.0f, 130.0f), true);
                for (int i = 0; i < (int)brushes.size(); ++i)
                {
                    const BrushVolume &b = brushes[i];
                    const glm::vec3 size = b.maxs - b.mins;
                    char label[160];
                    std::snprintf(label, sizeof(label), "%s#%d  (%.0f %.0f %.0f)",
                                  b.hidden ? "[H] " : "",
                                  i, size.x, size.y, size.z);
                    const bool rowSelected = selectionContains(selectedBrushes, i);
                    if (ImGui::Selectable(label, rowSelected))
                    {
                        if (ImGui::GetIO().KeyCtrl)
                        {
                            if (rowSelected)
                                selectionRemove(selectedBrushes, i);
                            else
                                selectionAddUnique(selectedBrushes, i);
                        }
                        else
                        {
                            selectedBrushes.clear();
                            selectionAddUnique(selectedBrushes, i);
                        }
                        selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                    }
                }
                ImGui::EndChild();
                if (!selectedBrushes.empty())
                {
                    if (ImGui::Button((std::string(ImGuiFontAwesome::kFile) + " Duplicate Brush").c_str()))
                    {
                        const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                        std::vector<int> newSelection = cloneSelectedBrushes(brushes, selectedBrushes, offset);
                        selectedBrushes = newSelection;
                        selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                        appendConsoleLine(console, "[edit] duplicated " + std::to_string((int)newSelection.size()) + " brush(es)");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete Brush"))
                    {
                        std::vector<int> source = selectedBrushes;
                        std::sort(source.begin(), source.end());
                        source.erase(std::unique(source.begin(), source.end()), source.end());
                        for (int i = (int)source.size() - 1; i >= 0; --i)
                        {
                            const int index = source[(size_t)i];
                            if (index < 0 || index >= (int)brushes.size())
                                continue;
                            brushes.erase(brushes.begin() + index);
                        }
                        selectedBrushes.clear();
                        selectedBrush = -1;
                        appendConsoleLine(console, "[edit] deleted selected brushes");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Randomize Colors"))
                    {
                        for (BrushVolume &b : brushes)
                            b.color = randomBrushColor();
                    }
                    if (ImGui::Button("Hide Selected"))
                    {
                        const std::vector<int> source = buildSortedUniqueValidSelection(selectedBrushes, (int)brushes.size());
                        for (int index : source)
                            brushes[(size_t)index].hidden = true;
                        appendConsoleLine(console, "[edit] hid " + std::to_string((int)source.size()) + " brush(es)");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Unhide Selected"))
                    {
                        const std::vector<int> source = buildSortedUniqueValidSelection(selectedBrushes, (int)brushes.size());
                        for (int index : source)
                            brushes[(size_t)index].hidden = false;
                        appendConsoleLine(console, "[edit] unhid " + std::to_string((int)source.size()) + " brush(es)");
                    }
                }
                if (ImGui::Button("Show All Hidden"))
                {
                    int count = 0;
                    for (BrushVolume &b : brushes)
                    {
                        if (b.hidden)
                            ++count;
                        b.hidden = false;
                    }
                    appendConsoleLine(console, "[edit] unhid all (" + std::to_string(count) + ")");
                }
            }

            if (!currentTexturePath.empty() && ImGuiPropertyGrid::Section("Current Texture", true))
            {
                // Load and display current texture
                std::string texName = "current_texture_preview";
                Texture *tex = TextureManager::instance().load(texName, currentTexturePath);
                
                if (tex)
                {
                    float aspectRatio = (float)tex->width / (float)tex->height;
                    float previewHeight = 100.0f;
                    float previewWidth = previewHeight * aspectRatio;
                    
                    ImGui::Text("%s", currentTexturePath.c_str());
                    ImGui::Text("%dx%d", tex->width, tex->height);
                    ImGui::Image((ImTextureID)(intptr_t)tex->id, ImVec2(previewWidth, previewHeight));
                }
                else
                {
                    ImGui::Text("Failed to load: %s", currentTexturePath.c_str());
                }
            }

            if (selectedBrush >= 0 && selectedBrush < (int)brushes.size() &&
                ImGuiPropertyGrid::Section("Brush", true) &&
                ImGuiPropertyGrid::Begin("brush_grid"))
            {
                BrushVolume &brush = brushes[selectedBrush];
                float mins[3] = {brush.mins.x, brush.mins.y, brush.mins.z};
                float maxs[3] = {brush.maxs.x, brush.maxs.y, brush.maxs.z};
                float color[3] = {brush.color.x, brush.color.y, brush.color.z};
                float uvOffset[2] = {brush.uvOffset.x, brush.uvOffset.y};
                float uvScale[2] = {brush.uvScale.x, brush.uvScale.y};
                if (selectedBrushFace < 0 || selectedBrushFace >= 6)
                    selectedBrushFace = 0;
                BrushVolume::FaceUV &faceUv = brush.faceUV[(size_t)selectedBrushFace];
                float faceUvOffset[2] = {faceUv.offset.x, faceUv.offset.y};
                float faceUvScale[2] = {faceUv.scale.x, faceUv.scale.y};
                const char *faceNames[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
                Texture *faceTexture = nullptr;
                const std::string faceTexturePath = resolveBrushFaceTexturePath(brush, selectedBrushFace);
                if (!faceTexturePath.empty())
                    faceTexture = loadEditorTexture(faceTexturePath);

                ImGuiPropertyGrid::Label("Texture");
                ImGui::TextWrapped("%s", brush.texturePath.empty() ? "(none)" : brush.texturePath.c_str());
                if (ImGui::Button("Use Current For Brush"))
                    brush.texturePath = currentTexturePath;
                ImGuiPropertyGrid::Label("Mins");
                if (ImGui::DragFloat3("##brush_mins", mins, 1.0f))
                    brush.mins = glm::vec3(mins[0], mins[1], mins[2]);
                ImGuiPropertyGrid::Label("Maxs");
                if (ImGui::DragFloat3("##brush_maxs", maxs, 1.0f))
                    brush.maxs = glm::vec3(maxs[0], maxs[1], maxs[2]);
                ImGuiPropertyGrid::Label("Color");
                if (ImGui::ColorEdit3("##brush_color", color))
                    brush.color = glm::vec3(color[0], color[1], color[2]);
                ImGuiPropertyGrid::Label("Hidden");
                ImGui::Checkbox("##brush_hidden", &brush.hidden);

                ImGuiPropertyGrid::Label("Brush UV Offset");
                if (ImGui::DragFloat2("##brush_uv_offset", uvOffset, 0.05f))
                    brush.uvOffset = glm::vec2(uvOffset[0], uvOffset[1]);
                ImGuiPropertyGrid::Label("Brush UV Scale");
                if (ImGui::DragFloat2("##brush_uv_scale", uvScale, 0.05f))
                    brush.uvScale = glm::vec2(uvScale[0], uvScale[1]);
                ImGuiPropertyGrid::DragFloat("Brush UV Rotation", &brush.uvRotation, 0.5f, -360.0f, 360.0f, "%.1f");

                ImGuiPropertyGrid::Label("Face");
                ImGui::Combo("##brush_face_select", &selectedBrushFace, faceNames, IM_ARRAYSIZE(faceNames));
                if (ImGui::Button("Left")) selectedBrushFace = faceLeft();
                ImGui::SameLine();
                if (ImGui::Button("Right")) selectedBrushFace = faceRight();
                ImGui::SameLine();
                if (ImGui::Button("Front")) selectedBrushFace = faceFront();
                ImGui::SameLine();
                if (ImGui::Button("Back")) selectedBrushFace = faceBack();
                if (ImGui::Button("Top")) selectedBrushFace = faceTop();
                ImGui::SameLine();
                if (ImGui::Button("Bottom")) selectedBrushFace = faceBottom();
                ImGuiPropertyGrid::Label("Face Texture");
                ImGui::TextWrapped("%s", faceTexturePath.empty() ? "(inherits brush)" : faceTexturePath.c_str());
                if (ImGui::Button("Use Current For Face"))
                    brush.faceTextures[(size_t)selectedBrushFace] = currentTexturePath;
                ImGui::SameLine();
                if (ImGui::Button("Clear Face Texture"))
                    brush.faceTextures[(size_t)selectedBrushFace].clear();
                if (ImGui::Button("Swap L/R")) swapBrushFaces(brush, faceLeft(), faceRight());
                ImGui::SameLine();
                if (ImGui::Button("Swap F/B")) swapBrushFaces(brush, faceFront(), faceBack());
                ImGui::SameLine();
                if (ImGui::Button("Swap T/B")) swapBrushFaces(brush, faceTop(), faceBottom());
                if (ImGui::Button("Rotate Y +90")) rotateBrushY90(brush, +1);
                ImGui::SameLine();
                if (ImGui::Button("Rotate Y -90")) rotateBrushY90(brush, -1);

                if (faceTexture)
                {
                    const float aspectRatio = (float)faceTexture->width / (float)faceTexture->height;
                    const float previewHeight = 72.0f;
                    const float previewWidth = previewHeight * aspectRatio;
                    ImGui::Image((ImTextureID)(intptr_t)faceTexture->id, ImVec2(previewWidth, previewHeight));
                }

                ImGuiPropertyGrid::Label("Face UV Offset");
                if (ImGui::DragFloat2("##face_uv_offset", faceUvOffset, 0.05f))
                    faceUv.offset = glm::vec2(faceUvOffset[0], faceUvOffset[1]);
                ImGuiPropertyGrid::Label("Face UV Scale");
                if (ImGui::DragFloat2("##face_uv_scale", faceUvScale, 0.05f))
                    faceUv.scale = glm::vec2(faceUvScale[0], faceUvScale[1]);
                ImGuiPropertyGrid::DragFloat("Face UV Rotation", &faceUv.rotation, 0.5f, -360.0f, 360.0f, "%.1f");

                if (ImGui::Button("Face Fit"))
                {
                    fitBrushFaceUvToTexture(brush, selectedBrushFace, faceTexture);
                }
                ImGui::SameLine();
                if (ImGui::Button("Face Rot +90"))
                {
                    faceUv.rotation += 90.0f;
                    if (faceUv.rotation > 360.0f)
                        faceUv.rotation -= 360.0f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Face Reset"))
                {
                    faceUv.offset = glm::vec2(0.0f);
                    faceUv.scale = glm::vec2(1.0f);
                    faceUv.rotation = 0.0f;
                }
                if (ImGui::Button("Reset All UV"))
                {
                    brush.uvOffset = glm::vec2(0.0f);
                    brush.uvScale = glm::vec2(1.0f);
                    brush.uvRotation = 0.0f;
                    for (BrushVolume::FaceUV &fuv : brush.faceUV)
                    {
                        fuv.offset = glm::vec2(0.0f);
                        fuv.scale = glm::vec2(1.0f);
                        fuv.rotation = 0.0f;
                    }
                }
                ImGuiPropertyGrid::End();
            }

            if (ImGuiPropertyGrid::Section("Layout", true) && ImGuiPropertyGrid::Begin("layout_grid"))
            {
                int layoutValue = layoutViewCount(settings.layoutMode);
                ImGuiPropertyGrid::Label("Mode");
                if (ImGui::RadioButton("2", layoutValue == 2))
                    settings.layoutMode = EditorLayoutMode::TwoViews;
                ImGui::SameLine();
                if (ImGui::RadioButton("3", layoutValue == 3))
                    settings.layoutMode = EditorLayoutMode::ThreeViews;
                ImGui::SameLine();
                if (ImGui::RadioButton("4", layoutValue == 4))
                    settings.layoutMode = EditorLayoutMode::FourViews;

                const char *viewItems[] = {"Top", "Bottom", "Front", "Back", "Left", "Right", "3D"};
                const int activeViews = layoutViewCount(settings.layoutMode);
                for (int i = 0; i < activeViews; ++i)
                {
                    int current = viewTypeToComboIndex(views[i].type);
                    ImGuiPropertyGrid::Label(("View " + std::to_string(i + 1)).c_str());
                    if (ImGui::Combo(("##layout_view_" + std::to_string(i)).c_str(), &current, viewItems, IM_ARRAYSIZE(viewItems)))
                    {
                        views[i].type = comboIndexToViewType(current);
                        syncViewLabels(views);
                    }
                }
                ImGuiPropertyGrid::End();
            }

            if (ImGuiPropertyGrid::Section("Focus", true) && ImGuiPropertyGrid::Begin("focus_grid"))
            {
                float focusValues[3] = {focus.x, focus.y, focus.z};
                ImGuiPropertyGrid::Label("Position");
                if (ImGui::DragFloat3("##focus_position", focusValues, 1.0f))
                    focus = glm::vec3(focusValues[0], focusValues[1], focusValues[2]);
                ImGuiPropertyGrid::End();
            }

            if (ImGuiPropertyGrid::Section("Brush Defaults", true) && ImGuiPropertyGrid::Begin("brush_defaults_grid"))
            {
                ImGuiPropertyGrid::DragFloat("Thickness", &settings.defaultBrushThickness, 1.0f, 1.0f, 2048.0f, "%.1f");
                ImGuiPropertyGrid::DragFloat("Height", &settings.defaultBrushHeight, 1.0f, 1.0f, 2048.0f, "%.1f");
                ImGuiPropertyGrid::End();
            }

            if (ImGuiPropertyGrid::Section("Controls", true))
            {
                ImGui::BulletText("1 Select, 2 Move, 3 Scale, 4 Face, 5 Brush");
                ImGui::BulletText("LMB ortho com Select: seleciona brush");
                ImGui::BulletText("Drag retangulo em Select: multi-selecao (Ctrl para adicionar)");
                ImGui::BulletText("Ctrl + clique: adiciona/remove da selecao");
                ImGui::BulletText("LMB ortho com Move: arrasta brush selecionado");
                ImGui::BulletText("LMB ortho com Scale: brush escala por eixo dominante (face)");
                ImGui::BulletText("Mouse wheel sobre uma view: zoom");
                ImGui::BulletText("RMB na view 2D: popup de centrar/reset");
                ImGui::BulletText("LMB na view 3D: orbit");
                ImGui::BulletText("MMB drag: pan (2D e 3D)");
                ImGui::BulletText("Ctrl+S guarda cena, Ctrl+Shift+S guarda como, Ctrl+O abre");
                ImGui::BulletText("Ctrl+C copia selecao, Ctrl+V cola, Ctrl+D clona");
                ImGui::BulletText("H esconde selecao, Shift+H mostra selecao");
                ImGui::BulletText("Delete remove o objeto selecionado");
                ImGui::BulletText("ESC fecha a app");
            }
            ImGui::EndChild();

            ImGuiSplitter::Horizontal("editor_sidebar_split", &settings.sidebarTopHeight, minTop, minBottom, panelWidth);

            const float consoleHeight = ImGui::GetContentRegionAvail().y;
            console.Render("Console", "editor actions", false, consoleHeight);
        }
        ImGui::End();

        const int assetWindowX = settings.sidebarWidth + margin;
        const int assetWindowY = device.GetHeight() - margin - settings.assetPanelHeight;
        const int assetWindowW = device.GetWidth() - assetWindowX - margin;

        ImGui::SetNextWindowPos(ImVec2((float)assetWindowX, (float)assetWindowY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)assetWindowW, (float)settings.assetPanelHeight), ImGuiCond_Always);
        if (ImGui::Begin("Assets"))
        {
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Root", &assetRoot);
            ImGui::SameLine();
            if (ImGui::Button("Folder..."))
            {
                folderDialog.Open(ImGuiFileDialog::Mode::ChooseFolder, resolveAssetRootPath(assetRoot));
            }
            ImGui::SameLine();
            if (ImGui::Button("Use assets"))
                assetRoot = "assets";
            ImGui::SameLine();
            if (ImGui::Button("Rescan"))
            {
                rescanAssets(assetRoot, assets);
                appendConsoleLine(console, "[assets] rescanned " + assetRoot);
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Filter", &assetFilter);
            ImGui::SameLine();
            ImGui::Checkbox("Grid View", &assetViewAsGrid);
            ImGui::SameLine();
            ImGui::Text("Current: %s", currentTexturePath.empty() ? "(none)" : currentTexturePath.c_str());

            ImGui::Separator();
            ImGui::BeginChild("##asset_list", ImVec2(-1.0f, -1.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
            
            if (assetViewAsGrid)
            {
                // Grid view with thumbnails
                const float thumbnailSize = 64.0f;
                const float padding = 4.0f;
                const int itemsPerRow = (int)((ImGui::GetContentRegionAvail().x - padding) / (thumbnailSize + padding));
                
                int itemIndex = 0;
                for (const AssetEntry &asset : assets)
                {
                    if (!containsInsensitive(asset.path, assetFilter) &&
                        !containsInsensitive(asset.name, assetFilter))
                        continue;

                    // Load texture for thumbnail
                    std::string texName = "asset_thumb_" + asset.path;
                    Texture *tex = TextureManager::instance().load(texName, asset.path);
                    
                    if (itemIndex > 0 && itemIndex % itemsPerRow != 0)
                        ImGui::SameLine();
                    
                    ImGui::PushID(itemIndex);
                    const bool selected = asset.path == currentTexturePath;
                    if (selected)
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
                    
                    // Use a placeholder texture if loading failed
                    ImTextureID textureId = (ImTextureID)(intptr_t)(tex ? tex->id : TextureManager::instance().getWhite()->id);
                    
                    if (ImGui::ImageButton(("##" + std::to_string(itemIndex)).c_str(), textureId, ImVec2(thumbnailSize, thumbnailSize)))
                    {
                        currentTexturePath = asset.path;
                        appendConsoleLine(console, "[assets] selected texture " + asset.path);
                    }
                    
                    if (selected)
                        ImGui::PopStyleColor();
                    
                    // Tooltip with filename and dimensions
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s", asset.name.c_str());
                        if (tex)
                        {
                            ImGui::Text("%dx%d", tex->width, tex->height);
                        }
                        else
                        {
                            ImGui::Text("Failed to load");
                        }
                        ImGui::EndTooltip();
                    }
                    
                    ImGui::PopID();
                    itemIndex++;
                }
            }
            else
            {
                // List view
                for (const AssetEntry &asset : assets)
                {
                    if (!containsInsensitive(asset.path, assetFilter) &&
                        !containsInsensitive(asset.name, assetFilter))
                        continue;

                    const bool selected = asset.path == currentTexturePath;
                    if (ImGui::Selectable(asset.path.c_str(), selected))
                    {
                        currentTexturePath = asset.path;
                        appendConsoleLine(console, "[assets] selected texture " + asset.path);
                    }
                }
            }
            
            ImGui::EndChild();
        }
        ImGui::End();

        // Handle folder dialog
        if (folderDialog.HasResult())
        {
            auto result = folderDialog.ConsumeResult();
            if (result.accepted)
            {
                assetRoot = makeAssetRootDisplayPath(result.path);
                rescanAssets(assetRoot, assets);
                appendConsoleLine(console, "[assets] changed root to " + assetRoot);
            }
        }
        folderDialog.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());

        if (sceneDialog.HasResult())
        {
            auto result = sceneDialog.ConsumeResult();
            if (result.accepted)
            {
                if (result.mode == ImGuiFileDialog::Mode::OpenFile)
                {
                    std::string error;
                    if (loadEditorScene(result.path, brushes, focus, currentTexturePath, error))
                    {
                        scenePath = result.path.string();
                        selectedBrushes.clear();
                        selectedBrush = -1;
                        appendConsoleLine(console, "[scene] loaded " + scenePath);
                        if (!error.empty())
                            appendConsoleLine(console, "[scene] " + error);
                    }
                    else
                    {
                        appendConsoleLine(console, "[scene] load failed: " + error);
                    }
                }
                else if (result.mode == ImGuiFileDialog::Mode::SaveFile)
                {
                    std::string error;
                    const std::filesystem::path finalPath = ensureSceneExtension(result.path);
                    if (saveEditorScene(finalPath, brushes, focus, currentTexturePath, error))
                    {
                        scenePath = finalPath.string();
                        appendConsoleLine(console, "[scene] saved " + scenePath);
                    }
                    else
                    {
                        appendConsoleLine(console, "[scene] save failed: " + error);
                    }
                }
            }
        }
        sceneDialog.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());

        const int activeViews = layoutViewCount(settings.layoutMode);
        drawViewToolPalettes(views, activeViews, currentTool);

        const glm::vec2 mousePos = Input::GetMousePosition();
        EditorViewType hoveredView = EditorViewType::Top;
        bool hasHoveredView = false;
        EditorView *hoveredViewPtr = nullptr;

        for (int i = 0; i < activeViews; ++i)
        {
            EditorView &view = views[i];
            if (view.rect.contains(mousePos))
            {
                hoveredView = view.type;
                hasHoveredView = true;
                hoveredViewPtr = &view;
                break;
            }
        }

        glm::vec3 hoveredWorld(0.0f);
        if (hoveredViewPtr && hoveredViewPtr->type != EditorViewType::Perspective)
        {
            hoveredWorld = snapPoint(
                orthoPointFromScreen(*hoveredViewPtr, focus, mousePos),
                settings.snapSize,
                settings.snapEnabled);
        }

        const bool ctrlDown = Input::IsKeyDown(KEY_LEFT_CONTROL) || Input::IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shiftDown = Input::IsKeyDown(KEY_LEFT_SHIFT) || Input::IsKeyDown(KEY_RIGHT_SHIFT);

        if (!io.WantCaptureKeyboard)
        {
            if (Input::IsKeyPressed(KEY_ONE)) currentTool = EditorTool::Select;
            if (Input::IsKeyPressed(KEY_TWO)) currentTool = EditorTool::Move;
            if (Input::IsKeyPressed(KEY_THREE)) currentTool = EditorTool::Scale;
            if (Input::IsKeyPressed(KEY_FOUR)) currentTool = EditorTool::Face;
            if (Input::IsKeyPressed(KEY_FIVE)) currentTool = EditorTool::Brush;

            if (ctrlDown && Input::IsKeyPressed(KEY_S))
            {
                if (shiftDown)
                {
                    std::filesystem::path startDir = std::filesystem::current_path();
                    std::filesystem::path sceneFs(scenePath);
                    if (!sceneFs.parent_path().empty())
                        startDir = sceneFs.parent_path();
                    sceneDialog.Open(ImGuiFileDialog::Mode::SaveFile, startDir, sceneFs.filename().string());
                }
                else
                {
                    std::string error;
                    if (saveEditorScene(scenePath, brushes, focus, currentTexturePath, error))
                        appendConsoleLine(console, "[scene] saved " + ensureSceneExtension(scenePath).string());
                    else
                        appendConsoleLine(console, "[scene] save failed: " + error);
                }
            }

            if (ctrlDown && Input::IsKeyPressed(KEY_O))
            {
                std::filesystem::path startDir = std::filesystem::current_path();
                std::filesystem::path sceneFs(scenePath);
                if (!sceneFs.parent_path().empty())
                    startDir = sceneFs.parent_path();
                sceneDialog.Open(ImGuiFileDialog::Mode::OpenFile, startDir, sceneFs.filename().string());
            }

            if (ctrlDown && Input::IsKeyPressed(KEY_C) && !selectedBrushes.empty())
            {
                brushClipboard = copySelectedBrushes(brushes, selectedBrushes);
                appendConsoleLine(console, "[edit] copied selected brushes (Ctrl+C)");
            }

            if (ctrlDown && Input::IsKeyPressed(KEY_V) && !brushClipboard.empty())
            {
                const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                std::vector<int> newSelection = pasteBrushClipboard(brushes, brushClipboard, offset);
                selectedBrushes = newSelection;
                selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                appendConsoleLine(console, "[edit] pasted brushes (Ctrl+V)");
            }

            if (ctrlDown && Input::IsKeyPressed(KEY_D) && !selectedBrushes.empty())
            {
                const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                std::vector<int> newSelection = cloneSelectedBrushes(brushes, selectedBrushes, offset);
                selectedBrushes = newSelection;
                selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                appendConsoleLine(console, "[edit] duplicated selected brushes (Ctrl+D)");
            }

            if (!selectedBrushes.empty() && Input::IsKeyPressed(KEY_H))
            {
                const std::vector<int> source = buildSortedUniqueValidSelection(selectedBrushes, (int)brushes.size());
                for (int index : source)
                    brushes[(size_t)index].hidden = !shiftDown;
                appendConsoleLine(console, shiftDown ? "[edit] unhid selected brushes (Shift+H)" : "[edit] hid selected brushes (H)");
            }

            if (!selectedBrushes.empty() && Input::IsKeyPressed(KEY_DELETE))
            {
                std::vector<int> source = selectedBrushes;
                std::sort(source.begin(), source.end());
                source.erase(std::unique(source.begin(), source.end()), source.end());
                for (int i = (int)source.size() - 1; i >= 0; --i)
                {
                    const int index = source[(size_t)i];
                    if (index < 0 || index >= (int)brushes.size())
                        continue;
                    brushes.erase(brushes.begin() + index);
                }
                appendConsoleLine(console, "[edit] deleted selected brushes");
                selectedBrushes.clear();
                selectedBrush = -1;
                draggingSelection = false;
            }
        }

        if (!io.WantCaptureMouse && hoveredViewPtr)
        {
            if (Input::IsMouseDown(MouseButton::MIDDLE))
            {
                const glm::vec2 delta = Input::GetMouseDelta();
                if (hoveredViewPtr->type == EditorViewType::Perspective)
                    panFocusInPerspective(focus, *hoveredViewPtr, delta);
                else
                    panFocusInOrthoView(focus, *hoveredViewPtr, delta);
            }

            const float wheel = Input::GetMouseWheelMoveV();
            if (std::fabs(wheel) > 1e-4f)
            {
                if (hoveredViewPtr->type == EditorViewType::Perspective)
                    hoveredViewPtr->perspectiveDistance = glm::clamp(hoveredViewPtr->perspectiveDistance - wheel * 32.0f, 32.0f, 4096.0f);
                else
                    hoveredViewPtr->orthoSize = glm::clamp(hoveredViewPtr->orthoSize - wheel * 16.0f, 16.0f, 2048.0f);
            }

            if (hoveredViewPtr->type == EditorViewType::Perspective && Input::IsMouseDown(MouseButton::LEFT))
            {
                const glm::vec2 delta = Input::GetMouseDelta();
                hoveredViewPtr->perspectiveYaw += delta.x * 0.25f;
                hoveredViewPtr->perspectivePitch = glm::clamp(hoveredViewPtr->perspectivePitch + delta.y * 0.25f, -89.0f, 89.0f);
            }

            if (hoveredViewPtr->type == EditorViewType::Perspective &&
                Input::IsMousePressed(MouseButton::LEFT) &&
                (currentTool == EditorTool::Select || currentTool == EditorTool::Face))
            {
                const glm::vec2 localMouse(
                    mousePos.x - (float)hoveredViewPtr->rect.x,
                    mousePos.y - (float)hoveredViewPtr->rect.y);
                const Ray ray = hoveredViewPtr->camera.getRay(localMouse.x, localMouse.y);

                int hitBrush = -1;
                int hitFace = -1;
                if (pickBrushWithRay(brushes, ray, hitBrush, hitFace))
                {
                    if (ctrlDown)
                    {
                        if (selectionContains(selectedBrushes, hitBrush))
                            selectionRemove(selectedBrushes, hitBrush);
                        else
                            selectionAddUnique(selectedBrushes, hitBrush);
                    }
                    else
                    {
                        selectedBrushes.clear();
                        selectionAddUnique(selectedBrushes, hitBrush);
                    }
                    selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                    if (hitFace >= 0)
                        selectedBrushFace = hitFace;
                    appendConsoleLine(console,
                                      "[pick3d] brush " + std::to_string(selectedBrush) +
                                      " face " + brushFaceName(selectedBrushFace));
                }
            }

            if (hoveredViewPtr->type != EditorViewType::Perspective && Input::IsMousePressed(MouseButton::LEFT))
            {
                if (currentTool == EditorTool::Select)
                {
                    selectionRect.active = true;
                    selectionRect.additive = ctrlDown;
                    selectionRect.viewType = hoveredViewPtr->type;
                    selectionRect.viewIndex = -1;
                    for (int i = 0; i < activeViews; ++i)
                    {
                        if (&views[i] == hoveredViewPtr)
                        {
                            selectionRect.viewIndex = i;
                            break;
                        }
                    }
                    selectionRect.startMouse = mousePos;
                    selectionRect.endMouse = mousePos;
                    selectionRect.startWorld = hoveredWorld;
                    selectionRect.endWorld = hoveredWorld;
                }
                else if (currentTool == EditorTool::Brush)
                {
                    if (!pendingBrush.active || pendingBrush.view != hoveredViewPtr->type)
                    {
                        pendingBrush.active = true;
                        pendingBrush.view = hoveredViewPtr->type;
                        pendingBrush.start = hoveredWorld;
                        appendConsoleLine(console, std::string("[brush] start in ") + viewTypeName(hoveredViewPtr->type));
                    }
                    else
                    {
                        BrushVolume brush = makeBrushFromDrag(
                            pendingBrush.view,
                            pendingBrush.start,
                            hoveredWorld,
                            settings.defaultBrushThickness,
                            settings.defaultBrushHeight,
                            focus,
                            currentTexturePath);
                        brushes.push_back(brush);
                        selectedBrush = (int)brushes.size() - 1;
                        selectedBrushes.clear();
                        selectionAddUnique(selectedBrushes, selectedBrush);
                        pendingBrush.active = false;
                        appendConsoleLine(console,
                                          "[brush] created brush " + std::to_string(selectedBrush) +
                                          " tex=" + currentTexturePath);
                    }
                }
                else if (currentTool == EditorTool::Face)
                {
                    const float pickDistance = glm::max(4.0f, hoveredViewPtr->orthoSize * 0.05f);
                    selectedBrush = findBrushAtPoint(brushes, hoveredViewPtr->type, hoveredWorld, pickDistance);
                    if (selectedBrush >= 0)
                    {
                        selectedBrushes.clear();
                        selectionAddUnique(selectedBrushes, selectedBrush);
                        selectedBrushFace = defaultFaceForView(hoveredViewPtr->type, selectedBrushFace);
                        appendConsoleLine(console,
                                          "[face] selected brush " + std::to_string(selectedBrush) +
                                          " face " + brushFaceName(selectedBrushFace));
                    }
                }
                else if (currentTool == EditorTool::Move)
                {
                    if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        draggingSelection = true;
                        dragTool = EditorTool::Move;
                        dragView = hoveredViewPtr->type;
                        dragStartWorld = hoveredWorld;
                        dragOriginalBrush = brushes[selectedBrush];
                        appendConsoleLine(console, "[move] begin drag brush " + std::to_string(selectedBrush));
                    }
                }
                else if (currentTool == EditorTool::Scale)
                {
                    if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        draggingSelection = true;
                        dragTool = EditorTool::Scale;
                        dragView = hoveredViewPtr->type;
                        dragScaleAxis = BrushScaleAxis::None;
                        dragScalePositiveFace = true;
                        dragStartWorld = hoveredWorld;
                        dragOriginalBrush = brushes[selectedBrush];
                        appendConsoleLine(console, "[scale] begin brush " + std::to_string(selectedBrush));
                    }
                }
            }

            if (draggingSelection &&
                hoveredViewPtr->type == dragView &&
                Input::IsMouseDown(MouseButton::LEFT))
            {
                if (dragTool == EditorTool::Move)
                {
                    const glm::vec3 delta = applyViewDelta(hoveredWorld - dragStartWorld, dragView);
                    if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        brushes[selectedBrush].mins = dragOriginalBrush.mins + delta;
                        brushes[selectedBrush].maxs = dragOriginalBrush.maxs + delta;
                    }
                }
                else if (dragTool == EditorTool::Scale)
                {
                    const glm::vec2 center2 = projectWorldToViewPlane((dragOriginalBrush.mins + dragOriginalBrush.maxs) * 0.5f, dragView);
                    const glm::vec2 start2 = projectWorldToViewPlane(dragStartWorld, dragView);
                    const glm::vec2 end2 = projectWorldToViewPlane(hoveredWorld, dragView);
                    const float startDist = glm::max(glm::length(start2 - center2), 1.0f);
                    const float endDist = glm::max(glm::length(end2 - center2), 1.0f);
                    (void)startDist;
                    (void)endDist;

                    if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        const glm::vec2 viewDelta = end2 - start2;
                        if (dragScaleAxis == BrushScaleAxis::None && glm::length2(viewDelta) > 1e-6f)
                        {
                            dragScaleAxis = chooseBrushScaleAxis(dragView, viewDelta);
                            const glm::vec3 center3 = (dragOriginalBrush.mins + dragOriginalBrush.maxs) * 0.5f;
                            const float startAxis = brushAxisValue(dragStartWorld, dragScaleAxis);
                            const float centerAxis = brushAxisValue(center3, dragScaleAxis);
                            dragScalePositiveFace = startAxis >= centerAxis;
                            selectedBrushFace = brushFaceForScaleAxis(dragScaleAxis, dragScalePositiveFace);
                        }

                        if (dragScaleAxis != BrushScaleAxis::None)
                        {
                            const glm::vec3 worldDelta = hoveredWorld - dragStartWorld;
                            const float deltaAxis = brushAxisValue(worldDelta, dragScaleAxis);
                            brushes[selectedBrush] = scaleBrushFaceAlongAxis(
                                dragOriginalBrush,
                                dragScaleAxis,
                                dragScalePositiveFace,
                                deltaAxis);
                        }
                    }
                }
            }
        }

        if (!io.WantCaptureMouse && selectionRect.active)
        {
            selectionRect.endMouse = mousePos;

            if (selectionRect.viewIndex >= 0 && selectionRect.viewIndex < activeViews)
            {
                const EditorView &selView = views[(size_t)selectionRect.viewIndex];
                selectionRect.endWorld = snapPoint(
                    orthoPointFromScreen(selView, focus, mousePos),
                    settings.snapSize,
                    settings.snapEnabled);
            }

            if (Input::IsMouseReleased(MouseButton::LEFT))
            {
                const float dragPixels = glm::length(selectionRect.endMouse - selectionRect.startMouse);
                if (!selectionRect.additive)
                    selectedBrushes.clear();

                if (dragPixels < 4.0f)
                {
                    float pickDistance = 8.0f;
                    if (selectionRect.viewIndex >= 0 && selectionRect.viewIndex < activeViews)
                        pickDistance = glm::max(4.0f, views[(size_t)selectionRect.viewIndex].orthoSize * 0.05f);

                    const int picked = findBrushAtPoint(brushes, selectionRect.viewType, selectionRect.endWorld, pickDistance);
                    if (picked >= 0)
                    {
                        if (selectionRect.additive && selectionContains(selectedBrushes, picked))
                            selectionRemove(selectedBrushes, picked);
                        else
                            selectionAddUnique(selectedBrushes, picked);
                    }
                }
                else
                {
                    int addedCount = 0;
                    for (int i = 0; i < (int)brushes.size(); ++i)
                    {
                        if (brushes[i].hidden)
                            continue;
                        if (!brushIntersectsSelectionRect(brushes[i], selectionRect.viewType, selectionRect.startWorld, selectionRect.endWorld))
                            continue;
                        if (!selectionContains(selectedBrushes, i))
                        {
                            selectionAddUnique(selectedBrushes, i);
                            ++addedCount;
                        }
                    }
                    appendConsoleLine(console, "[select] marquee selected " + std::to_string(addedCount) + " brush(es)");
                }

                selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                if (selectedBrush >= 0)
                    selectedBrushFace = defaultFaceForView(selectionRect.viewType, selectedBrushFace);

                selectionRect.active = false;
            }
        }

        if (draggingSelection && Input::IsMouseReleased(MouseButton::LEFT))
        {
            draggingSelection = false;
            dragScaleAxis = BrushScaleAxis::None;
            if (selectedBrush >= 0)
                appendConsoleLine(console,
                                  std::string(dragTool == EditorTool::Scale ? "[scale] end brush " : "[move] end drag brush ") +
                                  std::to_string(selectedBrush));
        }

        ImDrawList *overlay = ImGui::GetBackgroundDrawList();
        for (int i = 0; i < activeViews; ++i)
        {
            const EditorView &view = views[i];
            overlay->AddRect(ImVec2((float)view.rect.x, (float)view.rect.y),
                             ImVec2((float)(view.rect.x + view.rect.w), (float)(view.rect.y + view.rect.h)),
                             IM_COL32(35, 35, 35, 255),
                             0.0f,
                             0,
                             1.0f);
            overlay->AddText(ImVec2((float)view.rect.x + 8.0f, (float)view.rect.y + 8.0f),
                             IM_COL32(235, 235, 235, 255),
                             view.label);
        }

        if (selectionRect.active)
        {
            const float minX = glm::min(selectionRect.startMouse.x, selectionRect.endMouse.x);
            const float minY = glm::min(selectionRect.startMouse.y, selectionRect.endMouse.y);
            const float maxX = glm::max(selectionRect.startMouse.x, selectionRect.endMouse.x);
            const float maxY = glm::max(selectionRect.startMouse.y, selectionRect.endMouse.y);
            overlay->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(90, 160, 255, 35));
            overlay->AddRect(ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(100, 180, 255, 220), 0.0f, 0, 1.5f);
        }

        if (!io.WantCaptureMouse &&
            hoveredViewPtr &&
            hoveredViewPtr->type != EditorViewType::Perspective &&
            ImGui::IsMouseClicked(1))
        {
            orthoPopupViewIndex = -1;
            for (int i = 0; i < activeViews; ++i)
            {
                if (&views[i] == hoveredViewPtr)
                {
                    orthoPopupViewIndex = i;
                    break;
                }
            }
            orthoPopupWorld = hoveredWorld;
            ImGui::OpenPopup("OrthoViewPopup");
        }

        if (ImGui::BeginPopup("OrthoViewPopup"))
        {
            if (orthoPopupViewIndex >= 0 && orthoPopupViewIndex < activeViews)
            {
                EditorView &popupView = views[(size_t)orthoPopupViewIndex];
                ImGui::Text("%s", popupView.label);
                ImGui::Separator();

                if (ImGui::MenuItem("Center Here"))
                {
                    centerFocusForView(focus, popupView.type, orthoPopupWorld);
                    appendConsoleLine(console, std::string("[view] centered ") + popupView.label + " at mouse");
                }

                if (ImGui::MenuItem("Center Origin"))
                {
                    centerFocusForView(focus, popupView.type, glm::vec3(0.0f));
                    appendConsoleLine(console, std::string("[view] centered ") + popupView.label + " at origin");
                }

                if (ImGui::MenuItem("Reset Zoom"))
                {
                    popupView.orthoSize = 256.0f;
                    appendConsoleLine(console, std::string("[view] reset zoom ") + popupView.label);
                }

                if (ImGui::MenuItem("Reset 2D View"))
                {
                    centerFocusForView(focus, popupView.type, glm::vec3(0.0f));
                    popupView.orthoSize = 256.0f;
                    appendConsoleLine(console, std::string("[view] reset 2D view ") + popupView.label);
                }

                if (!selectedBrushes.empty())
                {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clone Selected", "Ctrl+D"))
                    {
                        const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                        std::vector<int> newSelection = cloneSelectedBrushes(brushes, selectedBrushes, offset);
                        selectedBrushes = newSelection;
                        selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                        appendConsoleLine(console, "[edit] cloned selection from popup");
                    }
                    if (ImGui::MenuItem("Copy Selected", "Ctrl+C"))
                    {
                        brushClipboard = copySelectedBrushes(brushes, selectedBrushes);
                        appendConsoleLine(console, "[edit] copied " + std::to_string((int)brushClipboard.size()) + " brush(es)");
                    }
                    if (ImGui::MenuItem("Hide Selected", "H"))
                    {
                        const std::vector<int> source = buildSortedUniqueValidSelection(selectedBrushes, (int)brushes.size());
                        for (int index : source)
                            brushes[(size_t)index].hidden = true;
                        appendConsoleLine(console, "[edit] hid " + std::to_string((int)source.size()) + " brush(es)");
                    }
                    if (ImGui::MenuItem("Unhide Selected", "Shift+H"))
                    {
                        const std::vector<int> source = buildSortedUniqueValidSelection(selectedBrushes, (int)brushes.size());
                        for (int index : source)
                            brushes[(size_t)index].hidden = false;
                        appendConsoleLine(console, "[edit] unhid " + std::to_string((int)source.size()) + " brush(es)");
                    }
                }

                if (!brushClipboard.empty())
                {
                    if (ImGui::MenuItem("Paste", "Ctrl+V"))
                    {
                        const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                        std::vector<int> newSelection = pasteBrushClipboard(brushes, brushClipboard, offset);
                        selectedBrushes = newSelection;
                        selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                        appendConsoleLine(console, "[edit] pasted " + std::to_string((int)newSelection.size()) + " brush(es)");
                    }
                }
            }
            ImGui::EndPopup();
        }

        // Right-click popup for 3D view rendering mode
        {
            // Find the perspective view rect
            EditorView *perspectiveView = nullptr;
            for (int i = 0; i < activeViews; ++i)
            {
                if (views[i].type == EditorViewType::Perspective)
                {
                    perspectiveView = &views[i];
                    break;
                }
            }
            if (perspectiveView)
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                bool mouseOverPerspective = mousePos.x >= perspectiveView->rect.x &&
                                           mousePos.x <= perspectiveView->rect.x + perspectiveView->rect.w &&
                                           mousePos.y >= perspectiveView->rect.y &&
                                           mousePos.y <= perspectiveView->rect.y + perspectiveView->rect.h;
                if (mouseOverPerspective && ImGui::IsMouseClicked(1))
                {
                    ImGui::OpenPopup("ViewModePopup");
                }
            }

            if (ImGui::BeginPopup("ViewModePopup"))
            {
                if (ImGui::MenuItem("Solid", nullptr, settings.renderingMode == EditorRenderingMode::Solid))
                {
                    settings.renderingMode = EditorRenderingMode::Solid;
                    saveEditorSettings(settings);
                }
                if (ImGui::MenuItem("Wireframe", nullptr, settings.renderingMode == EditorRenderingMode::Wireframe))
                {
                    settings.renderingMode = EditorRenderingMode::Wireframe;
                    saveEditorSettings(settings);
                }
                if (ImGui::MenuItem("Textured", nullptr, settings.renderingMode == EditorRenderingMode::Textured))
                {
                    settings.renderingMode = EditorRenderingMode::Textured;
                    saveEditorSettings(settings);
                }
                ImGui::EndPopup();
            }
        }

        glViewport(0, 0, device.GetWidth(), device.GetHeight());
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (int i = 0; i < activeViews; ++i)
        {
            const EditorView &view = views[i];
            const glm::vec3 previewEnd =
                (hasHoveredView && hoveredView == view.type && view.type != EditorViewType::Perspective)
                    ? hoveredWorld
                    : glm::vec3(0.0f);

            renderEditorView(worldBatch,
                             view,
                             device.GetHeight(),
                             brushes,
                             selectedBrushes,
                             selectedBrush,
                             selectedBrushFace,
                             pendingBrush,
                             settings.showGrid,
                             settings.showAxes,
                             settings.gridStep,
                             settings.defaultBrushThickness,
                             settings.defaultBrushHeight,
                             focus,
                             previewEnd,
                             currentTexturePath,
                             settings.renderingMode);
        }

        drawScreenOverlay(overlayBatch,
                          device.GetWidth(),
                          device.GetHeight(),
                          views,
                          hoveredView,
                          hasHoveredView);

        device.ImGuiEnd();
        device.Flip();
    }

    // Save settings before exit
    settings.assetRoot = assetRoot;
    settings.currentTexturePath = currentTexturePath;
    settings.focus = focus;
    settings.assetViewAsGrid = assetViewAsGrid;
    
    // Save view settings
    for (int i = 0; i < 4; ++i)
    {
        settings.views[i].orthoSize = views[i].orthoSize;
        settings.views[i].perspectiveDistance = views[i].perspectiveDistance;
        settings.views[i].perspectiveYaw = views[i].perspectiveYaw;
        settings.views[i].perspectivePitch = views[i].perspectivePitch;
    }
    
    saveEditorSettings(settings);
    appendConsoleLine(console, "[editor] saved settings to editor_settings.json");

    worldBatch.Release();
    overlayBatch.Release();
    device.Close();
    return 0;
}
