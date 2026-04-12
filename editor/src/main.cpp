#include <algorithm>
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
#include <glm/gtc/constants.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "Batch.hpp"
#include "Camera.hpp"
#include "CSG.hpp"
#include "Device.hpp"
#include "EditorConvexBrushOps.hpp"
#include "EditorBrushGeometryOps.hpp"
#include "EditorBrushOps.hpp"
#include "EditorData.hpp"
#include "EditorSceneIO.hpp"
#include "EditorTheme.hpp"
#include "EditorViewOps.hpp"
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

// Tool Icons structure
struct ToolIcons
{
    Texture *select = nullptr;
    Texture *move = nullptr;
    Texture *scale = nullptr;
    Texture *rotate = nullptr;
    Texture *face = nullptr;
    Texture *brush = nullptr;

    bool loadIcons()
    {
        // Configure for ImGui: flip vertical for ImGui compatibility
        TextureManager::instance().setFlipVertical(true);

        select = TextureManager::instance().load("icon_select", resolveTexturePathForLoad("assets/res/select24.png"));
        move = TextureManager::instance().load("icon_move", resolveTexturePathForLoad("assets/res/move24.png"));
        scale = TextureManager::instance().load("icon_scale", resolveTexturePathForLoad("assets/res/scale24.png"));
        rotate = TextureManager::instance().load("icon_rotate", resolveTexturePathForLoad("assets/res/rotate24.png"));
        face = TextureManager::instance().load("icon_face", resolveTexturePathForLoad("assets/res/face24.png"));
        brush = TextureManager::instance().load("icon_brush", resolveTexturePathForLoad("assets/res/brush24.png"));

        TextureManager::instance().resetDefaults();
        return select && move && scale && rotate && face && brush;
    }

    Texture *getIcon(EditorTool tool) const
    {
        switch (tool)
        {
        case EditorTool::Select: return select;
        case EditorTool::Move: return move;
        case EditorTool::Scale: return scale;
        case EditorTool::Rotate: return rotate;
        case EditorTool::Face: return face;
        case EditorTool::Clip: return nullptr;
        case EditorTool::Brush: return brush;
        }
        return nullptr;
    }
};


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
    case EditorTool::Rotate: return "Rotate";
    case EditorTool::Face: return "Face";
    case EditorTool::Clip: return "Clip";
    case EditorTool::Brush: return "Create";
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
    case EditorTool::Rotate: return "4";
    case EditorTool::Face: return "5";
    case EditorTool::Clip: return "6";
    case EditorTool::Brush: return "7";
    }
    return "";
}

static bool drawToolIconButton(EditorTool tool,
                               EditorTool currentTool,
                               const ToolIcons &toolIcons)
{
    const bool active = (tool == currentTool);
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.40f, 0.80f, 1.0f));
    }

    Texture *icon = toolIcons.getIcon(tool);
    bool clicked = false;
    if (icon && icon->id != 0)
        clicked = ImGui::ImageButton(("##tool_icon_" + std::to_string((int)tool)).c_str(),
                                     (ImTextureID)(intptr_t)icon->id,
                                     ImVec2(18.0f, 18.0f));
    else
        clicked = ImGui::SmallButton(toolName(tool));

    if (active)
        ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s (%s)", toolName(tool), toolShortcut(tool));

    return clicked;
}

static void drawViewToolPalettes(const std::array<EditorView, 4> &views,
                                 int activeViews,
                                 EditorTool &currentTool,
                                 const ToolIcons &toolIcons)
{
    static const EditorTool kTools[] = {
        EditorTool::Select,
        EditorTool::Move,
        EditorTool::Scale,
        EditorTool::Rotate,
        EditorTool::Face,
        EditorTool::Clip,
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
                if (drawToolIconButton(kTools[t], currentTool, toolIcons))
                    currentTool = kTools[t];
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
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

static void appendConsoleLine(ImGuiConsole &console, const std::string &line)
{
    std::string text = console.GetText();
    if (!text.empty())
        text += "\n";
    text += line;
    console.SetText(text);
}

static std::string entityDisplayName(const EditorEntity &entity, int index)
{
    if (!entity.name.empty())
        return entity.name;
    if (!entity.classname.empty())
        return entity.classname + " " + std::to_string(index + 1);
    return "Entity " + std::to_string(index + 1);
}

static EditorEntity makePointEntity(const std::string &classname,
                                    const glm::vec3 &origin,
                                    int index)
{
    EditorEntity entity;
    entity.classname = classname;
    entity.name = classname + " " + std::to_string(index + 1);
    entity.origin = origin;

    if (classname == "light")
    {
        entity.keyvalues.push_back({"light", "300"});
        entity.keyvalues.push_back({"_color", "1 1 1"});
    }

    return entity;
}

static void ensureWorldspawnEntity(EditorEntity &entity)
{
    entity.classname = "worldspawn";
    if (entity.name.empty())
        entity.name = "World";
}

static std::vector<EditorEntity> buildSceneEntitiesForSave(const EditorEntity &worldspawnEntity,
                                                           const std::vector<EditorEntity> &sceneEntities)
{
    std::vector<EditorEntity> entities;
    entities.reserve(1 + sceneEntities.size());

    EditorEntity worldspawn = worldspawnEntity;
    ensureWorldspawnEntity(worldspawn);
    entities.push_back(worldspawn);
    entities.insert(entities.end(), sceneEntities.begin(), sceneEntities.end());
    return entities;
}

static void applyLoadedSceneEntities(const std::vector<EditorEntity> &loadedEntities,
                                     EditorEntity &worldspawnEntity,
                                     std::vector<EditorEntity> &sceneEntities)
{
    worldspawnEntity = {};
    ensureWorldspawnEntity(worldspawnEntity);
    sceneEntities.clear();

    for (const EditorEntity &entity : loadedEntities)
    {
        if (entity.isWorldspawn())
        {
            if (worldspawnEntity.brushes.empty() && worldspawnEntity.keyvalues.empty())
            {
                worldspawnEntity = entity;
                ensureWorldspawnEntity(worldspawnEntity);
            }
            else
            {
                worldspawnEntity.brushes.insert(worldspawnEntity.brushes.end(),
                                                entity.brushes.begin(),
                                                entity.brushes.end());
            }
            continue;
        }

        sceneEntities.push_back(entity);
    }

    ensureWorldspawnEntity(worldspawnEntity);
}

static int countConvexBrushesInScene(const EditorEntity &worldspawnEntity,
                                     const std::vector<EditorEntity> &sceneEntities)
{
    int total = (int)worldspawnEntity.convexBrushes.size();
    for (const EditorEntity &entity : sceneEntities)
        total += (int)entity.convexBrushes.size();
    return total;
}

static size_t extraConvexBrushStartIndex(const EditorEntity &entity)
{
    return std::min(entity.convexBrushes.size(), entity.brushes.size());
}

static size_t extraConvexBrushCount(const EditorEntity &entity)
{
    return entity.convexBrushes.size() - extraConvexBrushStartIndex(entity);
}

static void syncEntityConvexBrushes(EditorEntity &entity)
{
    const size_t legacyCount = entity.brushes.size();
    if (entity.convexBrushes.size() < legacyCount)
        entity.convexBrushes.reserve(legacyCount);

    for (size_t i = 0; i < legacyCount; ++i)
    {
        const EditorBrush convex = makeConvexBrushFromVolume(entity.brushes[i]);
        if (i < entity.convexBrushes.size())
            entity.convexBrushes[i] = convex;
        else
            entity.convexBrushes.push_back(convex);
    }
}

static void syncSceneConvexBrushes(EditorEntity &worldspawnEntity,
                                   std::vector<EditorEntity> &sceneEntities)
{
    syncEntityConvexBrushes(worldspawnEntity);
    for (EditorEntity &entity : sceneEntities)
        syncEntityConvexBrushes(entity);
}

static int extraConvexBrushActualIndex(const EditorEntity &entity, int extraIndex)
{
    if (extraIndex < 0)
        return -1;
    const size_t start = extraConvexBrushStartIndex(entity);
    const size_t actual = start + (size_t)extraIndex;
    if (actual >= entity.convexBrushes.size())
        return -1;
    return (int)actual;
}

static EditorBrush *getExtraConvexBrush(EditorEntity &entity, int extraIndex)
{
    const int actualIndex = extraConvexBrushActualIndex(entity, extraIndex);
    if (actualIndex < 0)
        return nullptr;
    return &entity.convexBrushes[(size_t)actualIndex];
}

static const char *convexBrushPrimitiveName(EditorBrushPrimitive primitive)
{
    switch (primitive)
    {
    case EditorBrushPrimitive::Box: return "Box";
    case EditorBrushPrimitive::Wedge: return "Ramp";
    case EditorBrushPrimitive::Cylinder: return "Cylinder";
    case EditorBrushPrimitive::Custom: break;
    }
    return "Brush";
}

static std::string convexBrushDisplayName(const EditorBrush &brush, int index)
{
    if (!brush.name.empty())
        return brush.name;
    return std::string(convexBrushPrimitiveName(brush.primitive)) + " " + std::to_string(index + 1);
}

static bool convexBrushBounds(const EditorBrush &brush,
                              glm::vec3 &outMins,
                              glm::vec3 &outMaxs)
{
    const std::vector<EditorConvexFacePolygon> polygons = buildConvexFacePolygons(brush);
    bool foundVertex = false;

    for (const EditorConvexFacePolygon &polygon : polygons)
    {
        for (const glm::vec3 &vertex : polygon.vertices)
        {
            if (!foundVertex)
            {
                outMins = vertex;
                outMaxs = vertex;
                foundVertex = true;
            }
            else
            {
                outMins = glm::min(outMins, vertex);
                outMaxs = glm::max(outMaxs, vertex);
            }
        }
    }

    return foundVertex;
}

static void translateConvexBrush(EditorBrush &brush, const glm::vec3 &delta)
{
    if (glm::length2(delta) <= 1e-8f)
        return;

    for (EditorBrushFace &face : brush.faces)
    {
        for (glm::vec3 &point : face.planePoints)
            point += delta;
    }
    brush.dirty = true;
}

static bool pointInScreenPolygon(const std::vector<glm::vec2> &polygon, const glm::vec2 &point)
{
    if (polygon.size() < 3)
        return false;

    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const glm::vec2 &a = polygon[i];
        const glm::vec2 &b = polygon[j];
        const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
                             (point.x < (b.x - a.x) * (point.y - a.y) / ((b.y - a.y) + 1e-6f) + a.x);
        if (crosses)
            inside = !inside;
    }
    return inside;
}

struct PendingClip
{
    bool active = false;
    EditorViewType view = EditorViewType::Top;
    glm::vec3 start = glm::vec3(0.0f);
};

static glm::vec3 convexFaceNormal(const EditorBrushFace &face)
{
    const glm::vec3 normal = glm::cross(face.planePoints[1] - face.planePoints[0],
                                        face.planePoints[2] - face.planePoints[0]);
    if (glm::length2(normal) <= 1e-8f)
        return glm::vec3(0.0f);
    return glm::normalize(normal);
}

static bool buildClipPlaneFromView(EditorViewType view,
                                   const glm::vec3 &start,
                                   const glm::vec3 &end,
                                   glm::vec3 &planePoint,
                                   glm::vec3 &planeNormal)
{
    glm::vec3 lineDir(0.0f);
    glm::vec3 viewAxis(0.0f);

    switch (view)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        lineDir = glm::vec3(end.x - start.x, 0.0f, end.z - start.z);
        viewAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case EditorViewType::Front:
    case EditorViewType::Back:
        lineDir = glm::vec3(end.x - start.x, end.y - start.y, 0.0f);
        viewAxis = glm::vec3(0.0f, 0.0f, 1.0f);
        break;
    case EditorViewType::Left:
    case EditorViewType::Right:
        lineDir = glm::vec3(0.0f, end.y - start.y, end.z - start.z);
        viewAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        break;
    case EditorViewType::Perspective:
        return false;
    }

    if (glm::length2(lineDir) <= 1e-8f)
        return false;

    planePoint = start;
    planeNormal = glm::cross(viewAxis, glm::normalize(lineDir));
    return glm::length2(planeNormal) > 1e-8f;
}

static bool findConvexFaceAtScreenPos(const EditorBrush &brush,
                                      const EditorView &view,
                                      const glm::vec2 &mousePos,
                                      int &outFaceIndex)
{
    outFaceIndex = -1;
    float bestDepth = 1e30f;
    float bestDistance2 = 1e30f;

    for (const EditorConvexFacePolygon &polygon : buildConvexFacePolygons(brush))
    {
        if (polygon.vertices.size() < 3)
            continue;

        std::vector<glm::vec2> screenPolygon;
        screenPolygon.reserve(polygon.vertices.size());
        float depthSum = 0.0f;
        bool validProjection = true;
        for (const glm::vec3 &vertex : polygon.vertices)
        {
            const glm::vec4 clip = view.camera.viewProjection * glm::vec4(vertex, 1.0f);
            if (std::fabs(clip.w) <= 1e-6f)
            {
                validProjection = false;
                break;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (view.type == EditorViewType::Perspective && (ndc.z < -1.0f || ndc.z > 1.0f))
            {
                validProjection = false;
                break;
            }

            screenPolygon.push_back(glm::vec2(
                (float)view.rect.x + ((ndc.x * 0.5f) + 0.5f) * (float)view.rect.w,
                (float)view.rect.y + (1.0f - ((ndc.y * 0.5f) + 0.5f)) * (float)view.rect.h));
            depthSum += ndc.z;
        }

        if (!validProjection || !pointInScreenPolygon(screenPolygon, mousePos))
            continue;

        glm::vec2 center(0.0f);
        for (const glm::vec2 &screenPoint : screenPolygon)
            center += screenPoint;
        center /= (float)screenPolygon.size();

        const float depth = depthSum / (float)screenPolygon.size();
        const float distance2 = glm::length2(mousePos - center);
        if (view.type == EditorViewType::Perspective)
        {
            if (depth < bestDepth - 1e-4f ||
                (std::fabs(depth - bestDepth) <= 1e-4f && distance2 < bestDistance2))
            {
                bestDepth = depth;
                bestDistance2 = distance2;
                outFaceIndex = polygon.faceIndex;
            }
        }
        else if (distance2 < bestDistance2)
        {
            bestDistance2 = distance2;
            outFaceIndex = polygon.faceIndex;
        }
    }

    return outFaceIndex >= 0;
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
                                bool textureLock,
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
    const glm::vec3 samplePoint = textureLock ? (p - face.p0) : p;

    return glm::vec2(
        glm::dot(samplePoint, uRot) / (safeTexW * safeScaleU) + combinedOffset.x,
        glm::dot(samplePoint, vRot) / (safeTexH * safeScaleV) + combinedOffset.y);
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

static void drawBrushTextured(RenderBatch &batch, const BrushVolume &brush, bool textureLock)
{
    const auto faces = buildBrushFaces(brush);
    for (const BrushFaceGeometry &face : faces)
    {
        const std::string texturePath = resolveBrushFaceTexturePath(brush, face.faceIndex);
        Texture *texture = loadEditorTexture(texturePath);
        batch.SetTexture(texture ? texture->id : 0u);
        const float texW = (texture && texture->width > 0) ? (float)texture->width : 1.0f;
        const float texH = (texture && texture->height > 0) ? (float)texture->height : 1.0f;

        const glm::vec2 uv0 = computeBrushUV(brush, face, face.p0, textureLock, texW, texH);
        const glm::vec2 uv1 = computeBrushUV(brush, face, face.p1, textureLock, texW, texH);
        const glm::vec2 uv2 = computeBrushUV(brush, face, face.p2, textureLock, texW, texH);
        const glm::vec2 uv3 = computeBrushUV(brush, face, face.p3, textureLock, texW, texH);

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
                        bool textureLock,
                        EditorRenderingMode renderingMode,
                        bool enableTransparency = false,
                        float transparency = 1.0f)
{
    // Apply transparency only in 3D view
        const bool useTransparency = enableTransparency && (view.type == EditorViewType::Perspective);
        const u8 alphaValue = useTransparency ? (u8)glm::clamp(transparency * 255.0f, 0.0f, 255.0f) : 255;

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
            else if (!brushVisibleInOrthoView(brush, view, view.focus))
            {
                continue;
            }
        }

        if (renderingMode == EditorRenderingMode::Wireframe)
        {
            if (isSelected)
                batch.SetColor(255, 170, 70, alphaValue);
            else
                batch.SetColor((u8)glm::clamp(brush.color.x * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.y * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.z * 255.0f, 0.0f, 255.0f),
                               alphaValue);
            drawBrushWireframe(batch, brush);
        }
        else if (renderingMode == EditorRenderingMode::Textured)
        {
            batch.SetColor(255, 255, 255, alphaValue);
            drawBrushTextured(batch, brush, textureLock);
            if (isSelected)
            {
                batch.SetColor(255, 180, 70, alphaValue);
                drawBrushWireframe(batch, brush);
            }
        }
        else
        {
            if (isSelected)
                batch.SetColor(255, 180, 70, alphaValue);
            else
                batch.SetColor((u8)glm::clamp(brush.color.x * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.y * 255.0f, 0.0f, 255.0f),
                               (u8)glm::clamp(brush.color.z * 255.0f, 0.0f, 255.0f),
                               alphaValue);
            drawBrushSolid(batch, brush);
            if (isSelected)
            {
                batch.SetColor(255, 185, 80, alphaValue);
                drawBrushWireframe(batch, brush);
            }
        }

        if (isSelected && i == primarySelectedBrush)
        {
            batch.SetColor(255, 235, 80, alphaValue);
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
                view.focus,
                currentTexturePath);
        batch.SetColor(255, 210, 80, alphaValue);
        if (renderingMode == EditorRenderingMode::Wireframe)
        {
            drawBrushWireframe(batch, preview);
        }
        else if (renderingMode == EditorRenderingMode::Textured)
        {
            batch.SetColor(255, 255, 255, alphaValue);
            drawBrushTextured(batch, preview, textureLock);
            batch.SetColor(255, 210, 80, alphaValue);
            drawBrushWireframe(batch, preview);
        }
        else
        {
            drawBrushSolid(batch, preview);
            batch.SetColor(255, 210, 80, alphaValue);
            drawBrushWireframe(batch, preview);
        }
    }
}

static bool entityVisibleInOrthoView(const EditorEntity &entity,
                                     const EditorView &view,
                                     const glm::vec3 &focus)
{
    const float aspect = (view.rect.h > 0) ? ((float)view.rect.w / (float)view.rect.h) : 1.0f;
    const float halfH = view.orthoSize;
    const float halfW = halfH * aspect;

    switch (view.type)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        return entity.origin.x >= focus.x - halfW && entity.origin.x <= focus.x + halfW &&
               entity.origin.z >= focus.z - halfH && entity.origin.z <= focus.z + halfH;
    case EditorViewType::Front:
    case EditorViewType::Back:
        return entity.origin.x >= focus.x - halfW && entity.origin.x <= focus.x + halfW &&
               entity.origin.y >= focus.y - halfH && entity.origin.y <= focus.y + halfH;
    case EditorViewType::Left:
    case EditorViewType::Right:
        return entity.origin.z >= focus.z - halfW && entity.origin.z <= focus.z + halfW &&
               entity.origin.y >= focus.y - halfH && entity.origin.y <= focus.y + halfH;
    case EditorViewType::Perspective:
        break;
    }
    return true;
}

static void drawEntityMarker(RenderBatch &batch,
                             const EditorEntity &entity,
                             bool selected,
                             float size,
                             u8 alpha)
{
    const glm::vec3 p = entity.origin;
    if (selected)
        batch.SetColor(255, 235, 90, alpha);
    else if (entity.classname == "light")
        batch.SetColor(255, 230, 120, alpha);
    else if (entity.classname == "info_player_start")
        batch.SetColor(120, 220, 120, alpha);
    else
        batch.SetColor(180, 200, 255, alpha);

    batch.Line3D(p + glm::vec3(-size, 0.0f, 0.0f), p + glm::vec3(size, 0.0f, 0.0f));
    batch.Line3D(p + glm::vec3(0.0f, -size, 0.0f), p + glm::vec3(0.0f, size, 0.0f));
    batch.Line3D(p + glm::vec3(0.0f, 0.0f, -size), p + glm::vec3(0.0f, 0.0f, size));
}

static void drawEntities(RenderBatch &batch,
                         const EditorView &view,
                         const std::vector<EditorEntity> &entities,
                         int selectedEntity,
                         bool enableTransparency,
                         float transparency)
{
    const bool useTransparency = enableTransparency && (view.type == EditorViewType::Perspective);
    const u8 alphaValue = useTransparency ? (u8)glm::clamp(transparency * 255.0f, 0.0f, 255.0f) : 255;
    const float markerSize = (view.type == EditorViewType::Perspective)
        ? 18.0f
        : glm::max(view.orthoSize * 0.05f, 8.0f);

    for (int i = 0; i < (int)entities.size(); ++i)
    {
        const EditorEntity &entity = entities[i];
        if (entity.hidden || entity.isWorldspawn())
            continue;
        if (view.type != EditorViewType::Perspective && !entityVisibleInOrthoView(entity, view, view.focus))
            continue;
        drawEntityMarker(batch, entity, i == selectedEntity, markerSize, alphaValue);
    }
}

static glm::vec2 computeConvexFaceUv(const EditorBrushFace &face,
                                     const glm::vec3 &p,
                                     bool textureLock,
                                     float texWidth,
                                     float texHeight)
{
    glm::vec3 normal = glm::cross(face.planePoints[1] - face.planePoints[0],
                                  face.planePoints[2] - face.planePoints[0]);
    if (glm::length2(normal) <= 1e-8f)
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
    else
        normal = glm::normalize(normal);

    glm::vec3 tangent = face.planePoints[1] - face.planePoints[0];
    if (glm::length2(tangent) <= 1e-8f)
        tangent = glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length2(tangent) <= 1e-8f)
        tangent = glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f));
    tangent = glm::normalize(tangent);
    const glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

    const float angle = glm::radians(face.uvRotation);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const glm::vec3 uAxis = tangent * c - bitangent * s;
    const glm::vec3 vAxis = tangent * s + bitangent * c;

    const float safeTexW = glm::max(texWidth, 1.0f);
    const float safeTexH = glm::max(texHeight, 1.0f);
    const float safeScaleU = std::fabs(face.uvScale.x) > 1e-6f ? face.uvScale.x : 1.0f;
    const float safeScaleV = std::fabs(face.uvScale.y) > 1e-6f ? face.uvScale.y : 1.0f;
    const glm::vec3 localPoint = textureLock ? (p - face.planePoints[0]) : p;

    return glm::vec2(
        glm::dot(localPoint, uAxis) / (safeTexW * safeScaleU) + face.uvOffset.x,
        glm::dot(localPoint, vAxis) / (safeTexH * safeScaleV) + face.uvOffset.y);
}

static void drawConvexBrushWireframe(RenderBatch &batch, const EditorBrush &brush)
{
    const std::vector<EditorConvexFacePolygon> polygons = buildConvexFacePolygons(brush);
    for (const EditorConvexFacePolygon &polygon : polygons)
    {
        if (polygon.vertices.size() < 2)
            continue;
        for (size_t i = 0; i < polygon.vertices.size(); ++i)
        {
            const glm::vec3 &a = polygon.vertices[i];
            const glm::vec3 &b = polygon.vertices[(i + 1) % polygon.vertices.size()];
            batch.Line3D(a, b);
        }
    }
}

static void drawConvexBrushSolid(RenderBatch &batch, const EditorBrush &brush)
{
    const std::vector<EditorConvexFacePolygon> polygons = buildConvexFacePolygons(brush);
    for (const EditorConvexFacePolygon &polygon : polygons)
    {
        if (polygon.vertices.size() < 3)
            continue;
        for (size_t i = 1; i + 1 < polygon.vertices.size(); ++i)
            batch.Triangle(polygon.vertices[0], polygon.vertices[i], polygon.vertices[i + 1]);
    }
}

static void drawConvexBrushTextured(RenderBatch &batch, const EditorBrush &brush, bool textureLock)
{
    const std::vector<EditorConvexFacePolygon> polygons = buildConvexFacePolygons(brush);
    for (const EditorConvexFacePolygon &polygon : polygons)
    {
        if (polygon.faceIndex < 0 || polygon.faceIndex >= (int)brush.faces.size() || polygon.vertices.size() < 3)
            continue;

        const EditorBrushFace &face = brush.faces[(size_t)polygon.faceIndex];
        Texture *texture = face.texturePath.empty() ? TextureManager::instance().getWhite()
                                                    : loadEditorTexture(face.texturePath);
        batch.SetTexture(texture ? texture->id : 0u);
        const float texW = (texture && texture->width > 0) ? (float)texture->width : 1.0f;
        const float texH = (texture && texture->height > 0) ? (float)texture->height : 1.0f;

        for (size_t i = 1; i + 1 < polygon.vertices.size(); ++i)
        {
            const glm::vec3 &p0 = polygon.vertices[0];
            const glm::vec3 &p1 = polygon.vertices[i];
            const glm::vec3 &p2 = polygon.vertices[i + 1];
            batch.Triangle(p0,
                           p1,
                           p2,
                           computeConvexFaceUv(face, p0, textureLock, texW, texH),
                           computeConvexFaceUv(face, p1, textureLock, texW, texH),
                           computeConvexFaceUv(face, p2, textureLock, texW, texH));
        }
    }
}

static void drawExtraConvexBrushes(RenderBatch &batch,
                                   const EditorView &view,
                                   const EditorEntity &worldspawnEntity,
                                   const std::vector<EditorEntity> &entities,
                                   int selectedConvexBrush,
                                   int selectedConvexFace,
                                   bool textureLock,
                                   EditorRenderingMode renderingMode,
                                   bool enableTransparency,
                                   float transparency)
{
    const bool useTransparency = enableTransparency && (view.type == EditorViewType::Perspective);
    const u8 alphaValue = useTransparency ? (u8)glm::clamp(transparency * 255.0f, 0.0f, 255.0f) : 255;

    auto drawEntityConvex = [&](const EditorEntity &entity)
    {
        const size_t start = extraConvexBrushStartIndex(entity);
        for (size_t i = start; i < entity.convexBrushes.size(); ++i)
        {
            const EditorBrush &brush = entity.convexBrushes[i];
            if (brush.hidden)
                continue;
            const bool isSelected = entity.isWorldspawn() && ((int)(i - start) == selectedConvexBrush);

            if (renderingMode == EditorRenderingMode::Wireframe)
            {
                if (isSelected)
                    batch.SetColor(255, 170, 70, alphaValue);
                else
                    batch.SetColor((u8)glm::clamp(brush.color.x * 255.0f, 0.0f, 255.0f),
                                   (u8)glm::clamp(brush.color.y * 255.0f, 0.0f, 255.0f),
                                   (u8)glm::clamp(brush.color.z * 255.0f, 0.0f, 255.0f),
                                   alphaValue);
                drawConvexBrushWireframe(batch, brush);
            }
            else if (renderingMode == EditorRenderingMode::Textured)
            {
                batch.SetColor(255, 255, 255, alphaValue);
                drawConvexBrushTextured(batch, brush, textureLock);
                if (isSelected)
                {
                    batch.SetColor(255, 190, 90, alphaValue);
                    drawConvexBrushWireframe(batch, brush);
                }
            }
            else
            {
                if (isSelected)
                    batch.SetColor(255, 180, 70, alphaValue);
                else
                    batch.SetColor((u8)glm::clamp(brush.color.x * 255.0f, 0.0f, 255.0f),
                                   (u8)glm::clamp(brush.color.y * 255.0f, 0.0f, 255.0f),
                                   (u8)glm::clamp(brush.color.z * 255.0f, 0.0f, 255.0f),
                                   alphaValue);
                drawConvexBrushSolid(batch, brush);
                if (isSelected)
                {
                    batch.SetColor(255, 190, 90, alphaValue);
                    drawConvexBrushWireframe(batch, brush);
                }
            }

            if (isSelected && selectedConvexFace >= 0)
            {
                const std::vector<EditorConvexFacePolygon> polygons = buildConvexFacePolygons(brush);
                batch.SetColor(255, 235, 90, 255);
                for (const EditorConvexFacePolygon &polygon : polygons)
                {
                    if (polygon.faceIndex != selectedConvexFace || polygon.vertices.size() < 2)
                        continue;
                    for (size_t edgeIndex = 0; edgeIndex < polygon.vertices.size(); ++edgeIndex)
                    {
                        const glm::vec3 &a = polygon.vertices[edgeIndex];
                        const glm::vec3 &b = polygon.vertices[(edgeIndex + 1) % polygon.vertices.size()];
                        batch.Line3D(a, b);
                    }
                }
            }
        }
    };

    drawEntityConvex(worldspawnEntity);
    for (const EditorEntity &entity : entities)
        drawEntityConvex(entity);
}

static int findExtraConvexBrushAtScreenPos(const EditorEntity &entity,
                                           const EditorView &view,
                                           const glm::vec2 &mousePos)
{
    int bestIndex = -1;
    float bestDepth = 1e30f;
    float bestDistance2 = 1e30f;

    const size_t start = extraConvexBrushStartIndex(entity);
    for (size_t actualIndex = start; actualIndex < entity.convexBrushes.size(); ++actualIndex)
    {
        const EditorBrush &brush = entity.convexBrushes[actualIndex];
        if (brush.hidden)
            continue;

        const std::vector<EditorConvexFacePolygon> polygons = buildConvexFacePolygons(brush);
        bool hitBrush = false;
        float brushDepth = 1e30f;
        float brushDistance2 = 1e30f;

        for (const EditorConvexFacePolygon &polygon : polygons)
        {
            if (polygon.vertices.size() < 3)
                continue;

            std::vector<glm::vec2> screenPolygon;
            screenPolygon.reserve(polygon.vertices.size());

            float depthSum = 0.0f;
            bool validProjection = true;
            for (const glm::vec3 &vertex : polygon.vertices)
            {
                const glm::vec4 clip = view.camera.viewProjection * glm::vec4(vertex, 1.0f);
                if (std::fabs(clip.w) <= 1e-6f)
                {
                    validProjection = false;
                    break;
                }

                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (view.type == EditorViewType::Perspective && (ndc.z < -1.0f || ndc.z > 1.0f))
                {
                    validProjection = false;
                    break;
                }

                screenPolygon.push_back(glm::vec2(
                    (float)view.rect.x + ((ndc.x * 0.5f) + 0.5f) * (float)view.rect.w,
                    (float)view.rect.y + (1.0f - ((ndc.y * 0.5f) + 0.5f)) * (float)view.rect.h));
                depthSum += ndc.z;
            }

            if (!validProjection || !pointInScreenPolygon(screenPolygon, mousePos))
                continue;

            glm::vec2 center(0.0f);
            for (const glm::vec2 &screenPoint : screenPolygon)
                center += screenPoint;
            center /= (float)screenPolygon.size();

            hitBrush = true;
            brushDepth = glm::min(brushDepth, depthSum / (float)screenPolygon.size());
            brushDistance2 = glm::min(brushDistance2, glm::length2(mousePos - center));
        }

        if (!hitBrush)
            continue;

        const int extraIndex = (int)(actualIndex - start);
        if (view.type == EditorViewType::Perspective)
        {
            if (brushDepth < bestDepth - 1e-4f ||
                (std::fabs(brushDepth - bestDepth) <= 1e-4f && brushDistance2 < bestDistance2))
            {
                bestDepth = brushDepth;
                bestDistance2 = brushDistance2;
                bestIndex = extraIndex;
            }
        }
        else if (brushDistance2 < bestDistance2)
        {
            bestDistance2 = brushDistance2;
            bestIndex = extraIndex;
        }
    }

    return bestIndex;
}

static bool projectWorldToViewScreen(const EditorView &view,
                                     const glm::vec3 &world,
                                     glm::vec2 &outScreen)
{
    const glm::vec4 clip = view.camera.viewProjection * glm::vec4(world, 1.0f);
    if (std::fabs(clip.w) <= 1e-6f)
        return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (view.type == EditorViewType::Perspective)
    {
        if (ndc.z < -1.0f || ndc.z > 1.0f)
            return false;
    }

    outScreen.x = (float)view.rect.x + ((ndc.x * 0.5f) + 0.5f) * (float)view.rect.w;
    outScreen.y = (float)view.rect.y + (1.0f - ((ndc.y * 0.5f) + 0.5f)) * (float)view.rect.h;
    return true;
}

static ImU32 entityMarkerColor(const EditorEntity &entity, bool selected)
{
    if (selected)
        return IM_COL32(255, 235, 90, 255);
    if (entity.classname == "light")
        return IM_COL32(255, 212, 90, 255);
    if (entity.classname == "info_player_start")
        return IM_COL32(110, 230, 140, 255);
    return IM_COL32(120, 180, 255, 255);
}

static void drawEntityOverlayGlyph(ImDrawList *drawList,
                                   const glm::vec2 &center,
                                   const EditorEntity &entity,
                                   bool selected)
{
    const ImU32 color = entityMarkerColor(entity, selected);
    const ImU32 outline = IM_COL32(20, 24, 28, 240);

    if (entity.classname == "light")
    {
        drawList->AddCircleFilled(ImVec2(center.x, center.y), 5.0f, color, 16);
        drawList->AddCircle(ImVec2(center.x, center.y), 8.0f, outline, 16, 3.0f);
        drawList->AddCircle(ImVec2(center.x, center.y), 8.0f, color, 16, 1.5f);
        for (int i = 0; i < 8; ++i)
        {
            const float angle = glm::two_pi<float>() * ((float)i / 8.0f);
            const glm::vec2 dir(std::cos(angle), std::sin(angle));
            const glm::vec2 a = center + dir * 10.0f;
            const glm::vec2 b = center + dir * 15.0f;
            drawList->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, selected ? 2.5f : 1.5f);
        }
        return;
    }

    if (entity.classname == "info_player_start")
    {
        const ImVec2 top(center.x, center.y - 9.0f);
        const ImVec2 left(center.x - 8.0f, center.y + 6.0f);
        const ImVec2 right(center.x + 8.0f, center.y + 6.0f);
        drawList->AddTriangleFilled(top, left, right, color);
        drawList->AddTriangle(top, left, right, outline, 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y + 6.0f),
                          ImVec2(center.x, center.y + 14.0f),
                          color,
                          selected ? 2.5f : 1.5f);
        return;
    }

    const ImVec2 p0(center.x, center.y - 8.0f);
    const ImVec2 p1(center.x + 8.0f, center.y);
    const ImVec2 p2(center.x, center.y + 8.0f);
    const ImVec2 p3(center.x - 8.0f, center.y);
    drawList->AddQuadFilled(p0, p1, p2, p3, color);
    drawList->AddQuad(p0, p1, p2, p3, outline, 2.0f);
}

static void drawEntityOverlayLabels(ImDrawList *drawList,
                                    const std::array<EditorView, 4> &views,
                                    int activeViews,
                                    const std::vector<EditorEntity> &entities,
                                    int selectedEntity)
{
    for (int viewIndex = 0; viewIndex < activeViews; ++viewIndex)
    {
        const EditorView &view = views[(size_t)viewIndex];
        for (int i = 0; i < (int)entities.size(); ++i)
        {
            const EditorEntity &entity = entities[(size_t)i];
            if (entity.hidden || entity.isWorldspawn())
                continue;
            if (view.type != EditorViewType::Perspective && !entityVisibleInOrthoView(entity, view, view.focus))
                continue;

            glm::vec2 screenPos(0.0f);
            if (!projectWorldToViewScreen(view, entity.origin, screenPos))
                continue;

            const bool selected = (i == selectedEntity);
            drawEntityOverlayGlyph(drawList, screenPos, entity, selected);

            const std::string label = entityDisplayName(entity, i);
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 textPos(screenPos.x + 12.0f, screenPos.y - textSize.y * 0.5f);
            const ImVec2 bgMin(textPos.x - 4.0f, textPos.y - 2.0f);
            const ImVec2 bgMax(textPos.x + textSize.x + 4.0f, textPos.y + textSize.y + 2.0f);
            drawList->AddRectFilled(bgMin,
                                    bgMax,
                                    selected ? IM_COL32(35, 40, 28, 220) : IM_COL32(20, 24, 28, 210),
                                    4.0f);
            drawList->AddRect(bgMin,
                              bgMax,
                              entityMarkerColor(entity, selected),
                              4.0f,
                              0,
                              selected ? 1.5f : 1.0f);
            drawList->AddText(textPos, IM_COL32(245, 245, 245, 255), label.c_str());
        }
    }
}

static int findEntityAtScreenPos(const std::vector<EditorEntity> &entities,
                                 const EditorView &view,
                                 const glm::vec2 &mousePos,
                                 float maxPixelDistance)
{
    int bestIndex = -1;
    float bestDistance2 = maxPixelDistance * maxPixelDistance;

    for (int i = 0; i < (int)entities.size(); ++i)
    {
        const EditorEntity &entity = entities[(size_t)i];
        if (entity.hidden || entity.isWorldspawn())
            continue;
        if (view.type != EditorViewType::Perspective && !entityVisibleInOrthoView(entity, view, view.focus))
            continue;

        glm::vec2 screenPos(0.0f);
        if (!projectWorldToViewScreen(view, entity.origin, screenPos))
            continue;

        const glm::vec2 delta = screenPos - mousePos;
        const float distance2 = glm::dot(delta, delta);
        if (distance2 <= bestDistance2)
        {
            bestDistance2 = distance2;
            bestIndex = i;
        }
    }

    return bestIndex;
}

static glm::vec3 entityPerspectiveDragDelta(const EditorView &view, const glm::vec2 &mouseDelta)
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

    const float dragScale = glm::max(view.perspectiveDistance * 0.0015f, 0.01f);
    return (right * mouseDelta.x - up * mouseDelta.y) * dragScale;
}

static void renderEditorView(RenderBatch &batch,
                             const EditorView &view,
                             int screenHeight,
                             const std::vector<BrushVolume> &brushes,
                             const EditorEntity &worldspawnEntity,
                             const std::vector<EditorEntity> &entities,
                             const std::vector<int> &selectedBrushes,
                             int primarySelectedBrush,
                             int selectedConvexBrush,
                             int selectedConvexFace,
                             int selectedEntity,
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
                             bool textureLock,
                             EditorRenderingMode renderingMode,
                             bool enableTransparency = false,
                             float transparency = 1.0f)
{
    const glm::vec3 viewFocus = (view.type == EditorViewType::Perspective) ? focus : view.focus;
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

    // Enable transparency only for 3D view
    bool useTransparency = enableTransparency && (view.type == EditorViewType::Perspective);
    if (useTransparency)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    Material::applyDefaultStates();
    batch.SetMatrix(view.camera.viewProjection);

    if (showGrid)
    {
        const float extent = (view.type == EditorViewType::Perspective)
                                 ? glm::max(512.0f, view.perspectiveDistance)
                                 : glm::max(view.orthoSize * 1.5f, 128.0f);
        drawGridForView(batch, view.type, viewFocus, extent, gridStep);
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
                viewFocus,
                currentTexturePath,
                textureLock,
                effectiveMode,
                enableTransparency,
                transparency);

    drawExtraConvexBrushes(batch,
                           view,
                           worldspawnEntity,
                           entities,
                           selectedConvexBrush,
                           selectedConvexFace,
                           textureLock,
                           effectiveMode,
                           enableTransparency,
                           transparency);
    drawEntities(batch, view, entities, selectedEntity, enableTransparency, transparency);

    batch.Render();

    if (enableTransparency && (view.type == EditorViewType::Perspective))
        glDisable(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, 0, 0);
}

static void drawScreenOverlay(RenderBatch &batch,
                              int screenWidth,
                              int screenHeight,
                              const std::array<EditorView, 4> &views,
                              int activeViews,
                              const std::vector<EditorEntity> &entities,
                              int selectedEntity,
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

    drawEntityOverlayLabels(ImGui::GetBackgroundDrawList(), views, activeViews, entities, selectedEntity);
}

static void pushUndo(std::vector<std::vector<BrushVolume>>& undoStack,
                     const std::vector<BrushVolume>&        scene,
                     std::vector<std::vector<BrushVolume>>& redoStack,
                     int                                     maxUndo = 32)
{
    undoStack.push_back(scene);
    redoStack.clear();
    if ((int)undoStack.size() > maxUndo)
        undoStack.erase(undoStack.begin());
}

static bool popUndo(std::vector<std::vector<BrushVolume>>& undoStack,
                    std::vector<BrushVolume>&               scene,
                    std::vector<std::vector<BrushVolume>>& redoStack)
{
    if (undoStack.empty())
        return false;
    redoStack.push_back(scene);
    scene = undoStack.back();
    undoStack.pop_back();
    return true;
}

static bool popRedo(std::vector<std::vector<BrushVolume>>& redoStack,
                    std::vector<BrushVolume>&               scene,
                    std::vector<std::vector<BrushVolume>>& undoStack)
{
    if (redoStack.empty())
        return false;
    undoStack.push_back(scene);
    scene = redoStack.back();
    redoStack.pop_back();
    return true;
}

static void drawCSGPanel(std::vector<BrushVolume>&              brushes,
                         int&                                    selectedBrush,
                         std::vector<int>&                       selectedBrushes,
                         CSGOperation&                           csgOp,
                         float&                                  hollowThickness,
                         float&                                  splitPosition,
                         int&                                    splitAxis,
                         std::vector<std::vector<BrushVolume>>& undoStack,
                         std::vector<std::vector<BrushVolume>>& redoStack)
{
    ImGui::Spacing();

    const char* opNames[] = { "Add", "Subtract", "Intersect", "Hollow" };
    int opIdx = (int)csgOp;
    if (ImGui::Combo("Operation##csg", &opIdx, opNames, 4))
        csgOp = (CSGOperation)opIdx;

    ImGui::Spacing();

    if (csgOp == CSGOperation::Hollow)
    {
        ImGui::DragFloat("Wall Thickness", &hollowThickness, 1.0f, 1.0f, 128.0f, "%.1f");
        ImGui::TextDisabled("Cria paredes ocas a partir do brush seleccionado");
    }

    bool hasSel = selectedBrush >= 0 && selectedBrush < (int)brushes.size();

    if (csgOp == CSGOperation::Hollow)
    {
        ImGui::BeginDisabled(!hasSel);
        if (ImGui::Button("Apply Hollow##csg", ImVec2(-1, 0)))
        {
            pushUndo(undoStack, brushes, redoStack);
            BrushVolume tool = brushes[selectedBrush];
            brushes.erase(brushes.begin() + selectedBrush);
            auto walls = CSG::hollow(tool, hollowThickness);
            for (auto& w : walls) brushes.push_back(w);
            selectedBrush = -1;
            selectedBrushes.clear();
            hasSel = false;
        }
        ImGui::EndDisabled();
    }

    if (csgOp == CSGOperation::Subtract)
    {
        ImGui::BeginDisabled(!hasSel);
        if (ImGui::Button("Subtract Selected from Scene##csg", ImVec2(-1, 0)))
        {
            pushUndo(undoStack, brushes, redoStack);
            BrushVolume tool = brushes[selectedBrush];
            brushes.erase(brushes.begin() + selectedBrush);
            selectedBrush = -1;
            selectedBrushes.clear();
            hasSel = false;
            CSG::applyToScene(brushes, tool, CSGOperation::Subtract);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Selecciona o brush 'ferramenta' e clica para\ncortar todos os outros com ele.");
    }

    if (csgOp == CSGOperation::Intersect)
    {
        ImGui::BeginDisabled(!hasSel);
        if (ImGui::Button("Intersect Selected with Scene##csg", ImVec2(-1, 0)))
        {
            pushUndo(undoStack, brushes, redoStack);
            BrushVolume tool = brushes[selectedBrush];
            brushes.erase(brushes.begin() + selectedBrush);
            selectedBrush = -1;
            selectedBrushes.clear();
            hasSel = false;
            CSG::applyToScene(brushes, tool, CSGOperation::Intersect);
        }
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::Text("Split");
    ImGui::Spacing();

    const char* axisNames[] = { "X", "Y", "Z" };
    ImGui::Combo("Axis##split", &splitAxis, axisNames, 3);

    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button("Split Middle##split", ImVec2(-1, 0)))
    {
        pushUndo(undoStack, brushes, redoStack);
        BrushVolume original = brushes[selectedBrush];
        brushes.erase(brushes.begin() + selectedBrush);
        selectedBrush = -1;
        selectedBrushes.clear();
        hasSel = false;
        auto pieces = CSG::splitMiddle(original, splitAxis);
        for (auto& p : pieces) brushes.push_back(p);
    }

    if (hasSel)
    {
        const float mn = brushes[selectedBrush].mins[splitAxis];
        const float mx = brushes[selectedBrush].maxs[splitAxis];
        splitPosition = glm::clamp(splitPosition, mn + 1.0f, mx - 1.0f);
        ImGui::DragFloat("Position##split", &splitPosition, 1.0f);
    }

    if (ImGui::Button("Split at Position##split", ImVec2(-1, 0)))
    {
        pushUndo(undoStack, brushes, redoStack);
        BrushVolume original = brushes[selectedBrush];
        brushes.erase(brushes.begin() + selectedBrush);
        selectedBrush = -1;
        selectedBrushes.clear();
        hasSel = false;
        auto pieces = CSG::split(original, splitAxis, splitPosition);
        for (auto& p : pieces) brushes.push_back(p);
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float halfWidth = (ImGui::GetContentRegionAvail().x - buttonSpacing) * 0.5f;

    ImGui::BeginDisabled(undoStack.empty());
    if (ImGui::Button("Undo##csg", ImVec2(halfWidth, 0)))
    {
        popUndo(undoStack, brushes, redoStack);
        selectedBrush = -1;
        selectedBrushes.clear();
        hasSel = false;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(redoStack.empty());
    if (ImGui::Button("Redo##csg", ImVec2(halfWidth, 0)))
    {
        popRedo(redoStack, brushes, undoStack);
        selectedBrush = -1;
        selectedBrushes.clear();
        hasSel = false;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Undo: %d | Redo: %d", (int)undoStack.size(), (int)redoStack.size());

    ImGui::Separator();
    ImGui::Text("Scene: %d brushes", (int)brushes.size());
    if (hasSel)
    {
        const BrushVolume& b = brushes[selectedBrush];
        const glm::vec3 sz   = b.maxs - b.mins;
        std::string fallbackName;
        ImGui::Text("Selected #%d", selectedBrush);
        ImGui::Text("  Name: %s", brushDisplayName(b, fallbackName, selectedBrush).c_str());
        ImGui::Text("  Size: %.0fx%.0fx%.0f", sz.x, sz.y, sz.z);
        ImGui::Text("  Center: %.0f, %.0f, %.0f",
                    b.center().x, b.center().y, b.center().z);
    }
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

    EditorEntity worldspawnEntity;
    ensureWorldspawnEntity(worldspawnEntity);
    std::vector<EditorEntity> sceneEntities;
    std::vector<BrushVolume> &brushes = worldspawnEntity.brushes;
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
    std::string currentTexturePath = resolveTexturePathForLoad(settings.currentTexturePath);
    bool assetViewAsGrid = settings.assetViewAsGrid;
    rescanAssets(assetRoot, assets);
    appendConsoleLine(console, "[editor] scanned assets folder");

    glm::vec3 focus = settings.focus;
    PendingBrush pendingBrush;
    PendingClip pendingClip;
    int selectedBrush = -1;
    std::vector<int> selectedBrushes;
    int selectedConvexBrush = -1;
    int selectedConvexFace = -1;
    int selectedEntity = -1;
    int selectedBrushFace = 2; // +Y by default
    BrushSelectionRect selectionRect;
    int orthoPopupViewIndex = -1;
    glm::vec3 orthoPopupWorld(0.0f);
    EditorTool currentTool = EditorTool::Select;
    EditorTheme currentTheme = EditorTheme::Dark;
    bool draggingSelection = false;
    EditorTool dragTool = EditorTool::Move;
    EditorViewType dragView = EditorViewType::Top;
    BrushScaleAxis dragScaleAxis = BrushScaleAxis::None;
    bool dragScalePositiveFace = true;
    glm::vec3 dragStartWorld(0.0f);
    glm::vec2 dragStartMouse(0.0f);
    BrushVolume dragOriginalBrush;
    EditorBrush dragOriginalConvexBrush;
    glm::vec3 dragOriginalEntityOrigin(0.0f);
    int dragOriginalFace = 2;
    int dragRotateTurns = 0;
    bool dragRotateCommitted = false;

    CSGOperation csgOp = CSGOperation::Add;
    float hollowThickness = 16.0f;
    float splitPosition = 0.0f;
    int splitAxis = 1;
    std::vector<std::vector<BrushVolume>> undoStack;
    std::vector<std::vector<BrushVolume>> redoStack;

    // Apply view settings
    for (int i = 0; i < 4; ++i)
    {
        views[i].focus = focus;
        views[i].orthoSize = settings.views[i].orthoSize;
        views[i].perspectiveDistance = settings.views[i].perspectiveDistance;
        views[i].perspectiveYaw = settings.views[i].perspectiveYaw;
        views[i].perspectivePitch = settings.views[i].perspectivePitch;
    }

    // Load tool icons
    ToolIcons toolIcons;
    applyEditorTheme(currentTheme);
    if (toolIcons.loadIcons())
        appendConsoleLine(console, "[editor] loaded tool icons");
    else
        appendConsoleLine(console, "[editor] WARNING: failed to load some tool icons");

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
        syncSceneConvexBrushes(worldspawnEntity, sceneEntities);
        if (selectedConvexBrush >= (int)extraConvexBrushCount(worldspawnEntity))
        {
            selectedConvexBrush = -1;
            selectedConvexFace = -1;
        }
        else if (EditorBrush *selectedConvex = getExtraConvexBrush(worldspawnEntity, selectedConvexBrush))
        {
            if (selectedConvexFace >= (int)selectedConvex->faces.size())
                selectedConvexFace = -1;
        }

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
                    if (saveEditorScene(scenePath,
                                        buildSceneEntitiesForSave(worldspawnEntity, sceneEntities),
                                        focus,
                                        currentTexturePath,
                                        error))
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

                // Camera/View presets
                if (ImGui::MenuItem("Front Face View"))
                {
                    for (int i = 0; i < layoutViewCount(settings.layoutMode); ++i)
                        views[i].type = EditorViewType::Front;
                    syncViewLabels(views);
                }
                if (ImGui::MenuItem("Back Face View"))
                {
                    for (int i = 0; i < layoutViewCount(settings.layoutMode); ++i)
                        views[i].type = EditorViewType::Back;
                    syncViewLabels(views);
                }
                if (ImGui::MenuItem("All"))
                {
                    views[0].type = EditorViewType::Top;
                    views[1].type = EditorViewType::Front;
                    views[2].type = EditorViewType::Right;
                    views[3].type = EditorViewType::Perspective;
                    settings.layoutMode = EditorLayoutMode::FourViews;
                    syncViewLabels(views);
                }
                ImGui::Separator();
                ImGui::MenuItem("Show Grid", nullptr, &settings.showGrid);
                ImGui::MenuItem("Show Axes", nullptr, &settings.showAxes);
                ImGui::MenuItem("Snap", nullptr, &settings.snapEnabled);
                ImGui::MenuItem("Texture Lock", nullptr, &settings.textureLock);
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
                if (ImGui::MenuItem("Select", "1", currentTool == EditorTool::Select))
                    currentTool = EditorTool::Select;
                if (ImGui::MenuItem("Move", "2", currentTool == EditorTool::Move))
                    currentTool = EditorTool::Move;
                if (ImGui::MenuItem("Scale", "3", currentTool == EditorTool::Scale))
                    currentTool = EditorTool::Scale;
                if (ImGui::MenuItem("Rotate", "4", currentTool == EditorTool::Rotate))
                    currentTool = EditorTool::Rotate;
                if (ImGui::MenuItem("Face", "5", currentTool == EditorTool::Face))
                    currentTool = EditorTool::Face;
                if (ImGui::MenuItem("Clip", "6", currentTool == EditorTool::Clip))
                    currentTool = EditorTool::Clip;
                if (ImGui::MenuItem("Create", "7", currentTool == EditorTool::Brush))
                    currentTool = EditorTool::Brush;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Theme"))
            {
                if (ImGui::MenuItem("Dark", nullptr, currentTheme == EditorTheme::Dark))
                {
                    currentTheme = EditorTheme::Dark;
                    applyEditorTheme(currentTheme);
                    appendConsoleLine(console, "[ui] theme " + std::string(themeName(currentTheme)));
                }
                if (ImGui::MenuItem("Light", nullptr, currentTheme == EditorTheme::Light))
                {
                    currentTheme = EditorTheme::Light;
                    applyEditorTheme(currentTheme);
                    appendConsoleLine(console, "[ui] theme " + std::string(themeName(currentTheme)));
                }
                if (ImGui::MenuItem("Classic", nullptr, currentTheme == EditorTheme::Classic))
                {
                    currentTheme = EditorTheme::Classic;
                    applyEditorTheme(currentTheme);
                    appendConsoleLine(console, "[ui] theme " + std::string(themeName(currentTheme)));
                }
                if (ImGui::MenuItem("Studio", nullptr, currentTheme == EditorTheme::Studio))
                {
                    currentTheme = EditorTheme::Studio;
                    applyEditorTheme(currentTheme);
                    appendConsoleLine(console, "[ui] theme " + std::string(themeName(currentTheme)));
                }
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
            ImGui::TextWrapped("Editor de brushes com 4 views. As tools vivem nas views ortograficas e no menu de contexto.");
            ImGui::Separator();

            // Transparency settings
            ImGui::Checkbox("Enable Transparency", &settings.enableTransparency);
            if (settings.enableTransparency)
            {
                ImGui::SliderFloat("Opacity", &settings.transparency, 0.0f, 1.0f, "%.2f");
            }
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
            if (ImGui::Button("Clear Brushes"))
            {
                pushUndo(undoStack, brushes, redoStack);
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
                    view.focus = glm::vec3(0.0f);
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
                ImGuiPropertyGrid::Checkbox("Texture Lock", &settings.textureLock);
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
                ImGuiPropertyGrid::Label("Entities");
                ImGui::Text("%d", (int)sceneEntities.size() + 1);
                ImGuiPropertyGrid::Label("Worldspawn");
                ImGui::TextUnformatted(worldspawnEntity.name.c_str());
                ImGuiPropertyGrid::Label("Brushes");
                ImGui::Text("%d", (int)brushes.size());
                ImGuiPropertyGrid::Label("Convex Brushes");
                ImGui::Text("%d", countConvexBrushesInScene(worldspawnEntity, sceneEntities));
                ImGuiPropertyGrid::Label("Other Entities");
                ImGui::Text("%d", (int)sceneEntities.size());
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
                ImGuiPropertyGrid::Label("Selected Brush");
                ImGui::Text("%d", selectedBrush);
                ImGuiPropertyGrid::Label("Selected Convex");
                ImGui::Text("%d", selectedConvexBrush);
                ImGuiPropertyGrid::Label("Selected Entity");
                ImGui::Text("%d", selectedEntity);
                ImGuiPropertyGrid::Label("Selection");
                ImGui::Text("%d", (int)selectedBrushes.size());
                ImGuiPropertyGrid::End();
            }

            if (ImGuiPropertyGrid::Section("Entities", true))
            {
                if (ImGui::Button("Add Player Start"))
                {
                    sceneEntities.push_back(makePointEntity("info_player_start", focus, (int)sceneEntities.size()));
                    selectedEntity = (int)sceneEntities.size() - 1;
                    selectedBrush = -1;
                    selectedBrushes.clear();
                    selectedConvexBrush = -1;
                    appendConsoleLine(console, "[entity] added info_player_start");
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Light"))
                {
                    sceneEntities.push_back(makePointEntity("light", focus, (int)sceneEntities.size()));
                    selectedEntity = (int)sceneEntities.size() - 1;
                    selectedBrush = -1;
                    selectedBrushes.clear();
                    selectedConvexBrush = -1;
                    appendConsoleLine(console, "[entity] added light");
                }

                ImGui::BeginChild("##entity_list", ImVec2(-1.0f, 110.0f), true);
                for (int i = 0; i < (int)sceneEntities.size(); ++i)
                {
                    const EditorEntity &entity = sceneEntities[i];
                    const std::string label = entityDisplayName(entity, i) + " [" + entity.classname + "]";
                    if (ImGui::Selectable(label.c_str(), selectedEntity == i))
                    {
                        selectedEntity = i;
                        selectedBrush = -1;
                        selectedBrushes.clear();
                        selectedConvexBrush = -1;
                    }
                }
                ImGui::EndChild();

                if (selectedEntity >= 0 && selectedEntity < (int)sceneEntities.size())
                {
                    EditorEntity &entity = sceneEntities[(size_t)selectedEntity];
                    float origin[3] = {entity.origin.x, entity.origin.y, entity.origin.z};

                    if (ImGuiPropertyGrid::Begin("entity_grid"))
                    {
                        ImGuiPropertyGrid::Label("Name");
                        ImGui::InputText("##entity_name", &entity.name);
                        ImGuiPropertyGrid::Label("Classname");
                        ImGui::InputText("##entity_classname", &entity.classname);
                        ImGuiPropertyGrid::Label("Origin");
                        if (ImGui::DragFloat3("##entity_origin", origin, 1.0f))
                            entity.origin = glm::vec3(origin[0], origin[1], origin[2]);
                        ImGuiPropertyGrid::Label("Hidden");
                        ImGui::Checkbox("##entity_hidden", &entity.hidden);
                        ImGuiPropertyGrid::End();
                    }

                    if (ImGui::Button("Move To Focus"))
                        entity.origin = focus;
                    ImGui::SameLine();
                    if (ImGui::Button("Delete Entity"))
                    {
                        sceneEntities.erase(sceneEntities.begin() + selectedEntity);
                        selectedEntity = -1;
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("KeyValues");
                    for (int i = 0; i < (int)entity.keyvalues.size(); ++i)
                    {
                        ImGui::PushID(i);
                        ImGui::InputText("##kv_key", &entity.keyvalues[(size_t)i].key);
                        ImGui::SameLine();
                        ImGui::InputText("##kv_value", &entity.keyvalues[(size_t)i].value);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X"))
                        {
                            entity.keyvalues.erase(entity.keyvalues.begin() + i);
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Add KeyValue"))
                        entity.keyvalues.push_back({"", ""});
                }
            }

            if (ImGuiPropertyGrid::Section("CSG Tools", true))
            {
                drawCSGPanel(brushes, selectedBrush, selectedBrushes, csgOp, hollowThickness, splitPosition, splitAxis, undoStack, redoStack);
            }

            if (ImGuiPropertyGrid::Section("Convex Builders", true))
            {
                static int cylinderSides = 8;
                static int rampDirection = 0;
                const char *rampDirectionNames[] = {"+X", "-X", "+Z", "-Z"};
                ImGui::Combo("Ramp Direction", &rampDirection, rampDirectionNames, IM_ARRAYSIZE(rampDirectionNames));
                ImGui::SliderInt("Cylinder Sides", &cylinderSides, 3, 24);

                if (ImGui::Button("Add Box"))
                {
                    const glm::vec3 mins = focus + glm::vec3(-64.0f, 0.0f, -64.0f);
                    const glm::vec3 maxs = focus + glm::vec3(64.0f, settings.defaultBrushHeight, 64.0f);
                    EditorBrush brush = makeBoxConvexBrush(mins,
                                                           maxs,
                                                           "Convex Box " + std::to_string((int)worldspawnEntity.convexBrushes.size() + 1),
                                                           currentTexturePath);
                    brush.color = randomBrushColor();
                    selectedConvexBrush = (int)extraConvexBrushCount(worldspawnEntity);
                    selectedConvexFace = -1;
                    selectedBrush = -1;
                    selectedBrushes.clear();
                    selectedEntity = -1;
                    worldspawnEntity.convexBrushes.push_back(brush);
                    appendConsoleLine(console, "[convex] added box brush");
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Ramp"))
                {
                    const glm::vec3 mins = focus + glm::vec3(-64.0f, 0.0f, -64.0f);
                    const glm::vec3 maxs = focus + glm::vec3(64.0f, settings.defaultBrushHeight, 64.0f);
                    EditorBrush brush = makeWedgeConvexBrush(mins,
                                                             maxs,
                                                             (EditorRampDirection)rampDirection,
                                                             "Ramp " + std::to_string((int)worldspawnEntity.convexBrushes.size() + 1),
                                                             currentTexturePath);
                    brush.color = randomBrushColor();
                    selectedConvexBrush = (int)extraConvexBrushCount(worldspawnEntity);
                    selectedConvexFace = -1;
                    selectedBrush = -1;
                    selectedBrushes.clear();
                    selectedEntity = -1;
                    worldspawnEntity.convexBrushes.push_back(brush);
                    appendConsoleLine(console, "[convex] added ramp brush");
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Cylinder"))
                {
                    EditorBrush brush = makeCylinderConvexBrush(focus + glm::vec3(0.0f, settings.defaultBrushHeight * 0.5f, 0.0f),
                                                                64.0f,
                                                                settings.defaultBrushHeight,
                                                                cylinderSides,
                                                                "Cylinder " + std::to_string((int)worldspawnEntity.convexBrushes.size() + 1),
                                                                currentTexturePath);
                    brush.color = randomBrushColor();
                    selectedConvexBrush = (int)extraConvexBrushCount(worldspawnEntity);
                    selectedConvexFace = -1;
                    selectedBrush = -1;
                    selectedBrushes.clear();
                    selectedEntity = -1;
                    worldspawnEntity.convexBrushes.push_back(brush);
                    appendConsoleLine(console, "[convex] added cylinder brush");
                }
                ImGui::TextDisabled("Clip e Face Move trabalham nos convex brushes.");
            }

            if (ImGuiPropertyGrid::Section("Convex Brush List", true))
            {
                const size_t extraStart = extraConvexBrushStartIndex(worldspawnEntity);
                ImGui::BeginChild("##convex_brush_list", ImVec2(-1.0f, 110.0f), true);
                for (size_t actualIndex = extraStart; actualIndex < worldspawnEntity.convexBrushes.size(); ++actualIndex)
                {
                    const int extraIndex = (int)(actualIndex - extraStart);
                    const EditorBrush &brush = worldspawnEntity.convexBrushes[actualIndex];
                    glm::vec3 mins(0.0f);
                    glm::vec3 maxs(0.0f);
                    const glm::vec3 size = convexBrushBounds(brush, mins, maxs) ? (maxs - mins) : glm::vec3(0.0f);

                    char label[192];
                    std::snprintf(label, sizeof(label), "%s%s [%s] (%.0f %.0f %.0f)",
                                  brush.hidden ? "[H] " : "",
                                  convexBrushDisplayName(brush, extraIndex).c_str(),
                                  convexBrushPrimitiveName(brush.primitive),
                                  size.x, size.y, size.z);

                    if (ImGui::Selectable(label, selectedConvexBrush == extraIndex))
                    {
                        selectedConvexBrush = extraIndex;
                        selectedConvexFace = -1;
                        selectedBrush = -1;
                        selectedBrushes.clear();
                        selectedEntity = -1;
                    }
                }
                ImGui::EndChild();

                EditorBrush *selectedConvex = getExtraConvexBrush(worldspawnEntity, selectedConvexBrush);
                if (selectedConvex)
                {
                    if (ImGui::Button("Delete Convex Brush"))
                    {
                        const int actualIndex = extraConvexBrushActualIndex(worldspawnEntity, selectedConvexBrush);
                        if (actualIndex >= 0)
                        {
                            worldspawnEntity.convexBrushes.erase(worldspawnEntity.convexBrushes.begin() + actualIndex);
                            selectedConvexBrush = -1;
                            selectedConvexFace = -1;
                            appendConsoleLine(console, "[convex] deleted selected brush");
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(selectedConvex->hidden ? "Unhide Convex Brush" : "Hide Convex Brush"))
                    {
                        selectedConvex->hidden = !selectedConvex->hidden;
                    }
                }
            }

            if (ImGuiPropertyGrid::Section("Brush List", true))
            {
                ImGui::BeginChild("##brush_list", ImVec2(-1.0f, 130.0f), true);
                for (int i = 0; i < (int)brushes.size(); ++i)
                {
                    const BrushVolume &b = brushes[i];
                    const glm::vec3 size = b.maxs - b.mins;
                    std::string fallbackName;
                    const std::string &displayName = brushDisplayName(b, fallbackName, i);
                    char label[160];
                    std::snprintf(label, sizeof(label), "%s%s (#%d)  (%.0f %.0f %.0f)",
                                  b.hidden ? "[H] " : "",
                                  displayName.c_str(),
                                  i, size.x, size.y, size.z);
                    const bool rowSelected = selectionContains(selectedBrushes, i);
                    if (ImGui::Selectable(label, rowSelected))
                    {
                        selectedEntity = -1;
                        selectedConvexBrush = -1;
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
                        pushUndo(undoStack, brushes, redoStack);
                        const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                        std::vector<int> newSelection = cloneSelectedBrushes(brushes, selectedBrushes, offset);
                        selectedBrushes = newSelection;
                        selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                        appendConsoleLine(console, "[edit] duplicated " + std::to_string((int)newSelection.size()) + " brush(es)");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete Brush"))
                    {
                        pushUndo(undoStack, brushes, redoStack);
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
                        pushUndo(undoStack, brushes, redoStack);
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

            if (EditorBrush *selectedConvex = getExtraConvexBrush(worldspawnEntity, selectedConvexBrush);
                selectedConvex && ImGuiPropertyGrid::Section("Convex Brush", true) &&
                ImGuiPropertyGrid::Begin("convex_brush_grid"))
            {
                float color[3] = {selectedConvex->color.x, selectedConvex->color.y, selectedConvex->color.z};
                glm::vec3 mins(0.0f);
                glm::vec3 maxs(0.0f);
                const bool hasBounds = convexBrushBounds(*selectedConvex, mins, maxs);
                const glm::vec3 center = hasBounds ? (mins + maxs) * 0.5f : glm::vec3(0.0f);
                const glm::vec3 size = hasBounds ? (maxs - mins) : glm::vec3(0.0f);

                ImGuiPropertyGrid::Label("Name");
                ImGui::InputText("##convex_brush_name", &selectedConvex->name);
                ImGuiPropertyGrid::Label("Primitive");
                ImGui::TextUnformatted(convexBrushPrimitiveName(selectedConvex->primitive));
                ImGuiPropertyGrid::Label("Faces");
                ImGui::Text("%d", (int)selectedConvex->faces.size());
                ImGuiPropertyGrid::Label("Selected Face");
                ImGui::Text("%d", selectedConvexFace);
                ImGuiPropertyGrid::Label("Hidden");
                ImGui::Checkbox("##convex_brush_hidden", &selectedConvex->hidden);
                ImGuiPropertyGrid::Label("Color");
                if (ImGui::ColorEdit3("##convex_brush_color", color))
                    selectedConvex->color = glm::vec3(color[0], color[1], color[2]);
                ImGuiPropertyGrid::Label("Center");
                ImGui::Text("(%.0f %.0f %.0f)", center.x, center.y, center.z);
                ImGuiPropertyGrid::Label("Size");
                ImGui::Text("(%.0f %.0f %.0f)", size.x, size.y, size.z);
                ImGuiPropertyGrid::End();

                if (ImGui::Button("Move Convex To Focus"))
                {
                    translateConvexBrush(*selectedConvex, focus - center);
                }
                if (!currentTexturePath.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Use Current Texture For All Faces"))
                    {
                        for (EditorBrushFace &face : selectedConvex->faces)
                            face.texturePath = currentTexturePath;
                    }
                    if (selectedConvexFace >= 0 && selectedConvexFace < (int)selectedConvex->faces.size())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Use Current For Selected Face"))
                            selectedConvex->faces[(size_t)selectedConvexFace].texturePath = currentTexturePath;
                    }
                }
            }

            if (!currentTexturePath.empty() && ImGuiPropertyGrid::Section("Current Texture", true))
            {
                // Load current texture preview once per unique texture path.
                const std::string texName = "current_texture_preview_" + currentTexturePath;
                const std::string resolvedTexturePath = resolveTexturePathForLoad(currentTexturePath);
                TextureManager &textureManager = TextureManager::instance();
                Texture *tex = textureManager.get(texName);
                static std::string failedCurrentTexturePreviewPath;
                if (!tex && failedCurrentTexturePreviewPath != resolvedTexturePath)
                {
                    tex = textureManager.load(texName, resolvedTexturePath);
                    if (!tex)
                        failedCurrentTexturePreviewPath = resolvedTexturePath;
                    else
                        failedCurrentTexturePreviewPath.clear();
                }
                
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
                    ImGui::Text("Failed to load: %s", resolvedTexturePath.c_str());
                }
            }

            if (selectedBrush >= 0 && selectedBrush < (int)brushes.size() &&
                ImGuiPropertyGrid::Section("Brush", true) &&
                ImGuiPropertyGrid::Begin("brush_grid"))
            {
                BrushVolume &brush = brushes[selectedBrush];
                if (brush.name.empty())
                    brush.name = defaultBrushName(selectedBrush);
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

                ImGuiPropertyGrid::Label("Name");
                ImGui::InputText("##brush_name", &brush.name);
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

            if (ImGuiPropertyGrid::Section("3D Focus", true) && ImGuiPropertyGrid::Begin("focus_grid"))
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

            if (ImGuiPropertyGrid::Section("Help", true))
            {
                ImGui::BulletText("1 Select, 2 Move, 3 Scale, 4 Rotate, 5 Face, 6 Clip, 7 Create");
                ImGui::BulletText("LMB ortho com Select: seleciona brush");
                ImGui::BulletText("Drag retangulo em Select: multi-selecao (Ctrl para adicionar)");
                ImGui::BulletText("Ctrl + clique: adiciona/remove da selecao");
                ImGui::BulletText("LMB ortho com Move: arrasta brush selecionado");
                ImGui::BulletText("LMB ortho com Scale: brush escala por eixo dominante (face)");
                ImGui::BulletText("LMB ortho com Rotate: roda em passos de 90 graus com drag");
                ImGui::BulletText("LMB com Face num convex brush: selecciona e move a face");
                ImGui::BulletText("LMB com Clip: 2 pontos numa view 2D para cortar convex brush");
                ImGui::BulletText("Texture Lock: ON prende a textura ao brush; OFF usa world-space");
                ImGui::BulletText("Mouse wheel sobre uma view: zoom");
                ImGui::BulletText("RMB na view 2D: popup de tools + view/edit");
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
                TextureManager &textureManager = TextureManager::instance();
                
                int itemIndex = 0;
                for (const AssetEntry &asset : assets)
                {
                    if (!containsInsensitive(asset.path, assetFilter) &&
                        !containsInsensitive(asset.name, assetFilter))
                        continue;

                    // Load texture thumbnail once and reuse cached handle.
                    const std::string texName = "asset_thumb_" + asset.path;
                    Texture *tex = textureManager.get(texName);
                    if (!tex)
                        tex = textureManager.load(texName, asset.path);
                    
                    if (itemIndex > 0 && itemIndex % itemsPerRow != 0)
                        ImGui::SameLine();
                    
                    ImGui::PushID(itemIndex);
                    const bool selected = asset.path == currentTexturePath;
                    if (selected)
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
                    
                    // Use a placeholder texture if loading failed
                    ImTextureID textureId = (ImTextureID)(intptr_t)(tex ? tex->id : textureManager.getWhite()->id);
                    
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
                    std::vector<EditorEntity> loadedEntities;
                    if (loadEditorScene(result.path, loadedEntities, focus, currentTexturePath, error))
                    {
                        applyLoadedSceneEntities(loadedEntities, worldspawnEntity, sceneEntities);
                        for (EditorView &view : views)
                            view.focus = focus;
                        scenePath = result.path.string();
                        selectedBrushes.clear();
                        selectedBrush = -1;
                        selectedConvexBrush = -1;
                        selectedEntity = -1;
                        appendConsoleLine(console, "[scene] loaded " + scenePath);
                        appendConsoleLine(console, "[scene] entities " + std::to_string((int)sceneEntities.size() + 1) +
                                                   " (worldspawn + " + std::to_string((int)sceneEntities.size()) + ")");
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
                    if (saveEditorScene(finalPath,
                                        buildSceneEntitiesForSave(worldspawnEntity, sceneEntities),
                                        focus,
                                        currentTexturePath,
                                        error))
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
        drawViewToolPalettes(views, activeViews, currentTool, toolIcons);

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
                orthoPointFromScreen(*hoveredViewPtr, hoveredViewPtr->focus, mousePos),
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
            if (Input::IsKeyPressed(KEY_FOUR)) currentTool = EditorTool::Rotate;
            if (Input::IsKeyPressed(KEY_FIVE)) currentTool = EditorTool::Face;
            if (Input::IsKeyPressed(KEY_SIX)) currentTool = EditorTool::Clip;
            if (Input::IsKeyPressed(KEY_SEVEN)) currentTool = EditorTool::Brush;

            if (ctrlDown && Input::IsKeyPressed(KEY_Z))
            {
                if (popUndo(undoStack, brushes, redoStack))
                {
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    selectedConvexBrush = -1;
                    selectedEntity = -1;
                    appendConsoleLine(console, "[edit] Undo");
                }
            }

            if (ctrlDown && Input::IsKeyPressed(KEY_Y))
            {
                if (popRedo(redoStack, brushes, undoStack))
                {
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    selectedConvexBrush = -1;
                    selectedEntity = -1;
                    appendConsoleLine(console, "[edit] Redo");
                }
            }

            if (Input::IsKeyPressed(KEY_X) && selectedBrush >= 0 && selectedBrush < (int)brushes.size())
            {
                pushUndo(undoStack, brushes, redoStack);
                auto pieces = CSG::splitMiddle(brushes[selectedBrush], splitAxis);
                brushes.erase(brushes.begin() + selectedBrush);
                for (auto& p : pieces) brushes.push_back(p);
                selectedBrushes.clear();
                selectedBrush = -1;
                selectedConvexBrush = -1;
                selectedEntity = -1;
                appendConsoleLine(console, "[csg] Split middle");
            }

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
                    if (saveEditorScene(scenePath,
                                        buildSceneEntitiesForSave(worldspawnEntity, sceneEntities),
                                        focus,
                                        currentTexturePath,
                                        error))
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
                pushUndo(undoStack, brushes, redoStack);
                const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                std::vector<int> newSelection = pasteBrushClipboard(brushes, brushClipboard, offset);
                selectedBrushes = newSelection;
                selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                selectedConvexBrush = -1;
                selectedEntity = -1;
                appendConsoleLine(console, "[edit] pasted brushes (Ctrl+V)");
            }

            if (ctrlDown && Input::IsKeyPressed(KEY_D) && !selectedBrushes.empty())
            {
                pushUndo(undoStack, brushes, redoStack);
                const float offset = glm::max(settings.snapEnabled ? settings.snapSize : settings.gridStep, 1.0f);
                std::vector<int> newSelection = cloneSelectedBrushes(brushes, selectedBrushes, offset);
                selectedBrushes = newSelection;
                selectedBrush = selectedBrushes.empty() ? -1 : selectedBrushes.back();
                selectedConvexBrush = -1;
                selectedEntity = -1;
                appendConsoleLine(console, "[edit] duplicated selected brushes (Ctrl+D)");
            }

            if (!selectedBrushes.empty() && Input::IsKeyPressed(KEY_H))
            {
                const std::vector<int> source = buildSortedUniqueValidSelection(selectedBrushes, (int)brushes.size());
                for (int index : source)
                    brushes[(size_t)index].hidden = !shiftDown;
                appendConsoleLine(console, shiftDown ? "[edit] unhid selected brushes (Shift+H)" : "[edit] hid selected brushes (H)");
            }

            if (selectedEntity >= 0 && selectedEntity < (int)sceneEntities.size() && Input::IsKeyPressed(KEY_H))
            {
                sceneEntities[(size_t)selectedEntity].hidden = !shiftDown;
                appendConsoleLine(console,
                                  shiftDown ? "[entity] unhid selected entity" : "[entity] hid selected entity");
            }

            if (EditorBrush *selectedConvex = getExtraConvexBrush(worldspawnEntity, selectedConvexBrush);
                selectedConvex && Input::IsKeyPressed(KEY_H))
            {
                selectedConvex->hidden = !shiftDown;
                appendConsoleLine(console,
                                  shiftDown ? "[convex] unhid selected brush" : "[convex] hid selected brush");
            }

            if (!selectedBrushes.empty() && Input::IsKeyPressed(KEY_DELETE))
            {
                pushUndo(undoStack, brushes, redoStack);
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
                selectedConvexBrush = -1;
                selectedEntity = -1;
                draggingSelection = false;
            }

            if (selectedEntity >= 0 && selectedEntity < (int)sceneEntities.size() && Input::IsKeyPressed(KEY_DELETE))
            {
                sceneEntities.erase(sceneEntities.begin() + selectedEntity);
                appendConsoleLine(console, "[entity] deleted selected entity");
                selectedEntity = -1;
                draggingSelection = false;
            }

            if (selectedConvexBrush >= 0 && Input::IsKeyPressed(KEY_DELETE))
            {
                const int actualIndex = extraConvexBrushActualIndex(worldspawnEntity, selectedConvexBrush);
                if (actualIndex >= 0)
                {
                    worldspawnEntity.convexBrushes.erase(worldspawnEntity.convexBrushes.begin() + actualIndex);
                    appendConsoleLine(console, "[convex] deleted selected brush");
                    selectedConvexBrush = -1;
                    draggingSelection = false;
                }
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
                    panFocusInOrthoView(hoveredViewPtr->focus, *hoveredViewPtr, delta);
            }

            const float wheel = Input::GetMouseWheelMoveV();
            if (std::fabs(wheel) > 1e-4f)
            {
                if (hoveredViewPtr->type == EditorViewType::Perspective)
                    hoveredViewPtr->perspectiveDistance = glm::clamp(hoveredViewPtr->perspectiveDistance - wheel * 32.0f, 32.0f, 4096.0f);
                else
                    hoveredViewPtr->orthoSize = glm::clamp(hoveredViewPtr->orthoSize - wheel * 16.0f, 16.0f, 2048.0f);
            }

            if (hoveredViewPtr->type == EditorViewType::Perspective &&
                Input::IsMouseDown(MouseButton::LEFT) &&
                !draggingSelection &&
                currentTool != EditorTool::Move)
            {
                const glm::vec2 delta = Input::GetMouseDelta();
                hoveredViewPtr->perspectiveYaw += delta.x * 0.25f;
                hoveredViewPtr->perspectivePitch = glm::clamp(hoveredViewPtr->perspectivePitch + delta.y * 0.25f, -89.0f, 89.0f);
            }

            if (hoveredViewPtr->type == EditorViewType::Perspective &&
                Input::IsMousePressed(MouseButton::LEFT) &&
                (currentTool == EditorTool::Select || currentTool == EditorTool::Face))
            {
                const int hitEntity = findEntityAtScreenPos(sceneEntities, *hoveredViewPtr, mousePos, 18.0f);
                if (hitEntity >= 0 && currentTool == EditorTool::Select)
                {
                    selectedEntity = hitEntity;
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    selectedConvexBrush = -1;
                    appendConsoleLine(console, "[entity] selected " + entityDisplayName(sceneEntities[(size_t)hitEntity], hitEntity));
                }
                else
                {
                const int hitConvex = findExtraConvexBrushAtScreenPos(worldspawnEntity, *hoveredViewPtr, mousePos);
                if (hitConvex >= 0 && currentTool == EditorTool::Select)
                {
                    selectedConvexBrush = hitConvex;
                    selectedConvexFace = -1;
                    selectedEntity = -1;
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    appendConsoleLine(console, "[convex] selected " + convexBrushDisplayName(*getExtraConvexBrush(worldspawnEntity, hitConvex), hitConvex));
                }
                else if (hitConvex >= 0 && currentTool == EditorTool::Face)
                {
                    selectedConvexBrush = hitConvex;
                    selectedEntity = -1;
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    int hitFace = -1;
                    EditorBrush *convexBrush = getExtraConvexBrush(worldspawnEntity, hitConvex);
                    if (convexBrush && findConvexFaceAtScreenPos(*convexBrush, *hoveredViewPtr, mousePos, hitFace))
                    {
                        selectedConvexFace = hitFace;
                        appendConsoleLine(console, "[convex] selected face " + std::to_string(hitFace));
                    }
                }
                else
                {
                const glm::vec2 localMouse(
                    mousePos.x - (float)hoveredViewPtr->rect.x,
                    mousePos.y - (float)hoveredViewPtr->rect.y);
                const Ray ray = hoveredViewPtr->camera.getRay(localMouse.x, localMouse.y);

                int hitBrush = -1;
                int hitFace = -1;
                if (pickBrushWithRay(brushes, ray, hitBrush, hitFace))
                {
                    selectedEntity = -1;
                    selectedConvexBrush = -1;
                    selectedConvexFace = -1;
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
                }
            }

            if (hoveredViewPtr->type != EditorViewType::Perspective && Input::IsMousePressed(MouseButton::LEFT))
            {
                if (currentTool == EditorTool::Select)
                {
                    const int hitEntity = findEntityAtScreenPos(sceneEntities, *hoveredViewPtr, mousePos, 18.0f);
                    if (hitEntity >= 0)
                    {
                        selectedEntity = hitEntity;
                        selectedBrushes.clear();
                        selectedBrush = -1;
                        selectedConvexBrush = -1;
                        selectedConvexFace = -1;
                        appendConsoleLine(console, "[entity] selected " + entityDisplayName(sceneEntities[(size_t)hitEntity], hitEntity));
                    }
                    else
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
                            hoveredViewPtr->focus,
                            currentTexturePath);
                        brush.name = defaultBrushName((int)brushes.size());
                        pushUndo(undoStack, brushes, redoStack);

                    brushes.push_back(brush);
                    selectedBrush = (int)brushes.size() - 1;
                        selectedEntity = -1;
                        selectedConvexBrush = -1;
                        selectedConvexFace = -1;
                        selectedBrushes.clear();
                    selectionAddUnique(selectedBrushes, selectedBrush);
                        pendingBrush.active = false;
                        appendConsoleLine(console,
                                      "[brush] created brush tex=" + currentTexturePath);
                    }
                }
                else if (currentTool == EditorTool::Face)
                {
                    const int hitConvex = findExtraConvexBrushAtScreenPos(worldspawnEntity, *hoveredViewPtr, mousePos);
                    const int targetConvex = hitConvex >= 0 ? hitConvex : selectedConvexBrush;
                    EditorBrush *convexBrush = getExtraConvexBrush(worldspawnEntity, targetConvex);
                    int hitFace = -1;
                    if (convexBrush && findConvexFaceAtScreenPos(*convexBrush, *hoveredViewPtr, mousePos, hitFace))
                    {
                        selectedEntity = -1;
                        selectedBrush = -1;
                        selectedBrushes.clear();
                        selectedConvexBrush = targetConvex;
                        selectedConvexFace = hitFace;
                        draggingSelection = true;
                        dragTool = EditorTool::Face;
                        dragView = hoveredViewPtr->type;
                        dragStartWorld = hoveredWorld;
                        dragOriginalConvexBrush = *convexBrush;
                        appendConsoleLine(console,
                                          "[convex] face drag " + std::to_string(selectedConvexFace));
                    }
                    else
                    {
                        const float pickDistance = glm::max(4.0f, hoveredViewPtr->orthoSize * 0.05f);
                        selectedBrush = findBrushAtPoint(brushes, hoveredViewPtr->type, hoveredWorld, pickDistance);
                        if (selectedBrush >= 0)
                        {
                            selectedEntity = -1;
                            selectedConvexBrush = -1;
                            selectedConvexFace = -1;
                            selectedBrushes.clear();
                            selectionAddUnique(selectedBrushes, selectedBrush);
                            selectedBrushFace = defaultFaceForView(hoveredViewPtr->type, selectedBrushFace);
                            appendConsoleLine(console,
                                              "[face] selected brush " + std::to_string(selectedBrush) +
                                              " face " + brushFaceName(selectedBrushFace));
                        }
                    }
                }
                else if (currentTool == EditorTool::Clip)
                {
                    EditorBrush *selectedConvex = getExtraConvexBrush(worldspawnEntity, selectedConvexBrush);
                    if (!selectedConvex)
                    {
                        appendConsoleLine(console, "[clip] select a convex brush first");
                    }
                    else if (!pendingClip.active || pendingClip.view != hoveredViewPtr->type)
                    {
                        pendingClip.active = true;
                        pendingClip.view = hoveredViewPtr->type;
                        pendingClip.start = hoveredWorld;
                        appendConsoleLine(console, "[clip] start");
                    }
                    else
                    {
                        glm::vec3 planePoint(0.0f);
                        glm::vec3 planeNormal(0.0f);
                        EditorBrush clippedBrush;
                        if (buildClipPlaneFromView(pendingClip.view, pendingClip.start, hoveredWorld, planePoint, planeNormal) &&
                            clipConvexBrush(*selectedConvex, planePoint, planeNormal, currentTexturePath, clippedBrush))
                        {
                            *selectedConvex = clippedBrush;
                            selectedConvexFace = -1;
                            appendConsoleLine(console, "[clip] applied");
                        }
                        else
                        {
                            appendConsoleLine(console, "[clip] failed");
                        }
                        pendingClip.active = false;
                    }
                }
                else if (currentTool == EditorTool::Move)
                {
                    const int hitEntity = findEntityAtScreenPos(sceneEntities, *hoveredViewPtr, mousePos, 18.0f);
                    const int hitConvex = findExtraConvexBrushAtScreenPos(worldspawnEntity, *hoveredViewPtr, mousePos);
                    if (hitEntity >= 0)
                    {
                        draggingSelection = true;
                        dragTool = EditorTool::Move;
                        dragView = hoveredViewPtr->type;
                        dragStartWorld = hoveredWorld;
                        dragOriginalEntityOrigin = sceneEntities[(size_t)hitEntity].origin;
                        selectedEntity = hitEntity;
                        selectedBrush = -1;
                        selectedBrushes.clear();
                        selectedConvexBrush = -1;
                        selectedConvexFace = -1;
                        appendConsoleLine(console, "[entity] begin drag " + entityDisplayName(sceneEntities[(size_t)hitEntity], hitEntity));
                    }
                    else if (selectedEntity >= 0 && selectedEntity < (int)sceneEntities.size())
                    {
                        draggingSelection = true;
                        dragTool = EditorTool::Move;
                        dragView = hoveredViewPtr->type;
                        dragStartWorld = hoveredWorld;
                        dragOriginalEntityOrigin = sceneEntities[(size_t)selectedEntity].origin;
                        appendConsoleLine(console, "[entity] begin drag " + entityDisplayName(sceneEntities[(size_t)selectedEntity], selectedEntity));
                    }
                    else if (hitConvex >= 0)
                    {
                        EditorBrush *convexBrush = getExtraConvexBrush(worldspawnEntity, hitConvex);
                        if (convexBrush)
                        {
                            draggingSelection = true;
                            dragTool = EditorTool::Move;
                            dragView = hoveredViewPtr->type;
                            dragStartWorld = hoveredWorld;
                            dragStartMouse = mousePos;
                            dragOriginalConvexBrush = *convexBrush;
                            selectedConvexBrush = hitConvex;
                            selectedConvexFace = -1;
                            selectedEntity = -1;
                            selectedBrush = -1;
                            selectedBrushes.clear();
                            appendConsoleLine(console, "[convex] begin drag " + convexBrushDisplayName(*convexBrush, hitConvex));
                        }
                    }
                    else if (getExtraConvexBrush(worldspawnEntity, selectedConvexBrush))
                    {
                        draggingSelection = true;
                        dragTool = EditorTool::Move;
                        dragView = hoveredViewPtr->type;
                        dragStartWorld = hoveredWorld;
                        dragStartMouse = mousePos;
                        dragOriginalConvexBrush = *getExtraConvexBrush(worldspawnEntity, selectedConvexBrush);
                        selectedEntity = -1;
                        selectedBrush = -1;
                        selectedBrushes.clear();
                        selectedConvexFace = -1;
                        appendConsoleLine(console, "[convex] begin drag " + convexBrushDisplayName(dragOriginalConvexBrush, selectedConvexBrush));
                    }
                    else if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        draggingSelection = true;
                        dragTool = EditorTool::Move;
                        dragView = hoveredViewPtr->type;
                        selectedEntity = -1;
                        selectedConvexBrush = -1;
                        selectedConvexFace = -1;
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
                        selectedEntity = -1;
                        dragScaleAxis = BrushScaleAxis::None;
                        dragScalePositiveFace = true;
                        dragStartWorld = hoveredWorld;
                        dragOriginalBrush = brushes[selectedBrush];
                        appendConsoleLine(console, "[scale] begin brush " + std::to_string(selectedBrush));
                    }
                }
                else if (currentTool == EditorTool::Rotate)
                {
                    if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        draggingSelection = true;
                        dragTool = EditorTool::Rotate;
                        dragView = hoveredViewPtr->type;
                        selectedEntity = -1;
                        dragStartWorld = hoveredWorld;
                        dragStartMouse = mousePos;
                        dragOriginalBrush = brushes[selectedBrush];
                        dragOriginalFace = selectedBrushFace;
                        dragRotateTurns = 0;
                        dragRotateCommitted = false;
                        appendConsoleLine(console, "[rotate] begin brush " + std::to_string(selectedBrush));
                    }
                }
            }

            if (draggingSelection &&
                hoveredViewPtr->type == dragView &&
                Input::IsMouseDown(MouseButton::LEFT))
            {
                if (dragTool == EditorTool::Move)
                {
                    if (selectedEntity >= 0 && selectedEntity < (int)sceneEntities.size())
                    {
                        if (dragView == EditorViewType::Perspective)
                        {
                            sceneEntities[(size_t)selectedEntity].origin =
                                dragOriginalEntityOrigin + entityPerspectiveDragDelta(*hoveredViewPtr, Input::GetMouseDelta());
                            dragOriginalEntityOrigin = sceneEntities[(size_t)selectedEntity].origin;
                        }
                        else
                        {
                            const glm::vec3 delta = applyViewDelta(hoveredWorld - dragStartWorld, dragView);
                            sceneEntities[(size_t)selectedEntity].origin = dragOriginalEntityOrigin + delta;
                        }
                    }
                    else if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        const glm::vec3 delta = applyViewDelta(hoveredWorld - dragStartWorld, dragView);
                        brushes[selectedBrush].mins = dragOriginalBrush.mins + delta;
                        brushes[selectedBrush].maxs = dragOriginalBrush.maxs + delta;
                    }
                    else if (EditorBrush *selectedConvex = getExtraConvexBrush(worldspawnEntity, selectedConvexBrush))
                    {
                        if (dragView == EditorViewType::Perspective)
                        {
                            translateConvexBrush(*selectedConvex, entityPerspectiveDragDelta(*hoveredViewPtr, Input::GetMouseDelta()));
                            dragOriginalConvexBrush = *selectedConvex;
                        }
                        else
                        {
                            const glm::vec3 delta = applyViewDelta(hoveredWorld - dragStartWorld, dragView);
                            *selectedConvex = dragOriginalConvexBrush;
                            translateConvexBrush(*selectedConvex, delta);
                        }
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
                else if (dragTool == EditorTool::Face)
                {
                    EditorBrush *selectedConvex = getExtraConvexBrush(worldspawnEntity, selectedConvexBrush);
                    if (dragView != EditorViewType::Perspective &&
                        selectedConvexFace >= 0 &&
                        selectedConvex)
                    {
                        const glm::vec3 normal = convexFaceNormal(dragOriginalConvexBrush.faces[(size_t)selectedConvexFace]);
                        const glm::vec3 delta = applyViewDelta(hoveredWorld - dragStartWorld, dragView);
                        EditorBrush movedBrush;
                        if (moveConvexBrushFace(dragOriginalConvexBrush,
                                                selectedConvexFace,
                                                glm::dot(delta, normal),
                                                movedBrush))
                        {
                            *selectedConvex = movedBrush;
                        }
                    }
                }
                else if (dragTool == EditorTool::Rotate)
                {
                    if (selectedBrush >= 0 && selectedBrush < (int)brushes.size())
                    {
                        const int turns = rotationTurnsFromMouseDrag(dragView, mousePos - dragStartMouse);
                        if (turns != 0 && !dragRotateCommitted)
                        {
                            pushUndo(undoStack, brushes, redoStack);
                            dragRotateCommitted = true;
                        }

                        if (turns != dragRotateTurns || (!dragRotateCommitted && dragRotateTurns != 0))
                        {
                            dragRotateTurns = turns;
                            BrushVolume rotated = dragOriginalBrush;
                            rotateBrushForView90(rotated, dragView, dragRotateTurns);
                            brushes[selectedBrush] = rotated;
                            selectedBrushFace = rotateFaceForView90(dragOriginalFace, dragView, dragRotateTurns);
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
                    orthoPointFromScreen(selView, selView.focus, mousePos),
                    settings.snapSize,
                    settings.snapEnabled);
            }

            if (Input::IsMouseReleased(MouseButton::LEFT))
            {
                    const float dragPixels = glm::length(selectionRect.endMouse - selectionRect.startMouse);
                if (!selectionRect.additive)
                {
                    selectedBrushes.clear();
                    selectedConvexBrush = -1;
                    selectedConvexFace = -1;
                    selectedEntity = -1;
                }

                if (dragPixels < 4.0f)
                {
                    float pickDistance = 8.0f;
                    if (selectionRect.viewIndex >= 0 && selectionRect.viewIndex < activeViews)
                        pickDistance = glm::max(4.0f, views[(size_t)selectionRect.viewIndex].orthoSize * 0.05f);

                    const EditorView &pickView = views[(size_t)selectionRect.viewIndex];
                    const int pickedConvex = findExtraConvexBrushAtScreenPos(worldspawnEntity, pickView, selectionRect.endMouse);
                    if (pickedConvex >= 0)
                    {
                        selectedConvexBrush = (selectionRect.additive && selectedConvexBrush == pickedConvex) ? -1 : pickedConvex;
                        selectedConvexFace = -1;
                        selectedBrushes.clear();
                        selectedBrush = -1;
                        selectedEntity = -1;
                    }
                    else
                    {
                        const int picked = findBrushAtPoint(brushes, selectionRect.viewType, selectionRect.endWorld, pickDistance);
                        if (picked >= 0)
                        {
                            selectedConvexBrush = -1;
                            selectedConvexFace = -1;
                            if (selectionRect.additive && selectionContains(selectedBrushes, picked))
                                selectionRemove(selectedBrushes, picked);
                            else
                                selectionAddUnique(selectedBrushes, picked);
                        }
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
                {
                    selectedEntity = -1;
                    selectedConvexBrush = -1;
                    selectedConvexFace = -1;
                    selectedBrushFace = defaultFaceForView(selectionRect.viewType, selectedBrushFace);
                }

                selectionRect.active = false;
            }
        }

            if (draggingSelection && Input::IsMouseReleased(MouseButton::LEFT))
            {
                draggingSelection = false;
                dragScaleAxis = BrushScaleAxis::None;
                dragRotateTurns = 0;
                dragRotateCommitted = false;
                if (selectedEntity >= 0 && selectedEntity < (int)sceneEntities.size())
                {
                    appendConsoleLine(console,
                                      "[entity] end drag " + entityDisplayName(sceneEntities[(size_t)selectedEntity], selectedEntity));
                }
                else if (selectedConvexBrush >= 0)
                {
                    appendConsoleLine(console, "[convex] end drag " + std::to_string(selectedConvexBrush));
                }
                else if (selectedBrush >= 0)
                {
                    std::string action = "[move] end drag brush ";
                    if (dragTool == EditorTool::Scale)
                    action = "[scale] end brush ";
                else if (dragTool == EditorTool::Rotate)
                    action = "[rotate] end brush ";
                appendConsoleLine(console, action + std::to_string(selectedBrush));
            }
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

            // Header bar for view label
            overlay->AddRectFilled(ImVec2((float)view.rect.x, (float)view.rect.y),
                             ImVec2((float)(view.rect.x + view.rect.w), (float)(view.rect.y + 28.0f)),
                             IM_COL32(45, 45, 45, 200),
                             0.0f);

            overlay->AddText(ImVec2((float)view.rect.x + 8.0f, (float)view.rect.y + 6.0f),
                             IM_COL32(255, 255, 255, 255),
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

        if (pendingClip.active)
        {
            for (int i = 0; i < activeViews; ++i)
            {
                const EditorView &view = views[(size_t)i];
                if (view.type != pendingClip.view)
                    continue;

                glm::vec2 clipStartScreen(0.0f);
                if (projectWorldToViewScreen(view, pendingClip.start, clipStartScreen))
                {
                    overlay->AddLine(ImVec2(clipStartScreen.x, clipStartScreen.y),
                                     ImVec2(mousePos.x, mousePos.y),
                                     IM_COL32(255, 90, 90, 255),
                                     2.0f);
                    overlay->AddCircleFilled(ImVec2(clipStartScreen.x, clipStartScreen.y), 4.0f, IM_COL32(255, 90, 90, 255), 12);
                }
                break;
            }
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
                const ImVec2 iconSizeMenu(16, 16);

                ImGui::Text("%s", popupView.label);
                ImGui::Separator();

                ImGui::TextDisabled("TOOLS:");
                ImGui::Separator();

                if (toolIcons.select && toolIcons.select->id != 0)
                {
                    ImGui::Image((ImTextureID)(intptr_t)toolIcons.select->id, iconSizeMenu);
                    ImGui::SameLine();
                }
                if (ImGui::MenuItem("SELECT (1)", nullptr, currentTool == EditorTool::Select))
                    currentTool = EditorTool::Select;

                if (toolIcons.move && toolIcons.move->id != 0)
                {
                    ImGui::Image((ImTextureID)(intptr_t)toolIcons.move->id, iconSizeMenu);
                    ImGui::SameLine();
                }
                if (ImGui::MenuItem("MOVE (2)", nullptr, currentTool == EditorTool::Move))
                    currentTool = EditorTool::Move;

                if (toolIcons.scale && toolIcons.scale->id != 0)
                {
                    ImGui::Image((ImTextureID)(intptr_t)toolIcons.scale->id, iconSizeMenu);
                    ImGui::SameLine();
                }
                if (ImGui::MenuItem("SCALE (3)", nullptr, currentTool == EditorTool::Scale))
                    currentTool = EditorTool::Scale;

                if (toolIcons.rotate && toolIcons.rotate->id != 0)
                {
                    ImGui::Image((ImTextureID)(intptr_t)toolIcons.rotate->id, iconSizeMenu);
                    ImGui::SameLine();
                }
                if (ImGui::MenuItem("ROTATE (4)", nullptr, currentTool == EditorTool::Rotate))
                    currentTool = EditorTool::Rotate;

                if (toolIcons.face && toolIcons.face->id != 0)
                {
                    ImGui::Image((ImTextureID)(intptr_t)toolIcons.face->id, iconSizeMenu);
                    ImGui::SameLine();
                }
                if (ImGui::MenuItem("FACE (5)", nullptr, currentTool == EditorTool::Face))
                    currentTool = EditorTool::Face;

                if (ImGui::MenuItem("CLIP (6)", nullptr, currentTool == EditorTool::Clip))
                    currentTool = EditorTool::Clip;

                if (toolIcons.brush && toolIcons.brush->id != 0)
                {
                    ImGui::Image((ImTextureID)(intptr_t)toolIcons.brush->id, iconSizeMenu);
                    ImGui::SameLine();
                }
                if (ImGui::MenuItem("CREATE (7)", nullptr, currentTool == EditorTool::Brush))
                    currentTool = EditorTool::Brush;

                ImGui::Separator();
                ImGui::TextDisabled("VIEW:");
                ImGui::Separator();

                if (ImGui::MenuItem("Center Here"))
                {
                    centerFocusForView(popupView.focus, popupView.type, orthoPopupWorld);
                    appendConsoleLine(console, std::string("[view] centered ") + popupView.label + " at mouse");
                }

                if (ImGui::MenuItem("Center Origin"))
                {
                    centerFocusForView(popupView.focus, popupView.type, glm::vec3(0.0f));
                    appendConsoleLine(console, std::string("[view] centered ") + popupView.label + " at origin");
                }

                if (ImGui::MenuItem("Reset Zoom"))
                {
                    popupView.orthoSize = 256.0f;
                    appendConsoleLine(console, std::string("[view] reset zoom ") + popupView.label);
                }

                if (ImGui::MenuItem("Reset 2D View"))
                {
                    centerFocusForView(popupView.focus, popupView.type, glm::vec3(0.0f));
                    popupView.orthoSize = 256.0f;
                    appendConsoleLine(console, std::string("[view] reset 2D view ") + popupView.label);
                }

                ImGui::Separator();
                ImGui::TextDisabled("EDIT:");
                ImGui::Separator();

                if (ImGui::MenuItem("Create Player Start Here"))
                {
                    sceneEntities.push_back(makePointEntity("info_player_start", orthoPopupWorld, (int)sceneEntities.size()));
                    selectedEntity = (int)sceneEntities.size() - 1;
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    appendConsoleLine(console, "[entity] created info_player_start at popup");
                }
                if (ImGui::MenuItem("Create Light Here"))
                {
                    sceneEntities.push_back(makePointEntity("light", orthoPopupWorld, (int)sceneEntities.size()));
                    selectedEntity = (int)sceneEntities.size() - 1;
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    appendConsoleLine(console, "[entity] created light at popup");
                }

                ImGui::Separator();

                if (!selectedBrushes.empty())
                {
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

                ImGui::Separator();
                if (ImGui::MenuItem("Clear All Brushes"))
                {
                    pushUndo(undoStack, brushes, redoStack);
                    brushes.clear();
                    selectedBrushes.clear();
                    selectedBrush = -1;
                    pendingBrush.active = false;
                    appendConsoleLine(console, "[view] cleared all brushes");
                }
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty()))
                {
                    popUndo(undoStack, brushes, redoStack);
                    selectedBrushes.clear();
                    selectedBrush = -1;
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack.empty()))
                {
                    popRedo(redoStack, brushes, undoStack);
                    selectedBrushes.clear();
                    selectedBrush = -1;
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
                             worldspawnEntity,
                             sceneEntities,
                             selectedBrushes,
                             selectedBrush,
                             selectedConvexBrush,
                             selectedConvexFace,
                             selectedEntity,
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
                             settings.textureLock,
                             settings.renderingMode,
                             settings.enableTransparency,
                             settings.transparency);
        }

        drawScreenOverlay(overlayBatch,
                          device.GetWidth(),
                          device.GetHeight(),
                          views,
                          activeViews,
                          sceneEntities,
                          selectedEntity,
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
