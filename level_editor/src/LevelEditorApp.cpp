#include "LevelEditorApp.hpp"

#include "Input.hpp"
#include "LevelEditorSceneIO.hpp"
#include "Manager.hpp"
#include "CSG.hpp"
#include "MeshCSG.hpp"
#include "Batch.hpp"
#include "Device.hpp"
#include "RenderTarget.hpp"
#include "RenderState.hpp"
#include "Opengl.hpp"
#include "Material.hpp"
#include "Utils.hpp"
#include "MeshLoader.hpp"
#include "BinaryStream.hpp"
#include <stb_image.h>
#include "stb_image_write.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <limits>
#include <set>

#include <glm/gtx/norm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ImGuizmo.h"
#include "imgui.h"
#include "imgui_stdlib.h"

// Desktop GL 3.3 Core: glPolygonMode not declared by GLES glad headers
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
static void (*s_glPolygonMode)(GLenum, GLenum) = nullptr;

namespace
{
glm::mat4 meshLocalPivotTransform(const LevelMeshObject& object,
                                  const glm::vec3& rotationEuler,
                                  const glm::vec3& scale);
bool detectTerrainGridDimensions(const EditableMesh& mesh, int& outCols, int& outRows);
void recomputeEditableMeshNormals(EditableMesh& mesh);
void applyTerrainBrushStroke(EditableMesh& mesh,
                             int cols,
                             int rows,
                             const glm::vec3& centerLocal,
                             float radius,
                             float strength,
                             float flattenHeight,
                             LevelEditorApp::TerrainSculptMode mode);
float sampleTerrainHeightLocal(const EditableMesh& mesh, int cols, int rows, float x, float z);

const char* selectionModeName(LevelEditorApp::SelectionMode mode)
{
    switch (mode)
    {
    case LevelEditorApp::SelectionMode::Object: return "Object";
    case LevelEditorApp::SelectionMode::Face: return "Face";
    case LevelEditorApp::SelectionMode::Edge: return "Edge";
    case LevelEditorApp::SelectionMode::Vertex: return "Vertex";
    }
    return "Object";
}

const char* toolName(LevelEditorApp::Tool tool)
{
    switch (tool)
    {
    case LevelEditorApp::Tool::Select: return "Select";
    case LevelEditorApp::Tool::Move: return "Move";
    case LevelEditorApp::Tool::Scale: return "Scale";
    case LevelEditorApp::Tool::Rotate: return "Rotate";
    }
    return "Tool";
}

const char* assetViewModeName(LevelEditorApp::AssetViewMode mode)
{
    switch (mode)
    {
    case LevelEditorApp::AssetViewMode::List: return "List";
    case LevelEditorApp::AssetViewMode::Details: return "Details";
    case LevelEditorApp::AssetViewMode::Grid: return "Grid";
    }
    return "Details";
}

const char* csgAxisName(int axis)
{
    switch (axis)
    {
    case 0: return "X";
    case 1: return "Y";
    case 2: return "Z";
    }
    return "Y";
}

const char* entityTypeName(LevelEntityType type)
{
    switch (type)
    {
    case LevelEntityType::PlayerStart: return "Player Start";
    case LevelEntityType::Light: return "Light";
    case LevelEntityType::Door: return "Door";
    case LevelEntityType::Elevator: return "Elevator";
    case LevelEntityType::Platform: return "Platform";
    case LevelEntityType::Placement: return "Placement";
    }
    return "Entity";
}

const char* meshPrimitiveName(LevelMeshPrimitive primitive)
{
    switch (primitive)
    {
    case LevelMeshPrimitive::Unknown: return "Unknown";
    case LevelMeshPrimitive::Box: return "Box";
    case LevelMeshPrimitive::Room: return "Room";
    case LevelMeshPrimitive::Sector: return "Sector";
    case LevelMeshPrimitive::RoomBoxesPart: return "Room Boxes Part";
    case LevelMeshPrimitive::Cylinder: return "Cylinder";
    case LevelMeshPrimitive::Cone: return "Cone";
    case LevelMeshPrimitive::Sphere: return "Sphere";
    case LevelMeshPrimitive::Torus: return "Torus";
    case LevelMeshPrimitive::Tube: return "Tube";
    case LevelMeshPrimitive::Pyramid: return "Pyramid";
    case LevelMeshPrimitive::DoorFrame: return "Door Frame";
    case LevelMeshPrimitive::Terrain: return "Terrain";
    case LevelMeshPrimitive::Pillar: return "Pillar";
    case LevelMeshPrimitive::Plane: return "Plane";
    case LevelMeshPrimitive::Wedge: return "Wedge";
    case LevelMeshPrimitive::Stairs: return "Stairs";
    case LevelMeshPrimitive::SpiralStairs: return "Spiral Stairs";
    case LevelMeshPrimitive::Text: return "Text";
    case LevelMeshPrimitive::Imported: return "Imported";
    case LevelMeshPrimitive::Empty: return "Empty";
    }
    return "Unknown";
}

const char* viewTypeName(LevelEditorApp::ViewType type)
{
    switch (type)
    {
    case LevelEditorApp::ViewType::Top: return "Top";
    case LevelEditorApp::ViewType::Bottom: return "Bottom";
    case LevelEditorApp::ViewType::Front: return "Front";
    case LevelEditorApp::ViewType::Back: return "Back";
    case LevelEditorApp::ViewType::Left: return "Left";
    case LevelEditorApp::ViewType::Right: return "Right";
    case LevelEditorApp::ViewType::Perspective: return "3D";
    }
    return "View";
}

ImU32 tileFillForView(LevelEditorApp::ViewType type)
{
    switch (type)
    {
    case LevelEditorApp::ViewType::Top: return IM_COL32(20, 24, 30, 255);
    case LevelEditorApp::ViewType::Bottom: return IM_COL32(20, 24, 30, 255);
    case LevelEditorApp::ViewType::Front: return IM_COL32(22, 26, 32, 255);
    case LevelEditorApp::ViewType::Back: return IM_COL32(22, 26, 32, 255);
    case LevelEditorApp::ViewType::Left: return IM_COL32(18, 23, 29, 255);
    case LevelEditorApp::ViewType::Right: return IM_COL32(18, 23, 29, 255);
    case LevelEditorApp::ViewType::Perspective: return IM_COL32(16, 20, 26, 255);
    }
    return IM_COL32(20, 24, 30, 255);
}

bool hasImageExtension(const std::string& path)
{
    const char* extensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* ext : extensions)
    {
        const std::string suffix(ext);
        if (lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0)
            return true;
    }
    return false;
}

bool containsInsensitive(const std::string& text, const std::string& pattern)
{
    if (pattern.empty())
        return true;

    std::string lowerText = text;
    std::string lowerPattern = pattern;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerPattern.begin(), lowerPattern.end(), lowerPattern.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowerText.find(lowerPattern) != std::string::npos;
}

std::filesystem::path ensureSceneExtension(const std::filesystem::path& path)
{
    if (path.extension() == ".mred")
        return path;
    std::filesystem::path out = path;
    out.replace_extension(".mred");
    return out;
}

float niceGridStep(float target)
{
    static const float kSteps[] = {
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f
    };
    for (float step : kSteps)
    {
        if (step >= target)
            return step;
    }
    return kSteps[sizeof(kSteps) / sizeof(kSteps[0]) - 1];
}

glm::vec3 normalizeEulerDegrees(const glm::vec3& degrees)
{
    glm::vec3 result = degrees;
    auto normalizeAngle = [](float angle) -> float
    {
        while (angle > 180.0f) angle -= 360.0f;
        while (angle < -180.0f) angle += 360.0f;
        return angle;
    };
    result.x = normalizeAngle(result.x);
    result.y = normalizeAngle(result.y);
    result.z = normalizeAngle(result.z);
    return result;
}

bool nearlyEqualVec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(epsilon)));
}

BoundingBox editableMeshLocalBounds(const EditableMesh& mesh)
{
    BoundingBox bounds;
    for (const EditableVertex& vertex : mesh.vertices())
        bounds.expand(vertex.position);
    return bounds;
}

glm::vec3 editableMeshBoundsCenter(const EditableMesh& mesh)
{
    const BoundingBox bounds = editableMeshLocalBounds(mesh);
    return (bounds.min + bounds.max) * 0.5f;
}

glm::vec3 editableMeshBoundsBottomCenter(const EditableMesh& mesh)
{
    const BoundingBox bounds = editableMeshLocalBounds(mesh);
    return glm::vec3((bounds.min.x + bounds.max.x) * 0.5f,
                     bounds.min.y,
                     (bounds.min.z + bounds.max.z) * 0.5f);
}

// Snap to nearest vertex in OTHER meshes (world space). Returns true if snapped.
bool snapToNearestVertex(const LevelEditorScene& scene, int excludeMeshIndex,
                         const glm::vec3& worldPos, float threshold, glm::vec3& outSnapped)
{
    float bestDist2 = threshold * threshold;
    bool found = false;
    for (int mi = 0; mi < static_cast<int>(scene.meshObjects().size()); ++mi)
    {
        if (mi == excludeMeshIndex) continue;
        const LevelMeshObject& other = scene.meshObjects()[mi];
        if (!other.visible) continue;
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), other.position)
            * glm::translate(glm::mat4(1.0f), other.pivot)
            * glm::rotate(glm::mat4(1.0f), glm::radians(other.rotationEuler.x), glm::vec3(1,0,0))
            * glm::rotate(glm::mat4(1.0f), glm::radians(other.rotationEuler.y), glm::vec3(0,1,0))
            * glm::rotate(glm::mat4(1.0f), glm::radians(other.rotationEuler.z), glm::vec3(0,0,1))
            * glm::scale(glm::mat4(1.0f), other.scale)
            * glm::translate(glm::mat4(1.0f), -other.pivot);
        for (const auto& v : other.mesh.vertices())
        {
            const glm::vec3 wp = glm::vec3(model * glm::vec4(v.position, 1.0f));
            const float d2 = glm::dot(wp - worldPos, wp - worldPos);
            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                outSnapped = wp;
                found = true;
            }
        }
    }
    return found;
}

glm::vec3 editableFaceNormal(const EditableMesh& mesh, const EditableFace& face)
{
    if (face.indices.size() < 3)
        return glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 normal(0.0f);
    for (std::size_t i = 0; i < face.indices.size(); ++i)
    {
        const int currentIndex = face.indices[i];
        const int nextIndex = face.indices[(i + 1) % face.indices.size()];
        if (currentIndex < 0 || currentIndex >= static_cast<int>(mesh.vertices().size()) ||
            nextIndex < 0 || nextIndex >= static_cast<int>(mesh.vertices().size()))
        {
            continue;
        }

        const glm::vec3& current = mesh.vertices()[(size_t)currentIndex].position;
        const glm::vec3& next = mesh.vertices()[(size_t)nextIndex].position;
        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }

    const float len2 = glm::length2(normal);
    if (len2 <= 1e-8f)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(normal);
}

bool extrudeMeshFace(LevelMeshObject& object, int faceIndex, float distance)
{
    if (faceIndex < 0 || faceIndex >= static_cast<int>(object.mesh.faces().size()))
        return false;

    EditableFace& face = object.mesh.facesMutable()[(size_t)faceIndex];
    if (face.indices.size() < 3)
        return false;

    const glm::vec3 normal = editableFaceNormal(object.mesh, face);
    std::vector<int> baseIndices = face.indices;
    std::vector<int> newIndices;
    newIndices.reserve(baseIndices.size());

    for (int index : baseIndices)
    {
        if (index < 0 || index >= static_cast<int>(object.mesh.vertices().size()))
            return false;

        EditableVertex vertex = object.mesh.vertices()[(size_t)index];
        vertex.position += normal * distance;
        object.mesh.verticesMutable().push_back(vertex);
        newIndices.push_back(static_cast<int>(object.mesh.vertices().size()) - 1);
    }

    face.indices = newIndices;

    std::vector<EditableFace>& faces = object.mesh.facesMutable();
    for (std::size_t i = 0; i < baseIndices.size(); ++i)
    {
        const std::size_t next = (i + 1) % baseIndices.size();
        EditableFace sideFace;
        sideFace.materialName = face.materialName;
        sideFace.indices = {
            baseIndices[i],
            baseIndices[next],
            newIndices[next],
            newIndices[i]
        };
        faces.push_back(sideFace);
    }

    return true;
}

glm::vec3 worldNormalForLocalDirection(const LevelMeshObject& object, const glm::vec3& localDirection)
{
    const glm::vec3 worldDirection = glm::vec3(meshLocalPivotTransform(object, object.rotationEuler, object.scale) *
                                               glm::vec4(localDirection, 0.0f));
    const float len2 = glm::length2(worldDirection);
    if (len2 <= 1e-8f)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(worldDirection);
}

glm::vec3 orthoVisibleAxisMask(LevelEditorApp::ViewType viewType)
{
    switch (viewType)
    {
    case LevelEditorApp::ViewType::Top:
    case LevelEditorApp::ViewType::Bottom:
        return glm::vec3(1.0f, 0.0f, 1.0f);
    case LevelEditorApp::ViewType::Front:
    case LevelEditorApp::ViewType::Back:
        return glm::vec3(1.0f, 1.0f, 0.0f);
    case LevelEditorApp::ViewType::Left:
    case LevelEditorApp::ViewType::Right:
        return glm::vec3(0.0f, 1.0f, 1.0f);
    case LevelEditorApp::ViewType::Perspective:
        break;
    }
    return glm::vec3(1.0f);
}

glm::mat4 meshLocalPivotTransform(const LevelMeshObject& object,
                                  const glm::vec3& rotationEuler,
                                  const glm::vec3& scale)
{
    glm::mat4 localTransform(1.0f);
    localTransform = glm::translate(localTransform, object.pivot);
    localTransform = glm::rotate(localTransform, glm::radians(rotationEuler.x), glm::vec3(1.0f, 0.0f, 0.0f));
    localTransform = glm::rotate(localTransform, glm::radians(rotationEuler.y), glm::vec3(0.0f, 1.0f, 0.0f));
    localTransform = glm::rotate(localTransform, glm::radians(rotationEuler.z), glm::vec3(0.0f, 0.0f, 1.0f));
    localTransform = glm::scale(localTransform, scale);
    localTransform = glm::translate(localTransform, -object.pivot);
    return localTransform;
}

glm::mat4 meshObjectPivotFrameMatrix(const LevelMeshObject& object)
{
    glm::mat4 frame(1.0f);
    frame = glm::translate(frame, object.position + object.pivot);
    frame = glm::rotate(frame, glm::radians(object.rotationEuler.x), glm::vec3(1.0f, 0.0f, 0.0f));
    frame = glm::rotate(frame, glm::radians(object.rotationEuler.y), glm::vec3(0.0f, 1.0f, 0.0f));
    frame = glm::rotate(frame, glm::radians(object.rotationEuler.z), glm::vec3(0.0f, 0.0f, 1.0f));
    frame = glm::scale(frame, object.scale);
    return frame;
}

glm::vec3 meshPivotTranslationDelta(const LevelMeshObject& object, const glm::vec3& newPivot)
{
    const glm::mat3 rotationScale = glm::mat3(meshLocalPivotTransform(object, object.rotationEuler, object.scale));
    const glm::vec3 oldOffset = object.pivot - rotationScale * object.pivot;
    const glm::vec3 newOffset = newPivot - rotationScale * newPivot;
    return oldOffset - newOffset;
}

void setMeshPivotPreserveWorld(LevelMeshObject& object, const glm::vec3& newPivot)
{
    if (nearlyEqualVec3(object.pivot, newPivot))
        return;

    object.position += meshPivotTranslationDelta(object, newPivot);
    object.pivot = newPivot;
}

void bakeMeshPivotIntoVertices(LevelMeshObject& object)
{
    if (nearlyEqualVec3(object.pivot, glm::vec3(0.0f)))
        return;

    for (EditableVertex& vertex : object.mesh.verticesMutable())
        vertex.position -= object.pivot;

    object.position += object.pivot;
    object.pivot = glm::vec3(0.0f);
}

void bakeMeshRotationScaleIntoVertices(LevelMeshObject& object)
{
    const glm::vec3 normalizedRotation = normalizeEulerDegrees(object.rotationEuler);
    const bool hasRotation = !nearlyEqualVec3(normalizedRotation, glm::vec3(0.0f));
    const bool hasScale = !nearlyEqualVec3(object.scale, glm::vec3(1.0f));
    if (!hasRotation && !hasScale)
        return;

    const glm::mat4 localTransform = meshLocalPivotTransform(object, object.rotationEuler, object.scale);

    for (EditableVertex& vertex : object.mesh.verticesMutable())
        vertex.position = glm::vec3(localTransform * glm::vec4(vertex.position, 1.0f));

    object.rotationEuler = glm::vec3(0.0f);
    object.scale = glm::vec3(1.0f);
}

void bakeMeshVertexTransform(LevelMeshObject& object,
                             const glm::vec3& translate,
                             const glm::vec3& rotateEuler,
                             const glm::vec3& scale)
{
    const bool hasTranslate = !nearlyEqualVec3(translate, glm::vec3(0.0f));
    const bool hasRotate = !nearlyEqualVec3(normalizeEulerDegrees(rotateEuler), glm::vec3(0.0f));
    const bool hasScale = !nearlyEqualVec3(scale, glm::vec3(1.0f));
    if (!hasTranslate && !hasRotate && !hasScale)
        return;

    glm::mat4 localTransform = meshLocalPivotTransform(object, rotateEuler, scale);
    localTransform = glm::translate(glm::mat4(1.0f), translate) * localTransform;

    for (EditableVertex& vertex : object.mesh.verticesMutable())
        vertex.position = glm::vec3(localTransform * glm::vec4(vertex.position, 1.0f));
}

glm::mat4 meshObjectModelMatrix(const LevelMeshObject& object)
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, object.position);
    model = glm::translate(model, object.pivot);
    model = glm::rotate(model, glm::radians(object.rotationEuler.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(object.rotationEuler.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(object.rotationEuler.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, object.scale);
    model = glm::translate(model, -object.pivot);
    return model;
}

std::string resolveLegacyImportedTexturePath(const std::string& materialName,
                                            const std::string& assetRoot,
                                            const std::string& lastImportDir);

std::string resolveSceneRelativePath(const std::string& rawPath,
                                     const std::filesystem::path& scenePath)
{
    if (rawPath.empty())
        return rawPath;

    std::error_code ec;
    std::filesystem::path candidate(rawPath);
    if (candidate.is_absolute())
        return candidate.lexically_normal().generic_string();

    if (std::filesystem::exists(candidate, ec))
        return candidate.lexically_normal().generic_string();

    if (!scenePath.empty())
    {
        const std::filesystem::path sceneDir = scenePath.parent_path();
        if (!sceneDir.empty())
        {
            const std::filesystem::path fromSceneDir = (sceneDir / candidate).lexically_normal();
            if (std::filesystem::exists(fromSceneDir, ec))
                return fromSceneDir.generic_string();

            const std::filesystem::path fromSceneParent = (sceneDir / ".." / candidate).lexically_normal();
            if (std::filesystem::exists(fromSceneParent, ec))
                return fromSceneParent.generic_string();
        }
    }

    return candidate.lexically_normal().generic_string();
}

Texture* findTextureByMaterialRef(TextureManager& texMgr, const std::string& materialRef)
{
    if (materialRef.empty() || materialRef == "default")
        return nullptr;

    return texMgr.get(materialRef);
}

Texture* resolveTextureForMaterialRef(TextureManager& texMgr,
                                      const std::string& materialRef,
                                      const std::string& cachePrefix,
                                      std::unordered_set<std::string>* failedLoads,
                                      const std::string& assetRoot,
                                      const std::string& lastImportDir)
{
    if (materialRef.empty() || materialRef == "default")
        return nullptr;

    const std::string directCacheKey = cachePrefix + materialRef;
    if (failedLoads && failedLoads->find(directCacheKey) != failedLoads->end())
        return nullptr;

    if (Texture* tex = findTextureByMaterialRef(texMgr, materialRef))
        return tex;

    if (Texture* tex = texMgr.get(directCacheKey))
        return tex;

    std::string resolvedPath;
    if (materialRef.rfind("import::", 0) == 0)
        resolvedPath = resolveLegacyImportedTexturePath(materialRef, assetRoot, lastImportDir);

    const std::string baseDir = assetRoot.empty() ? "assets" : assetRoot;
    if (resolvedPath.empty())
        resolvedPath = ResolveTexturePath(baseDir, materialRef);
    if (resolvedPath.empty())
        resolvedPath = ResolveTexturePath(baseDir, PathFilename(materialRef));

    if (resolvedPath.empty())
    {
        if (failedLoads)
            failedLoads->insert(directCacheKey);
        return nullptr;
    }

    if (Texture* tex = findTextureByMaterialRef(texMgr, resolvedPath))
        return tex;

    const std::string resolvedCacheKey = cachePrefix + resolvedPath;
    if (Texture* tex = texMgr.get(resolvedCacheKey))
        return tex;

    if (failedLoads && failedLoads->find(resolvedCacheKey) != failedLoads->end())
        return nullptr;

    Texture* tex = texMgr.load(resolvedCacheKey, resolvedPath);
    if (!tex && failedLoads)
    {
        failedLoads->insert(directCacheKey);
        failedLoads->insert(resolvedCacheKey);
    }
    return tex;
}

std::vector<LevelEditorApp::MeshTransformState> collectSelectedMeshTransformStates(
    const LevelEditorScene& scene,
    const std::vector<int>& selectedMeshIndices,
    int primaryMeshIndex)
{
    std::vector<LevelEditorApp::MeshTransformState> states;
    const auto& meshObjects = scene.meshObjects();
    states.reserve(selectedMeshIndices.size() + 1);

    auto appendIndex = [&](int index)
    {
        if (index < 0 || index >= static_cast<int>(meshObjects.size()))
            return;
        const auto existing = std::find_if(states.begin(), states.end(),
            [index](const LevelEditorApp::MeshTransformState& state) { return state.index == index; });
        if (existing != states.end())
            return;

        const LevelMeshObject& object = meshObjects[(size_t)index];
        LevelEditorApp::MeshTransformState state;
        state.index = index;
        state.position = object.position;
        state.rotationEuler = object.rotationEuler;
        state.scale = object.scale;
        state.pivot = object.pivot;
        states.push_back(state);
    };

    appendIndex(primaryMeshIndex);
    for (int index : selectedMeshIndices)
        appendIndex(index);
    return states;
}

glm::vec3 meshPivotWorldPosition(const LevelEditorApp::MeshTransformState& state)
{
    return state.position + state.pivot;
}

glm::vec3 selectionBoundsCenter(const LevelEditorScene& scene,
                                const std::vector<LevelEditorApp::MeshTransformState>& states)
{
    BoundingBox worldBounds;
    bool hasBounds = false;
    for (const LevelEditorApp::MeshTransformState& state : states)
    {
        if (state.index < 0 || state.index >= static_cast<int>(scene.meshObjects().size()))
            continue;

        LevelMeshObject object = scene.meshObjects()[(size_t)state.index];
        object.position = state.position;
        object.rotationEuler = state.rotationEuler;
        object.scale = state.scale;
        object.pivot = state.pivot;

        const BoundingBox localBounds = editableMeshLocalBounds(object.mesh);
        const BoundingBox transformed = localBounds.transformed(meshObjectModelMatrix(object));
        if (!transformed.is_valid())
            continue;
        if (!hasBounds)
        {
            worldBounds = transformed;
            hasBounds = true;
        }
        else
        {
            worldBounds.expand(transformed);
        }
    }

    if (hasBounds)
        return worldBounds.center();

    glm::vec3 average(0.0f);
    int count = 0;
    for (const LevelEditorApp::MeshTransformState& state : states)
    {
        average += meshPivotWorldPosition(state);
        ++count;
    }
    return count > 0 ? average / static_cast<float>(count) : glm::vec3(0.0f);
}

glm::vec3 orthoRotationAxis(LevelEditorApp::ViewType viewType)
{
    switch (viewType)
    {
    case LevelEditorApp::ViewType::Top:
    case LevelEditorApp::ViewType::Bottom:
        return glm::vec3(0.0f, 1.0f, 0.0f);
    case LevelEditorApp::ViewType::Front:
    case LevelEditorApp::ViewType::Back:
        return glm::vec3(0.0f, 0.0f, 1.0f);
    case LevelEditorApp::ViewType::Left:
    case LevelEditorApp::ViewType::Right:
        return glm::vec3(1.0f, 0.0f, 0.0f);
    case LevelEditorApp::ViewType::Perspective:
        break;
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 orthoGroupScaleFromDrag(const glm::vec2& dragMouseDelta,
                                  LevelEditorApp::ViewType viewType,
                                  const glm::vec3& startScale)
{
    glm::vec3 scaledSize = glm::vec3(1.0f);
    const glm::vec3 safeStartScale = glm::max(glm::abs(startScale), glm::vec3(0.01f));
    const float scaleSpeed = 0.01f;

    switch (viewType)
    {
    case LevelEditorApp::ViewType::Top:
    case LevelEditorApp::ViewType::Bottom:
        scaledSize.x = std::max(0.01f, safeStartScale.x + dragMouseDelta.x * scaleSpeed) / safeStartScale.x;
        scaledSize.z = std::max(0.01f, safeStartScale.z - dragMouseDelta.y * scaleSpeed) / safeStartScale.z;
        break;
    case LevelEditorApp::ViewType::Front:
    case LevelEditorApp::ViewType::Back:
        scaledSize.x = std::max(0.01f, safeStartScale.x + dragMouseDelta.x * scaleSpeed) / safeStartScale.x;
        scaledSize.y = std::max(0.01f, safeStartScale.y - dragMouseDelta.y * scaleSpeed) / safeStartScale.y;
        break;
    case LevelEditorApp::ViewType::Left:
    case LevelEditorApp::ViewType::Right:
        scaledSize.z = std::max(0.01f, safeStartScale.z + dragMouseDelta.x * scaleSpeed) / safeStartScale.z;
        scaledSize.y = std::max(0.01f, safeStartScale.y - dragMouseDelta.y * scaleSpeed) / safeStartScale.y;
        break;
    case LevelEditorApp::ViewType::Perspective:
        break;
    }

    return glm::max(scaledSize, glm::vec3(0.01f));
}

EditableMesh transformedEditableMesh(const EditableMesh& mesh, const glm::mat4& transform)
{
    std::vector<EditableVertex> vertices = mesh.vertices();
    for (EditableVertex& vertex : vertices)
        vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
    return EditableMesh::FromData(vertices, mesh.faces());
}

void fillRenderMeshFromEditable(const EditableMesh& editableMesh, Mesh& mesh)
{
    for (const EditableFace& face : editableMesh.faces())
    {
        if (face.indices.size() < 3)
            continue;

        const uint32_t baseIndex = static_cast<uint32_t>(mesh.buffer.vertices.size());

        // Check if all UVs in this face are zero — need auto-UV
        bool allZeroUV = true;
        for (int vertexIndex : face.indices)
        {
            if (vertexIndex < 0 || vertexIndex >= static_cast<int>(editableMesh.vertices().size()))
                continue;
            const glm::vec2& uv = editableMesh.vertices()[(size_t)vertexIndex].uv;
            if (uv.x != 0.0f || uv.y != 0.0f) { allZeroUV = false; break; }
        }

        // Compute face normal for planar projection
        glm::vec3 faceNormal(0.0f, 1.0f, 0.0f);
        if (allZeroUV && face.indices.size() >= 3)
        {
            const auto& verts = editableMesh.vertices();
            const glm::vec3& p0 = verts[(size_t)face.indices[0]].position;
            const glm::vec3& p1 = verts[(size_t)face.indices[1]].position;
            const glm::vec3& p2 = verts[(size_t)face.indices[2]].position;
            faceNormal = glm::cross(p1 - p0, p2 - p0);
            if (glm::length(faceNormal) > 1e-6f)
                faceNormal = glm::normalize(faceNormal);
        }

        for (int vertexIndex : face.indices)
        {
            if (vertexIndex < 0 || vertexIndex >= static_cast<int>(editableMesh.vertices().size()))
                continue;

            const EditableVertex& ev = editableMesh.vertices()[(size_t)vertexIndex];
            Vertex vertex{};
            vertex.position = ev.position;
            vertex.normal = ev.normal;
            vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

            if (allZeroUV)
            {
                // Planar box-mapping: project position onto the two axes
                // perpendicular to the dominant face normal axis
                const glm::vec3 absN = glm::abs(faceNormal);
                if (absN.y >= absN.x && absN.y >= absN.z)
                    vertex.uv = glm::vec2(ev.position.x, ev.position.z) * 0.01f;
                else if (absN.x >= absN.y && absN.x >= absN.z)
                    vertex.uv = glm::vec2(ev.position.z, ev.position.y) * 0.01f;
                else
                    vertex.uv = glm::vec2(ev.position.x, ev.position.y) * 0.01f;
            }
            else
            {
                vertex.uv = ev.uv;
            }

            mesh.buffer.vertices.push_back(vertex);
        }

        const uint32_t vertexCount = static_cast<uint32_t>(face.indices.size());
        for (uint32_t i = 1; i + 1 < vertexCount; ++i)
        {
            mesh.buffer.indices.push_back(baseIndex);
            mesh.buffer.indices.push_back(baseIndex + i);
            mesh.buffer.indices.push_back(baseIndex + i + 1);
        }
    }

    mesh.compute_normals();
}

EditableMesh makeEditableFromRenderMesh(const Mesh& mesh, const glm::mat4& transform)
{
    std::vector<EditableVertex> vertices;
    std::vector<EditableFace> faces;

    // Preserve source vertex splits (UV seams/hard edges) by mapping source vertex index 1:1.
    std::vector<int> sourceToEditable(mesh.buffer.vertices.size(), -1);
    auto getOrAddVertex = [&](uint32_t srcIndex) -> int
    {
        if (srcIndex >= sourceToEditable.size())
            return -1;
        int& mapped = sourceToEditable[srcIndex];
        if (mapped >= 0)
            return mapped;

        const Vertex& sv = mesh.buffer.vertices[srcIndex];
        EditableVertex v;
        v.position = glm::vec3(transform * glm::vec4(sv.position, 1.0f));
        v.normal = glm::normalize(glm::mat3(transform) * sv.normal);
        v.uv = sv.uv;
        mapped = static_cast<int>(vertices.size());
        vertices.push_back(v);
        return mapped;
    };

    if (!mesh.surfaces.empty())
    {
        for (const Surface& surface : mesh.surfaces)
        {
            std::string matName = "default";
            if (surface.material_index >= 0 &&
                surface.material_index < static_cast<int>(mesh.materials.size()) &&
                mesh.materials[(size_t)surface.material_index])
            {
                Material* mat = mesh.materials[(size_t)surface.material_index];
                Texture* albedo = mat->getTexture("u_albedo");
                if (albedo && !albedo->name.empty() && albedo->id != 0)
                    matName = albedo->name;
                else if (!mat->name.empty())
                    matName = mat->name;
                else
                    matName = "material_" + std::to_string(surface.material_index);
            }

            for (uint32_t i = surface.index_start; i + 2 < surface.index_start + surface.index_count; i += 3)
            {
                if (i + 2 >= mesh.buffer.indices.size())
                    continue;

                EditableFace face;
                face.materialName = matName;
                face.uvProjection = UvProjection::Mesh;
                bool valid = true;
                for (int k = 0; k < 3; ++k)
                {
                    const uint32_t index = mesh.buffer.indices[i + k];
                    const int vi = getOrAddVertex(index);
                    if (vi < 0)
                    { valid = false; break; }
                    face.indices.push_back(vi);
                }
                if (valid && face.indices.size() == 3)
                    faces.push_back(face);
            }
        }
    }
    else
    {
        for (std::size_t i = 0; i + 2 < mesh.buffer.indices.size(); i += 3)
        {
            EditableFace face;
            face.materialName = "default";
            face.uvProjection = UvProjection::Mesh;
            bool valid = true;
            for (int k = 0; k < 3; ++k)
            {
                const uint32_t index = mesh.buffer.indices[i + k];
                const int vi = getOrAddVertex(index);
                if (vi < 0)
                { valid = false; break; }
                face.indices.push_back(vi);
            }
            if (valid && face.indices.size() == 3)
                faces.push_back(face);
        }
    }

    return EditableMesh::FromData(vertices, faces);
}

MeshPlane editableFacePlane(const EditableMesh& mesh, const EditableFace& face, const glm::vec3& meshCenter)
{
    glm::vec3 faceCenter(0.0f);
    int validCount = 0;
    for (int index : face.indices)
    {
        if (index < 0 || index >= static_cast<int>(mesh.vertices().size()))
            continue;
        faceCenter += mesh.vertices()[(size_t)index].position;
        ++validCount;
    }

    if (validCount <= 0)
        return MeshPlane::FromPointNormal(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    faceCenter /= static_cast<float>(validCount);
    glm::vec3 normal = editableFaceNormal(mesh, face);
    if (glm::dot(normal, meshCenter - faceCenter) > 0.0f)
        normal = -normal;
    return MeshPlane::FromPointNormal(faceCenter, normal);
}

std::vector<MeshPlane> buildConvexClipPlanes(const EditableMesh& mesh)
{
    std::vector<MeshPlane> planes;
    if (mesh.vertexCount() == 0)
        return planes;

    glm::vec3 center(0.0f);
    for (const EditableVertex& vertex : mesh.vertices())
        center += vertex.position;
    center /= static_cast<float>(mesh.vertexCount());

    planes.reserve(mesh.faceCount());
    for (const EditableFace& face : mesh.faces())
    {
        if (face.indices.size() < 3)
            continue;
        planes.push_back(editableFacePlane(mesh, face, center));
    }
    return planes;
}

EditableMesh intersectEditableMeshesConvex(const EditableMesh& target, const EditableMesh& cutter)
{
    EditableMesh result = target;
    for (const MeshPlane& plane : buildConvexClipPlanes(cutter))
    {
        result = clipEditableMeshAgainstPlane(result, plane, false, true);
        if (result.faceCount() == 0)
            break;
    }
    return result;
}

std::vector<EditableMesh> subtractEditableMeshesConvex(const EditableMesh& target, const EditableMesh& cutter)
{
    std::vector<EditableMesh> pieces;
    EditableMesh remaining = target;
    for (const MeshPlane& plane : buildConvexClipPlanes(cutter))
    {
        const EditableMesh outside = clipEditableMeshAgainstPlane(remaining, plane, true, true);
        if (outside.faceCount() > 0)
            pieces.push_back(outside);

        remaining = clipEditableMeshAgainstPlane(remaining, plane, false, true);
        if (remaining.faceCount() == 0)
            break;
    }
    return pieces;
}

}

void LevelEditorApp::InvalidateMeshCache()
{
    for (std::size_t i = 0; i < meshGPUCacheCount_; ++i)
    {
        meshGPUCache_[i].buffer.free();
        meshGPUCache_[i].faceRanges.clear();
    }
    delete[] meshGPUCache_;
    meshGPUCache_ = nullptr;
    meshGPUCacheCount_ = 0;
    meshCacheValid_ = false;
}

void LevelEditorApp::RebuildMeshCache()
{
    InvalidateMeshCache();

    const std::size_t count = scene_.meshObjects().size();
    if (count == 0) { meshCacheValid_ = true; return; }

    meshGPUCache_ = new CachedMeshGPU[count];
    meshGPUCacheCount_ = count;

    for (std::size_t i = 0; i < count; ++i)
    {
        const LevelMeshObject& object = scene_.meshObjects()[i];
        CachedMeshGPU& cached = meshGPUCache_[i];
        const auto& faces = object.mesh.faces();
        const auto& verts = object.mesh.vertices();
        const std::size_t faceCount = faces.size();

        // Build sorted face order by materialName for batched rendering
        std::vector<std::size_t> sortedFaceIndices(faceCount);
        for (std::size_t fi = 0; fi < faceCount; ++fi) sortedFaceIndices[fi] = fi;
        std::sort(sortedFaceIndices.begin(), sortedFaceIndices.end(),
            [&](std::size_t a, std::size_t b) { return faces[a].materialName < faces[b].materialName; });

        // Initialize per-face ranges (indexed by original face index)
        cached.faceRanges.resize(faceCount, {0, 0});

        // Emit vertices/indices in sorted material order
        std::string currentMat;
        uint32_t matStart = 0;

        // Compute mesh center for cylindrical/spherical UV projection
        glm::vec3 meshCenter(0.0f);
        if (!verts.empty())
        {
            for (const auto& v : verts) meshCenter += v.position;
            meshCenter /= static_cast<float>(verts.size());
        }

        auto emitFace = [&](std::size_t fi)
        {
            const EditableFace& face = faces[fi];
            if (face.indices.size() < 3) return;

            const uint32_t indexOffset = static_cast<uint32_t>(cached.buffer.indices.size());
            const uint32_t baseVert = static_cast<uint32_t>(cached.buffer.vertices.size());

            // Face normal for planar UV
            glm::vec3 faceN(0.0f, 1.0f, 0.0f);
            {
                const glm::vec3& p0 = verts[(size_t)face.indices[0]].position;
                const glm::vec3& p1 = verts[(size_t)face.indices[1]].position;
                const glm::vec3& p2 = verts[(size_t)face.indices[2]].position;
                glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
                if (glm::length(cr) > 1e-6f) faceN = glm::normalize(cr);
            }

            const float cosR = std::cos(glm::radians(face.uvRotation));
            const float sinR = std::sin(glm::radians(face.uvRotation));

            int vertInFace = 0;
            for (int idx : face.indices)
            {
                if (idx < 0 || idx >= static_cast<int>(verts.size())) { ++vertInFace; continue; }
                const EditableVertex& ev = verts[(size_t)idx];
                Vertex vert{};
                vert.position = ev.position;
                vert.normal = ev.normal;
                // Store lightmap UV in tangent.xy if available
                glm::vec2 lmUv(0.0f, 0.0f);
                if (i < lightmapResult_.meshUVs.size() &&
                    fi < lightmapResult_.meshUVs[i].faceVertexUVs.size() &&
                    vertInFace < static_cast<int>(lightmapResult_.meshUVs[i].faceVertexUVs[fi].size()))
                {
                    lmUv = lightmapResult_.meshUVs[i].faceVertexUVs[fi][vertInFace];
                }
                vert.tangent = glm::vec4(lmUv.x, lmUv.y, 0.0f, 1.0f);

                const glm::vec3 absN = glm::abs(faceN);
                glm::vec2 uv;
                switch (face.uvProjection)
                {
                case UvProjection::Planar:
                {
                    // Project onto the face plane using face normal as projection axis
                    // Build tangent/bitangent from face normal
                    glm::vec3 tangent = glm::cross(faceN, glm::vec3(0, 1, 0));
                    if (glm::length(tangent) < 0.01f)
                        tangent = glm::cross(faceN, glm::vec3(1, 0, 0));
                    tangent = glm::normalize(tangent);
                    const glm::vec3 bitangent = glm::normalize(glm::cross(faceN, tangent));
                    uv = glm::vec2(glm::dot(ev.position, tangent), glm::dot(ev.position, bitangent));
                    break;
                }
                case UvProjection::Cylindrical:
                {
                    // Cylindrical: angle around Y axis -> U, height -> V
                    const glm::vec3 rel = ev.position - meshCenter;
                    uv.x = std::atan2(rel.x, rel.z) / 6.2831853f + 0.5f;
                    uv.y = rel.y * 0.01f;
                    break;
                }
                case UvProjection::Spherical:
                {
                    // Spherical: longitude -> U, latitude -> V
                    const glm::vec3 rel = ev.position - meshCenter;
                    const float len = glm::length(rel);
                    const glm::vec3 d = len > 1e-6f ? rel / len : glm::vec3(0, 1, 0);
                    uv.x = std::atan2(d.x, d.z) / 6.2831853f + 0.5f;
                    uv.y = std::asin(glm::clamp(d.y, -1.0f, 1.0f)) / 3.1415926f + 0.5f;
                    break;
                }
                case UvProjection::Mesh:
                    uv = ev.uv;
                    break;
                case UvProjection::Box:
                default:
                {
                    if (absN.y >= absN.x && absN.y >= absN.z)
                        uv = glm::vec2(ev.position.x, ev.position.z);
                    else if (absN.x >= absN.y && absN.x >= absN.z)
                        uv = glm::vec2(ev.position.z, ev.position.y);
                    else
                        uv = glm::vec2(ev.position.x, ev.position.y);
                    break;
                }
                }

                const glm::vec2 uvScale = (face.uvProjection == UvProjection::Mesh)
                    ? face.uvScale
                    : (face.uvScale * 0.01f);
                uv *= uvScale;
                const float ru = uv.x * cosR - uv.y * sinR;
                const float rv = uv.x * sinR + uv.y * cosR;
                uv = glm::vec2(ru, rv) + face.uvOffset;

                vert.uv = uv;
                cached.buffer.vertices.push_back(vert);
                ++vertInFace;
            }

            const uint32_t vertCount = static_cast<uint32_t>(face.indices.size());
            for (uint32_t t = 1; t + 1 < vertCount; ++t)
            {
                cached.buffer.indices.push_back(baseVert);
                cached.buffer.indices.push_back(baseVert + t);
                cached.buffer.indices.push_back(baseVert + t + 1);
            }

            const uint32_t indexCount = (vertCount - 2) * 3;
            cached.faceRanges[fi] = {indexOffset, indexCount};
        };

        for (std::size_t si = 0; si < sortedFaceIndices.size(); ++si)
        {
            const std::size_t fi = sortedFaceIndices[si];
            const std::string& mat = faces[fi].materialName;

            if (si == 0)
            {
                currentMat = mat;
                matStart = static_cast<uint32_t>(cached.buffer.indices.size());
            }
            else if (mat != currentMat)
            {
                // Close previous material range
                const uint32_t matEnd = static_cast<uint32_t>(cached.buffer.indices.size());
                if (matEnd > matStart)
                    cached.materialRanges.push_back({currentMat, matStart, matEnd - matStart});
                currentMat = mat;
                matStart = matEnd;
            }

            emitFace(fi);
        }

        // Close last range
        {
            const uint32_t matEnd = static_cast<uint32_t>(cached.buffer.indices.size());
            if (matEnd > matStart)
                cached.materialRanges.push_back({currentMat, matStart, matEnd - matStart});
        }

        // Compute normals
        for (auto& vtx : cached.buffer.vertices) vtx.normal = glm::vec3(0.0f);
        for (size_t j = 0; j + 2 < cached.buffer.indices.size(); j += 3)
        {
            Vertex& v0 = cached.buffer.vertices[cached.buffer.indices[j]];
            Vertex& v1 = cached.buffer.vertices[cached.buffer.indices[j+1]];
            Vertex& v2 = cached.buffer.vertices[cached.buffer.indices[j+2]];
            glm::vec3 n = glm::cross(v1.position - v0.position, v2.position - v0.position);
            v0.normal += n; v1.normal += n; v2.normal += n;
        }
        for (auto& vtx : cached.buffer.vertices)
            if (glm::length(vtx.normal) > 1e-6f) vtx.normal = glm::normalize(vtx.normal);

        cached.buffer.upload();
    }

    meshCacheValid_ = true;
}

void LevelEditorApp::StartBakeAsync()
{
    if (bakeRunning_) return;
    bakeRunning_ = true;
    bakeProgress_.store(0.0f);
    // Copy the scene so the thread doesn't touch live data
    bakeSceneCopy_ = scene_;
    LightmapSettings settings = lightmapSettings_;

    bakeThread_ = std::make_unique<std::thread>([this, settings]() {
        bakeResult_ = BakeLightmaps(bakeSceneCopy_, settings, &bakeProgress_);
    });
}

void LevelEditorApp::FinishBakeAsync()
{
    if (!bakeRunning_ || !bakeThread_) return;
    bakeThread_->join();
    bakeThread_.reset();
    bakeRunning_ = false;

    lightmapResult_ = std::move(bakeResult_);

    // Upload to GPU
    if (lightmapTexture_)
        glDeleteTextures(1, &lightmapTexture_);
    lightmapTexture_ = 0;

    if (!lightmapResult_.pixels.empty())
    {
        // Save PNG
        stbi_write_png(lightmapResult_.savedPath.c_str(),
                       lightmapResult_.width, lightmapResult_.height,
                       3, lightmapResult_.pixels.data(), lightmapResult_.width * 3);

        glGenTextures(1, &lightmapTexture_);
        glBindTexture(GL_TEXTURE_2D, lightmapTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, lightmapResult_.width, lightmapResult_.height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, lightmapResult_.pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Reset RenderState cache so old texture ID doesn't alias
    RenderState::instance().resetCache();

    useLightmap_ = (lightmapTexture_ != 0);
    meshCacheValid_ = false;

    printf("[Lightmap] Bake finished. Texture=%u, %dx%d\n",
           lightmapTexture_, lightmapResult_.width, lightmapResult_.height);
}

void LevelEditorApp::BakeAndUploadLightmap()
{
    printf("[Lightmap] Baking... resolution=%d, meshes=%d, entities=%d\n",
           lightmapSettings_.resolution,
           (int)scene_.meshObjects().size(),
           (int)scene_.entities().size());

    int lightCount = 0;
    for (const auto& e : scene_.entities())
        if (e.type == LevelEntityType::Light) {
            printf("[Lightmap]  Light: pos=(%.1f,%.1f,%.1f) color=(%.2f,%.2f,%.2f) intensity=%.2f radius=%.1f\n",
                   e.position.x, e.position.y, e.position.z,
                   e.color.r, e.color.g, e.color.b, e.intensity, e.radius);
            lightCount++;
        }
    printf("[Lightmap]  %d lights found\n", lightCount);

    lightmapResult_ = BakeLightmaps(scene_, lightmapSettings_);

    printf("[Lightmap] Result: %dx%d, %d bytes, meshUVs=%d\n",
           lightmapResult_.width, lightmapResult_.height,
           (int)lightmapResult_.pixels.size(),
           (int)lightmapResult_.meshUVs.size());

    // Check pixel content
    if (!lightmapResult_.pixels.empty())
    {
        int nonZero = 0;
        float maxVal = 0;
        for (uint8_t p : lightmapResult_.pixels) {
            if (p > 0) nonZero++;
            if (p > maxVal) maxVal = p;
        }
        printf("[Lightmap] Pixels: %d non-zero out of %d, max=%d\n",
               nonZero, (int)lightmapResult_.pixels.size(), (int)maxVal);
    }

    // Check UV content
    for (int mi = 0; mi < (int)lightmapResult_.meshUVs.size(); ++mi)
    {
        const auto& muv = lightmapResult_.meshUVs[mi];
        int uvCount = 0;
        for (const auto& fv : muv.faceVertexUVs) uvCount += (int)fv.size();
        printf("[Lightmap] Mesh %d: %d faces with UVs, total %d verts\n",
               mi, (int)muv.faceVertexUVs.size(), uvCount);
    }

    // Save to PNG
    if (!lightmapResult_.pixels.empty())
    {
        stbi_write_png(lightmapResult_.savedPath.c_str(),
                       lightmapResult_.width, lightmapResult_.height,
                       3, lightmapResult_.pixels.data(), lightmapResult_.width * 3);
    }

    // Upload to GPU
    if (lightmapTexture_)
        glDeleteTextures(1, &lightmapTexture_);
    lightmapTexture_ = 0;

    if (!lightmapResult_.pixels.empty())
    {
        glGenTextures(1, &lightmapTexture_);
        glBindTexture(GL_TEXTURE_2D, lightmapTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, lightmapResult_.width, lightmapResult_.height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, lightmapResult_.pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Force mesh cache rebuild to inject lightmap UVs into tangent.xy
    useLightmap_ = (lightmapTexture_ != 0);
    meshCacheValid_ = false;
}

bool LevelEditorApp::Section(const char* label, bool defaultOpen)
{
    auto it = sectionOpen_.find(label);
    if (it == sectionOpen_.end())
        it = sectionOpen_.emplace(label, defaultOpen).first;
    ImGui::SetNextItemOpen(it->second, ImGuiCond_Always);
    bool open = ImGui::CollapsingHeader(label);
    it->second = open;
    return open;
}

bool LevelEditorApp::SelectedMeshIsTerrain(int* outCols, int* outRows) const
{
    if (selectedMeshIndex_ < 0 || selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
        return false;

    const LevelMeshObject& selectedObject = scene_.meshObjects()[static_cast<std::size_t>(selectedMeshIndex_)];
    int cols = 0;
    int rows = 0;
    if (!detectTerrainGridDimensions(selectedObject.mesh, cols, rows))
        return false;
    if (selectedObject.primitive != LevelMeshPrimitive::Terrain)
        return false;

    if (outCols)
        *outCols = cols;
    if (outRows)
        *outRows = rows;
    return true;
}

static const char* kSettingsPath = "level_editor_settings.json";

void LevelEditorApp::SaveEditorSettings()
{
    nlohmann::json j;
    j["assetRoot"] = assetRoot_;
    j["scenePath"] = scenePath_;
    j["viewLayout"] = static_cast<int>(viewLayout_);
    j["currentTexturePath"] = currentTexturePath_;
    j["lastImportDir"] = lastImportDir_;
    j["lastTerrainHeightmapDir"] = lastTerrainHeightmapDir_;
    j["assetViewMode"] = static_cast<int>(assetViewMode_);
    j["theme"] = static_cast<int>(theme_);
    j["showGrid"] = showGrid_;
    j["snapEnabled"] = snapEnabled_;
    j["snapToGeometry"] = snapToGeometry_;
    j["gridSize"] = gridSize_;
    j["perspGridSize"] = perspGridSize_;
    j["useTransparency"] = useTransparency_;
    j["transparency"] = transparency_;
    j["cullMode"] = static_cast<int>(cullMode_);
    j["perspectiveMinDistance"] = perspectiveMinDistance_;
    j["perspectiveNearPlane"] = perspectiveNearPlane_;
    j["perspectiveFarPlane"] = perspectiveFarPlane_;
    j["faceHighlightFillEnabled"] = faceHighlightFillEnabled_;
    j["faceHighlightFillAlpha"] = faceHighlightFillAlpha_;
    j["selectionMode"] = static_cast<int>(selectionMode_);
    // View types
    nlohmann::json viewsJson = nlohmann::json::array();
    for (int i = 0; i < 4; ++i)
    {
        nlohmann::json v;
        v["type"] = static_cast<int>(views_[i].type);
        v["renderMode"] = static_cast<int>(views_[i].renderMode);
        v["orthoSize"] = views_[i].orthoSize;
        v["perspectiveYaw"] = views_[i].perspectiveYaw;
        v["perspectivePitch"] = views_[i].perspectivePitch;
        v["perspectiveDistance"] = views_[i].perspectiveDistance;
        v["focus"] = {views_[i].focus.x, views_[i].focus.y, views_[i].focus.z};
        v["clearColor"] = {views_[i].clearColor.r, views_[i].clearColor.g, views_[i].clearColor.b};
        viewsJson.push_back(v);
    }
    j["views"] = viewsJson;

    // Debug visualization
    j["debugDrawNormals"] = debugDrawNormals_;
    j["debugDrawTangents"] = debugDrawTangents_;
    j["debugNormalLength"] = debugNormalLength_;

    // Lightmap settings
    j["useLightmap"] = useLightmap_;
    j["lmResolution"] = lightmapSettings_.resolution;
    j["lmSamples"] = lightmapSettings_.samplesPerTexel;
    j["lmBias"] = lightmapSettings_.bias;
    j["lmAmbient"] = lightmapSettings_.ambient;

    // Section collapse states
    nlohmann::json secJson;
    for (const auto& [name, open] : sectionOpen_)
        secJson[name] = open;
    j["sections"] = secJson;

    std::ofstream out(kSettingsPath);
    if (out.is_open())
        out << j.dump(4);
}

void LevelEditorApp::LoadEditorSettings()
{
    std::ifstream in(kSettingsPath);
    if (!in.is_open())
        return;
    nlohmann::json j;
    try { j = nlohmann::json::parse(in); }
    catch (...) { return; }

    if (j.contains("assetRoot")) assetRoot_ = j["assetRoot"].get<std::string>();
    if (j.contains("scenePath")) scenePath_ = j["scenePath"].get<std::string>();
    if (j.contains("viewLayout"))
    {
        const int vl = j["viewLayout"].get<int>();
        if (vl >= 1 && vl <= 4) viewLayout_ = static_cast<ViewLayout>(vl);
    }
    if (j.contains("currentTexturePath")) currentTexturePath_ = j["currentTexturePath"].get<std::string>();
    if (j.contains("lastImportDir")) lastImportDir_ = j["lastImportDir"].get<std::string>();
    if (j.contains("lastTerrainHeightmapDir")) lastTerrainHeightmapDir_ = j["lastTerrainHeightmapDir"].get<std::string>();
    if (j.contains("assetViewMode"))
    {
        const int av = j["assetViewMode"].get<int>();
        if (av >= 0 && av <= 2) assetViewMode_ = static_cast<AssetViewMode>(av);
    }
    if (j.contains("theme"))
    {
        const int th = j["theme"].get<int>();
        if (th >= 0 && th <= 3) { theme_ = static_cast<LevelEditorTheme>(th); applyLevelEditorTheme(theme_); }
    }
    if (j.contains("showGrid")) showGrid_ = j["showGrid"].get<bool>();
    if (j.contains("snapEnabled")) snapEnabled_ = j["snapEnabled"].get<bool>();
    if (j.contains("snapToGeometry")) snapToGeometry_ = j["snapToGeometry"].get<bool>();
    if (j.contains("gridSize")) gridSize_ = j["gridSize"].get<float>();
    if (j.contains("perspGridSize")) perspGridSize_ = j["perspGridSize"].get<float>();
    if (j.contains("useTransparency")) useTransparency_ = j["useTransparency"].get<bool>();
    if (j.contains("transparency")) transparency_ = j["transparency"].get<float>();
    if (j.contains("cullMode"))
    {
        const int cm = j["cullMode"].get<int>();
        if (cm >= static_cast<int>(CullMode::Off) && cm <= static_cast<int>(CullMode::Back))
            cullMode_ = static_cast<CullMode>(cm);
    }
    else if (j.contains("disableBackfaceCulling"))
    {
        // Backward compatibility with previous boolean setting.
        cullMode_ = j["disableBackfaceCulling"].get<bool>() ? CullMode::Off : CullMode::Back;
    }
    if (j.contains("perspectiveMinDistance")) perspectiveMinDistance_ = j["perspectiveMinDistance"].get<float>();
    if (j.contains("perspectiveNearPlane")) perspectiveNearPlane_ = j["perspectiveNearPlane"].get<float>();
    if (j.contains("perspectiveFarPlane")) perspectiveFarPlane_ = j["perspectiveFarPlane"].get<float>();
    if (j.contains("faceHighlightFillEnabled")) faceHighlightFillEnabled_ = j["faceHighlightFillEnabled"].get<bool>();
    if (j.contains("faceHighlightFillAlpha")) faceHighlightFillAlpha_ = j["faceHighlightFillAlpha"].get<float>();
    perspectiveMinDistance_ = std::max(0.01f, perspectiveMinDistance_);
    perspectiveNearPlane_ = std::max(0.001f, perspectiveNearPlane_);
    perspectiveFarPlane_ = std::max(perspectiveNearPlane_ + 1.0f, perspectiveFarPlane_);
    if (j.contains("selectionMode"))
    {
        const int sm = j["selectionMode"].get<int>();
        if (sm >= 0 && sm <= 3) selectionMode_ = static_cast<SelectionMode>(sm);
    }
    if (j.contains("views") && j["views"].is_array())
    {
        const auto& arr = j["views"];
        for (int i = 0; i < 4 && i < static_cast<int>(arr.size()); ++i)
        {
            const auto& v = arr[i];
            if (v.contains("type")) views_[i].type = static_cast<ViewType>(v["type"].get<int>());
            if (v.contains("renderMode")) views_[i].renderMode = static_cast<RenderMode>(v["renderMode"].get<int>());
            if (v.contains("orthoSize")) views_[i].orthoSize = v["orthoSize"].get<float>();
            if (v.contains("perspectiveYaw")) views_[i].perspectiveYaw = v["perspectiveYaw"].get<float>();
            if (v.contains("perspectivePitch")) views_[i].perspectivePitch = v["perspectivePitch"].get<float>();
            if (v.contains("perspectiveDistance")) views_[i].perspectiveDistance = v["perspectiveDistance"].get<float>();
            views_[i].perspectiveDistance = std::max(views_[i].perspectiveDistance, perspectiveMinDistance_);
            if (v.contains("focus") && v["focus"].is_array() && v["focus"].size() == 3)
                views_[i].focus = glm::vec3(v["focus"][0].get<float>(), v["focus"][1].get<float>(), v["focus"][2].get<float>());
            if (v.contains("clearColor") && v["clearColor"].is_array() && v["clearColor"].size() == 3)
                views_[i].clearColor = glm::vec4(v["clearColor"][0].get<float>(), v["clearColor"][1].get<float>(), v["clearColor"][2].get<float>(), 1.0f);
        }
    }

    // Debug visualization
    if (j.contains("debugDrawNormals")) debugDrawNormals_ = j["debugDrawNormals"].get<bool>();
    if (j.contains("debugDrawTangents")) debugDrawTangents_ = j["debugDrawTangents"].get<bool>();
    if (j.contains("debugNormalLength")) debugNormalLength_ = j["debugNormalLength"].get<float>();

    // Lightmap settings
    if (j.contains("useLightmap")) useLightmap_ = j["useLightmap"].get<bool>();
    if (j.contains("lmResolution")) lightmapSettings_.resolution = j["lmResolution"].get<int>();
    if (j.contains("lmSamples")) lightmapSettings_.samplesPerTexel = j["lmSamples"].get<int>();
    if (j.contains("lmBias")) lightmapSettings_.bias = j["lmBias"].get<float>();
    if (j.contains("lmAmbient")) lightmapSettings_.ambient = j["lmAmbient"].get<float>();

    // Section collapse states
    if (j.contains("sections") && j["sections"].is_object())
    {
        for (auto& [key, val] : j["sections"].items())
            sectionOpen_[key] = val.get<bool>();
    }
}

// ============================================================
//  Export helpers
// ============================================================
namespace {
struct ExportVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec2 lightmapUv{0.0f, 0.0f};
};

// Collect all visible meshes into one merged vertex/index list, grouped by material.
// Each mesh's transform (position + rotation + scale) is baked into the vertices.
struct ExportData {
    std::vector<ExportVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::string> materialNames;
    struct SubMesh { uint32_t indexStart; uint32_t indexCount; int materialIdx; };
    std::vector<SubMesh> subMeshes;
    bool hasLightmapUVs = false;
};

ExportData gatherExportData(const LevelEditorScene& scene, const LightmapResult* lmResult = nullptr)
{
    ExportData data;
    std::map<std::string, int> matMap;

    const bool haveLM = lmResult && !lmResult->meshUVs.empty();
    data.hasLightmapUVs = haveLM;

    int meshIdx = -1; // index across ALL mesh objects (including hidden)
    for (int objIdx = 0; objIdx < static_cast<int>(scene.meshObjects().size()); ++objIdx)
    {
        const auto& obj = scene.meshObjects()[objIdx];
        meshIdx = objIdx; // lightmapResult_.meshUVs is indexed by meshObject index
        if (!obj.visible) continue;

        const glm::mat4 T = glm::translate(glm::mat4(1.0f), obj.position);
        const glm::mat4 R = glm::mat4_cast(glm::quat(glm::radians(obj.rotationEuler)));
        const glm::mat4 S = glm::scale(glm::mat4(1.0f), obj.scale);
        const glm::mat4 model = T * R * S;
        const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

        const auto& verts = obj.mesh.vertices();
        const auto& faces = obj.mesh.faces();

        // Group faces by material, emit unique vertices per face-vertex
        std::map<std::string, std::vector<uint32_t>> facesPerMat;
        for (int fi = 0; fi < static_cast<int>(faces.size()); ++fi)
        {
            const auto& face = faces[fi];
            // Emit one vertex per face-vertex (allows unique lightmap UVs per face)
            std::vector<uint32_t> faceVerts;
            for (int vi = 0; vi < static_cast<int>(face.indices.size()); ++vi)
            {
                int idx = face.indices[vi];
                if (idx < 0 || idx >= static_cast<int>(verts.size())) continue;
                const auto& sv = verts[idx];
                ExportVertex ev;
                ev.pos = glm::vec3(model * glm::vec4(sv.position, 1.0f));
                ev.normal = glm::normalize(normalMat * sv.normal);
                ev.uv = sv.uv;
                if (haveLM && meshIdx < static_cast<int>(lmResult->meshUVs.size()) &&
                    fi < static_cast<int>(lmResult->meshUVs[meshIdx].faceVertexUVs.size()) &&
                    vi < static_cast<int>(lmResult->meshUVs[meshIdx].faceVertexUVs[fi].size()))
                {
                    ev.lightmapUv = lmResult->meshUVs[meshIdx].faceVertexUVs[fi][vi];
                }
                faceVerts.push_back(static_cast<uint32_t>(data.vertices.size()));
                data.vertices.push_back(ev);
            }

            // Fan triangulate
            auto& idxList = facesPerMat[face.materialName];
            for (size_t k = 2; k < faceVerts.size(); ++k)
            {
                idxList.push_back(faceVerts[0]);
                idxList.push_back(faceVerts[k - 1]);
                idxList.push_back(faceVerts[k]);
            }
        }

        for (auto& [matName, idxList] : facesPerMat)
        {
            if (idxList.empty()) continue;
            int matIdx;
            auto it = matMap.find(matName);
            if (it != matMap.end()) {
                matIdx = it->second;
            } else {
                matIdx = static_cast<int>(data.materialNames.size());
                data.materialNames.push_back(matName);
                matMap[matName] = matIdx;
            }

            ExportData::SubMesh sm;
            sm.indexStart = static_cast<uint32_t>(data.indices.size());
            sm.indexCount = static_cast<uint32_t>(idxList.size());
            sm.materialIdx = matIdx;
            data.subMeshes.push_back(sm);
            data.indices.insert(data.indices.end(), idxList.begin(), idxList.end());
        }
    }

    return data;
}

glm::vec4 computeTangent(const ExportVertex& v0, const ExportVertex& v1, const ExportVertex& v2, const glm::vec3& normal)
{
    const glm::vec3 e1 = v1.pos - v0.pos;
    const glm::vec3 e2 = v2.pos - v0.pos;
    const glm::vec2 duv1 = v1.uv - v0.uv;
    const glm::vec2 duv2 = v2.uv - v0.uv;
    float denom = duv1.x * duv2.y - duv2.x * duv1.y;
    glm::vec3 t;
    if (std::abs(denom) < 1e-8f)
        t = glm::vec3(1, 0, 0);
    else {
        float r = 1.0f / denom;
        t = glm::normalize((e1 * duv2.y - e2 * duv1.y) * r);
    }
    // Gram-Schmidt
    t = glm::normalize(t - normal * glm::dot(normal, t));
    glm::vec3 b = glm::cross(normal, t);
    float w = (glm::dot(glm::cross(normal, t), b) < 0.0f) ? -1.0f : 1.0f;
    return glm::vec4(t, w);
}

} // anon namespace

bool LevelEditorApp::ExportSceneOBJ(const std::string& path)
{
    ExportData data = gatherExportData(scene_);
    if (data.vertices.empty()) return false;

    // Write .mtl file
    std::string mtlPath = path;
    if (mtlPath.size() > 4 && mtlPath.substr(mtlPath.size() - 4) == ".obj")
        mtlPath = mtlPath.substr(0, mtlPath.size() - 4) + ".mtl";
    else
        mtlPath += ".mtl";

    std::string mtlName = std::filesystem::path(mtlPath).filename().string();

    {
        std::ofstream mtl(mtlPath);
        if (!mtl.is_open()) return false;
        for (const auto& name : data.materialNames)
        {
            mtl << "newmtl " << name << "\n";
            mtl << "Kd 0.8 0.8 0.8\n";
            mtl << "d 1.0\n\n";
        }
    }

    // Write .obj file
    std::ofstream out(path);
    if (!out.is_open()) return false;

    out << "# Exported from Level Editor\n";
    out << "mtllib " << mtlName << "\n\n";

    // Vertices
    for (const auto& v : data.vertices)
        out << "v " << v.pos.x << " " << v.pos.y << " " << v.pos.z << "\n";
    out << "\n";

    // Normals
    for (const auto& v : data.vertices)
        out << "vn " << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";
    out << "\n";

    // UVs
    for (const auto& v : data.vertices)
        out << "vt " << v.uv.x << " " << v.uv.y << "\n";
    out << "\n";

    // Faces (grouped by material/submesh)
    for (const auto& sm : data.subMeshes)
    {
        out << "usemtl " << data.materialNames[sm.materialIdx] << "\n";
        for (uint32_t i = 0; i < sm.indexCount; i += 3)
        {
            // OBJ indices are 1-based
            const uint32_t a = data.indices[sm.indexStart + i] + 1;
            const uint32_t b = data.indices[sm.indexStart + i + 1] + 1;
            const uint32_t c = data.indices[sm.indexStart + i + 2] + 1;
            out << "f " << a << "/" << a << "/" << a
                << " " << b << "/" << b << "/" << b
                << " " << c << "/" << c << "/" << c << "\n";
        }
    }

    printf("[Export] OBJ saved: %s  (%zu verts, %zu tris)\n",
           path.c_str(), data.vertices.size(), data.indices.size() / 3);
    return true;
}

bool LevelEditorApp::ExportSceneH3D(const std::string& path)
{
    const bool haveLM = !lightmapResult_.pixels.empty();
    ExportData data = gatherExportData(scene_, haveLM ? &lightmapResult_ : nullptr);
    if (data.vertices.empty()) return false;

    BinaryStream stream(path, "wb");
    if (!stream.isOpen()) return false;

    stream.writeU32(MESH_MAGIC);
    stream.writeU32(MESH_VERSION);

    // MATS chunk
    {
        stream.writeU32(CHUNK_MATS);
        stream.writeU32(0); // placeholder
        Sint64 start = stream.tell();

        stream.writeU32(static_cast<uint32_t>(data.materialNames.size()));
        for (const auto& name : data.materialNames)
        {
            stream.writeStr(name);
            stream.writeF32(0.8f); stream.writeF32(0.8f); stream.writeF32(0.8f); // diffuse
            stream.writeStr(""); // texture ref
        }

        Sint64 end = stream.tell();
        stream.seek(start - 4);
        stream.writeU32(static_cast<uint32_t>(end - start));
        stream.seek(end);
    }

    // Precompute per-vertex tangents for H3D
    std::vector<glm::vec4> tangents(data.vertices.size(), glm::vec4(1, 0, 0, 1));
    for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
    {
        uint32_t i0 = data.indices[i], i1 = data.indices[i + 1], i2 = data.indices[i + 2];
        glm::vec4 t = computeTangent(data.vertices[i0], data.vertices[i1], data.vertices[i2], data.vertices[i0].normal);
        tangents[i0] = t;
        tangents[i1] = t;
        tangents[i2] = t;
    }

    // BUFF chunk
    {
        stream.writeU32(CHUNK_BUFF);
        stream.writeU32(0);
        Sint64 buffStart = stream.tell();

        stream.writeU32(BUFFER_FLAG_TANGENTS | (data.hasLightmapUVs ? BUFFER_FLAG_LIGHTMAP : 0));

        // VRTS
        {
            stream.writeU32(CHUNK_VRTS);
            stream.writeU32(0);
            Sint64 vrtsStart = stream.tell();

            stream.writeU32(static_cast<uint32_t>(data.vertices.size()));
            for (size_t i = 0; i < data.vertices.size(); ++i)
            {
                const auto& v = data.vertices[i];
                stream.writeF32(v.pos.x); stream.writeF32(v.pos.y); stream.writeF32(v.pos.z);
                stream.writeF32(v.normal.x); stream.writeF32(v.normal.y); stream.writeF32(v.normal.z);
                stream.writeF32(tangents[i].x); stream.writeF32(tangents[i].y);
                stream.writeF32(tangents[i].z); stream.writeF32(tangents[i].w);
                stream.writeF32(v.uv.x); stream.writeF32(v.uv.y);
                if (data.hasLightmapUVs) {
                    stream.writeF32(v.lightmapUv.x); stream.writeF32(v.lightmapUv.y);
                }
            }

            Sint64 vrtsEnd = stream.tell();
            stream.seek(vrtsStart - 4);
            stream.writeU32(static_cast<uint32_t>(vrtsEnd - vrtsStart));
            stream.seek(vrtsEnd);
        }

        // IDXS
        {
            stream.writeU32(CHUNK_IDXS);
            stream.writeU32(0);
            Sint64 idxsStart = stream.tell();

            stream.writeU32(static_cast<uint32_t>(data.indices.size()));
            for (auto idx : data.indices) stream.writeU32(idx);

            Sint64 idxsEnd = stream.tell();
            stream.seek(idxsStart - 4);
            stream.writeU32(static_cast<uint32_t>(idxsEnd - idxsStart));
            stream.seek(idxsEnd);
        }

        // SURF
        {
            stream.writeU32(CHUNK_SURF);
            stream.writeU32(0);
            Sint64 surfStart = stream.tell();

            stream.writeU32(static_cast<uint32_t>(data.subMeshes.size()));
            for (const auto& sm : data.subMeshes)
            {
                stream.writeU32(sm.indexStart);
                stream.writeU32(sm.indexCount);
                stream.writeI32(sm.materialIdx);
            }

            Sint64 surfEnd = stream.tell();
            stream.seek(surfStart - 4);
            stream.writeU32(static_cast<uint32_t>(surfEnd - surfStart));
            stream.seek(surfEnd);
        }

        Sint64 buffEnd = stream.tell();
        stream.seek(buffStart - 4);
        stream.writeU32(static_cast<uint32_t>(buffEnd - buffStart));
        stream.seek(buffEnd);
    }

    // LMAP chunk — embedded lightmap texture (RGB)
    if (data.hasLightmapUVs && !lightmapResult_.pixels.empty())
    {
        stream.writeU32(CHUNK_LMAP);
        stream.writeU32(0);
        Sint64 lmStart = stream.tell();

        stream.writeI32(lightmapResult_.width);
        stream.writeI32(lightmapResult_.height);
        stream.writeI32(3); // channels (RGB)
        stream.writeRaw(lightmapResult_.pixels.data(), lightmapResult_.pixels.size());

        Sint64 lmEnd = stream.tell();
        stream.seek(lmStart - 4);
        stream.writeU32(static_cast<uint32_t>(lmEnd - lmStart));
        stream.seek(lmEnd);
    }

    printf("[Export] H3D saved: %s  (%zu verts, %zu tris, %zu mats, %zu surfs%s)\n",
           path.c_str(), data.vertices.size(), data.indices.size() / 3,
           data.materialNames.size(), data.subMeshes.size(),
           data.hasLightmapUVs ? ", +lightmap" : "");
    return true;
}

LevelEditorApp::LevelEditorApp()
{
    applyLevelEditorTheme(theme_);
    InitializeViews();
    scenePath_ = "scenes/level_scene.mred";
    LoadEditorSettings();

    // Auto-load last scene if path exists
    if (!scenePath_.empty() && std::filesystem::exists(scenePath_))
    {
        if (LoadSceneFromPath(scenePath_))
            sceneStatusMessage_ = "Loaded: " + scenePath_;
        sceneDirty_ = false;
    }

    RescanAssets();
    SyncSelectedMeshes();

    viewBatch_ = std::make_unique<RenderBatch>();
    viewBatch_->Init();

    // Unified editor shader: solid color OR textured, controlled by u_useTexture
    {
        const char* vert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_viewProj;
uniform mat4 u_model;
out vec3 v_normal;
out vec2 v_uv;
out vec2 v_lmUv;
void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_normal = mat3(u_model) * a_normal;
    v_uv = a_uv;
    v_lmUv = a_tangent.xy;
    gl_Position = u_viewProj * worldPos;
})";
        const char* frag = R"(#version 300 es
precision highp float;
out vec4 FragColor;
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_lmUv;
uniform vec4 u_color;
uniform int u_useTexture;
uniform int u_useLightmap;
uniform sampler2D u_albedo;
uniform sampler2D u_lightmap;
void main() {
    vec4 base;
    if (u_useTexture != 0) {
        base = texture(u_albedo, v_uv);
        base.a *= u_color.a;
    } else {
        base = u_color;
    }
    if (u_useLightmap != 0) {
        vec3 lm = texture(u_lightmap, v_lmUv).rgb;
        base.rgb *= lm;
    }
    FragColor = base;
})";
        solidShader_ = ShaderManager::instance().loadFromSource("editor_solid", vert, frag);
    }

    // Load glPolygonMode (desktop GL, not in GLES glad headers)
    if (!s_glPolygonMode)
        s_glPolygonMode = (void(*)(GLenum, GLenum))SDL_GL_GetProcAddress("glPolygonMode");
}

LevelEditorApp::~LevelEditorApp()
{
    // Wait for any in-progress bake
    if (bakeThread_ && bakeThread_->joinable())
        bakeThread_->join();
    // Auto-save scene on exit
    if (!scenePath_.empty())
        SaveSceneToPath(scenePath_, false);
    SaveEditorSettings();
    InvalidateMeshCache();
    if (viewBatch_)
        viewBatch_->Release();
    for (auto& view : views_)
    {
        delete view.rt;
        view.rt = nullptr;
    }
}

void LevelEditorApp::RenderFrame(float deltaTime)
{
    // Check if async bake finished
    if (bakeRunning_ && bakeProgress_.load() >= 1.0f)
        FinishBakeAsync();

    if (ImGuiViewport* mainViewport = ImGui::GetMainViewport())
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            mainViewport->Pos,
            ImVec2(mainViewport->Pos.x + mainViewport->Size.x, mainViewport->Pos.y + mainViewport->Size.y),
            IM_COL32(18, 20, 24, 255));
    }

    ImGuizmo::BeginFrame();
    HandleUndoRedoShortcuts();
    HandleToolShortcuts();

    // Entity preview animation
    if (entityPreviewActive_ && entityPreviewIndex_ >= 0 &&
        entityPreviewIndex_ < static_cast<int>(scene_.entities().size()))
    {
        const LevelEntityObject& ent = scene_.entities()[(size_t)entityPreviewIndex_];
        const float speed = 0.5f; // 0→1 in 2 seconds (full cycle ~4s)

        if (entityPreviewForward_)
        {
            entityPreviewTime_ += deltaTime * speed;
            if (entityPreviewTime_ >= 1.0f) { entityPreviewTime_ = 1.0f; entityPreviewForward_ = false; }
        }
        else
        {
            entityPreviewTime_ -= deltaTime * speed;
            if (entityPreviewTime_ <= 0.0f) { entityPreviewTime_ = 0.0f; entityPreviewForward_ = true; }
        }

        // Smooth ease in/out
        const float t = entityPreviewTime_ * entityPreviewTime_ * (3.0f - 2.0f * entityPreviewTime_);

        if (ent.linkedMeshIndex >= 0 && ent.linkedMeshIndex < static_cast<int>(scene_.meshObjects().size()))
        {
            LevelMeshObject& mesh = scene_.meshObjects()[(size_t)ent.linkedMeshIndex];

            if (ent.type == LevelEntityType::Door)
            {
                if (ent.doorType == DoorType::Slide || ent.doorType == DoorType::Shutter)
                {
                    glm::vec3 dir = glm::length(ent.direction) > 0.01f ? glm::normalize(ent.direction) : glm::vec3(1, 0, 0);
                    mesh.position = entityPreviewOrigPos_ + dir * ent.doorDistance * t;
                }
                else if (ent.doorType == DoorType::Turn)
                {
                    mesh.rotationEuler = entityPreviewOrigRot_ + glm::vec3(0, ent.doorDistance * t, 0);
                }
            }
            else if (ent.type == LevelEntityType::Elevator || ent.type == LevelEntityType::Platform)
            {
                mesh.position = glm::mix(entityPreviewOrigPos_, ent.endPosition, t);
            }
            sceneDirty_ = true;
        }
    }

    ShowMenuBar();
    UpdatePanelLayout();
    ShowLeftPanel();
    ShowCenterPanel();
    ShowAssetsPanel();
    ShowRightPanel();
    ShowUvMappingWindow();
    ShowStatusBar(deltaTime);
    HandleFileDialogs();
}

void LevelEditorApp::UpdatePanelLayout()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float padding = 8.0f;
    const float gap = 8.0f;
    const float statusBarHeight = 28.0f;
    const float menuBarHeight = ImGui::GetFrameHeight();

    const float totalWidth = std::max(640.0f, viewport->Size.x - padding * 2.0f);
    const float topY = viewport->Pos.y + menuBarHeight + padding;
    const float height = std::max(180.0f, viewport->Size.y - menuBarHeight - statusBarHeight - padding * 2.0f);

    float leftWidth = std::clamp(totalWidth * 0.22f, 220.0f, 360.0f);
    float rightWidth = std::clamp(totalWidth * 0.24f, 260.0f, 420.0f);
    float centerWidth = totalWidth - leftWidth - rightWidth - gap * 2.0f;

    if (centerWidth < 320.0f)
    {
        float deficit = 320.0f - centerWidth;
        const float rightShrink = std::min(deficit, rightWidth - 260.0f);
        rightWidth -= rightShrink;
        deficit -= rightShrink;

        const float leftShrink = std::min(deficit, leftWidth - 220.0f);
        leftWidth -= leftShrink;
        deficit -= leftShrink;

        centerWidth = totalWidth - leftWidth - rightWidth - gap * 2.0f;
    }

    const float leftX = viewport->Pos.x + padding;
    const float centerX = leftX + leftWidth + gap;
    const float rightX = centerX + centerWidth + gap;

    leftPanelPos_ = ImVec2(leftX, topY);
    leftPanelSize_ = ImVec2(leftWidth, height);
    const float panelGap = 8.0f;
    const float assetHeight = std::clamp(height * 0.24f, 140.0f, 260.0f);
    const float centerHeight = std::max(180.0f, height - assetHeight - panelGap);
    centerPanelPos_ = ImVec2(centerX, topY);
    centerPanelSize_ = ImVec2(centerWidth, centerHeight);
    assetPanelPos_ = ImVec2(centerX, topY + centerHeight + panelGap);
    assetPanelSize_ = ImVec2(centerWidth, height - centerHeight - panelGap);
    rightPanelPos_ = ImVec2(rightX, topY);
    rightPanelSize_ = ImVec2(rightWidth, height);
}

void LevelEditorApp::RescanAssets()
{
    assets_.clear();
    std::error_code ec;
    std::filesystem::path root = std::filesystem::path(assetRoot_);
    if (root.is_relative())
        root = std::filesystem::current_path(ec) / root;
    if (ec || !std::filesystem::exists(root))
        return;

    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    {
        if (ec || !it->is_regular_file())
            continue;

        const std::string path = it->path().generic_string();
        if (!hasImageExtension(path))
            continue;

        AssetEntry entry;
        entry.name = it->path().filename().string();
        entry.path = path;
        assets_.push_back(entry);
    }

    std::sort(assets_.begin(), assets_.end(), [](const AssetEntry& a, const AssetEntry& b)
    {
        return a.path < b.path;
    });
}

bool LevelEditorApp::SaveSceneToPath(const std::string& path, bool setAsCurrentPath)
{
    std::filesystem::path savePath = ensureSceneExtension(std::filesystem::path(path));
    std::string error;
    scene_.assetRoot() = assetRoot_.empty() ? std::string("assets") : assetRoot_;
    if (!saveLevelEditorScene(savePath, scene_, error))
    {
        sceneStatusMessage_ = "Save failed: " + error;
        return false;
    }

    if (setAsCurrentPath)
        scenePath_ = savePath.generic_string();
    sceneDirty_ = false;
    sceneStatusMessage_ = "Saved: " + savePath.generic_string();
    return true;
}

bool LevelEditorApp::IsMeshSelected(int index) const
{
    return std::find(selectedMeshIndices_.begin(), selectedMeshIndices_.end(), index) != selectedMeshIndices_.end();
}

bool LevelEditorApp::IsVertexSelected(int index) const
{
    return std::find(selectedVertexIndices_.begin(), selectedVertexIndices_.end(), index) != selectedVertexIndices_.end();
}

bool LevelEditorApp::IsFaceSelected(int index) const
{
    return std::find(selectedFaceIndices_.begin(), selectedFaceIndices_.end(), index) != selectedFaceIndices_.end();
}

void LevelEditorApp::SyncCurrentTextureFromSelection()
{
    currentTexturePath_.clear();

    if (selectedMeshIndex_ < 0 || selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
        return;

    const LevelMeshObject& object = scene_.meshObjects()[(size_t)selectedMeshIndex_];
    auto acceptMaterial = [&](const std::string& materialName) -> bool
    {
        if (materialName.empty() || materialName == "default")
            return false;
        currentTexturePath_ = materialName;
        return true;
    };

    if (selectionMode_ == SelectionMode::Face &&
        selectedFaceIndex_ >= 0 &&
        selectedFaceIndex_ < static_cast<int>(object.mesh.faceCount()))
    {
        const EditableFace& face = object.mesh.faces()[(size_t)selectedFaceIndex_];
        if (acceptMaterial(face.materialName))
            return;
    }

    for (const EditableFace& face : object.mesh.faces())
    {
        if (acceptMaterial(face.materialName))
            return;
    }
}

void LevelEditorApp::SetSingleSelectedMesh(int index)
{
    const int meshCount = static_cast<int>(scene_.meshObjects().size());
    if (meshCount <= 0)
    {
        selectedMeshIndex_ = -1;
        selectedMeshIndices_.clear();
        SyncCurrentTextureFromSelection();
        return;
    }

    selectedMeshIndex_ = std::clamp(index, 0, meshCount - 1);
    selectedMeshIndices_.assign(1, selectedMeshIndex_);
    selectedVertexIndices_.clear();
    selectedFaceIndex_ = -1;
    selectedFaceIndices_.clear();
    selectedEntityIndex_ = -1; // deselect entity when selecting mesh
    SyncCurrentTextureFromSelection();
}

void LevelEditorApp::SyncSelectedMeshes()
{
    const int meshCount = static_cast<int>(scene_.meshObjects().size());
    if (meshCount <= 0)
    {
        selectedMeshIndex_ = -1;
        selectedMeshIndices_.clear();
        SyncCurrentTextureFromSelection();
        return;
    }

    selectedMeshIndex_ = std::clamp(selectedMeshIndex_, 0, meshCount - 1);
    selectedMeshIndices_.erase(
        std::remove_if(selectedMeshIndices_.begin(), selectedMeshIndices_.end(),
            [meshCount](int index) { return index < 0 || index >= meshCount; }),
        selectedMeshIndices_.end());
    std::sort(selectedMeshIndices_.begin(), selectedMeshIndices_.end());
    selectedMeshIndices_.erase(std::unique(selectedMeshIndices_.begin(), selectedMeshIndices_.end()), selectedMeshIndices_.end());

    if (!IsMeshSelected(selectedMeshIndex_))
        selectedMeshIndices_.insert(selectedMeshIndices_.begin(), selectedMeshIndex_);

    if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < meshCount)
    {
        const int vertexCount = static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.vertexCount());
        selectedVertexIndices_.erase(
            std::remove_if(selectedVertexIndices_.begin(), selectedVertexIndices_.end(),
                [vertexCount](int index) { return index < 0 || index >= vertexCount; }),
            selectedVertexIndices_.end());
        std::sort(selectedVertexIndices_.begin(), selectedVertexIndices_.end());
        selectedVertexIndices_.erase(std::unique(selectedVertexIndices_.begin(), selectedVertexIndices_.end()), selectedVertexIndices_.end());
    }
    else
    {
        selectedVertexIndices_.clear();
        selectedFaceIndex_ = -1;
        selectedFaceIndices_.clear();
    }

    SyncCurrentTextureFromSelection();
}

void LevelEditorApp::SetSingleSelectedFace(int index)
{
    if (selectedMeshIndex_ < 0 || selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
    {
        selectedFaceIndex_ = -1;
        selectedFaceIndices_.clear();
        SyncCurrentTextureFromSelection();
        return;
    }

    const int faceCount = static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faceCount());
    if (faceCount <= 0)
    {
        selectedFaceIndex_ = -1;
        selectedFaceIndices_.clear();
        SyncCurrentTextureFromSelection();
        return;
    }

    selectedFaceIndex_ = std::clamp(index, 0, faceCount - 1);
    selectedFaceIndices_.assign(1, selectedFaceIndex_);
    selectedVertexIndices_.clear();
    selectedEntityIndex_ = -1;
    SyncCurrentTextureFromSelection();
}

void LevelEditorApp::SyncSelectedFaces()
{
    if (selectedMeshIndex_ < 0 || selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
    {
        selectedFaceIndex_ = -1;
        selectedFaceIndices_.clear();
        SyncCurrentTextureFromSelection();
        return;
    }

    const int faceCount = static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faceCount());
    if (faceCount <= 0)
    {
        selectedFaceIndex_ = -1;
        selectedFaceIndices_.clear();
        SyncCurrentTextureFromSelection();
        return;
    }

    selectedFaceIndices_.erase(
        std::remove_if(selectedFaceIndices_.begin(), selectedFaceIndices_.end(),
            [faceCount](int index) { return index < 0 || index >= faceCount; }),
        selectedFaceIndices_.end());
    std::sort(selectedFaceIndices_.begin(), selectedFaceIndices_.end());
    selectedFaceIndices_.erase(std::unique(selectedFaceIndices_.begin(), selectedFaceIndices_.end()), selectedFaceIndices_.end());

    if (selectedFaceIndex_ < 0)
    {
        if (selectedFaceIndices_.empty())
        {
            selectedFaceIndex_ = -1;
            return;
        }
        selectedFaceIndex_ = selectedFaceIndices_.front();
    }
    else
    {
        selectedFaceIndex_ = std::clamp(selectedFaceIndex_, 0, faceCount - 1);
        if (!IsFaceSelected(selectedFaceIndex_))
            selectedFaceIndices_.insert(selectedFaceIndices_.begin(), selectedFaceIndex_);
    }

    selectedVertexIndices_.clear();
    selectedEntityIndex_ = -1;
    SyncCurrentTextureFromSelection();
}

namespace
{
void toggleIndexSelection(std::vector<int>& indices, int index)
{
    const auto it = std::find(indices.begin(), indices.end(), index);
    if (it != indices.end())
        indices.erase(it);
    else
        indices.push_back(index);
}

void addUniqueIndices(std::vector<int>& dst, const std::vector<int>& src)
{
    for (int index : src)
    {
        if (std::find(dst.begin(), dst.end(), index) == dst.end())
            dst.push_back(index);
    }
}

bool parseLegacyImportedTextureKey(const std::string& materialName,
                                   std::string& outMeshName,
                                   std::string& outMeshFilename)
{
    constexpr const char* prefix = "import::";
    constexpr const char* slotMarker = "::tex_";
    if (materialName.rfind(prefix, 0) != 0)
        return false;

    const std::size_t slotPos = materialName.rfind(slotMarker);
    if (slotPos == std::string::npos || slotPos <= std::strlen(prefix))
        return false;

    outMeshFilename = materialName.substr(std::strlen(prefix), slotPos - std::strlen(prefix));
    if (outMeshFilename.empty())
        return false;

    outMeshName = std::string(prefix) + outMeshFilename;
    return true;
}

std::string findFileRecursiveByName(const std::filesystem::path& root, const std::string& fileName)
{
    if (root.empty() || fileName.empty())
        return std::string();

    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
        return std::string();

    const std::filesystem::path direct = root / fileName;
    if (std::filesystem::exists(direct, ec) && std::filesystem::is_regular_file(direct, ec))
        return direct.generic_string();

    std::filesystem::recursive_directory_iterator it(root,
        std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        if (it->path().filename() == fileName)
            return it->path().generic_string();
    }
    return std::string();
}

std::string resolveLegacyImportedTexturePath(const std::string& materialName,
                                            const std::string& assetRoot,
                                            const std::string& lastImportDir)
{
    std::string meshName;
    std::string meshFileName;
    if (!parseLegacyImportedTextureKey(materialName, meshName, meshFileName))
        return std::string();

    std::string meshPath;
    if (!lastImportDir.empty())
    {
        meshPath = findFileRecursiveByName(std::filesystem::path(lastImportDir), meshFileName);
        if (meshPath.empty())
        {
            const std::filesystem::path direct = std::filesystem::path(lastImportDir) / meshFileName;
            if (std::filesystem::exists(direct))
                meshPath = direct.generic_string();
        }
    }

    if (meshPath.empty() && !assetRoot.empty())
        meshPath = findFileRecursiveByName(std::filesystem::path(assetRoot), meshFileName);

    if (meshPath.empty())
        return std::string();

    const std::string textureDir = std::filesystem::path(meshPath).parent_path().generic_string();
    MeshManager::instance().load(meshName, meshPath, textureDir);

    Texture* tex = findTextureByMaterialRef(TextureManager::instance(), materialName);
    if (tex && !tex->name.empty())
        return tex->name;
    if (tex && !tex->sourcePath.empty())
        return tex->sourcePath;
    return std::string();
}

int preloadLegacyImportedMaterials(const LevelEditorScene& scene,
                                   const std::string& assetRoot,
                                   const std::string& lastImportDir)
{
    int loadedCount = 0;
    for (const LevelMeshObject& object : scene.meshObjects())
    {
        std::unordered_set<std::string> uniqueMaterials;
        for (const EditableFace& face : object.mesh.faces())
        {
            std::string meshName;
            std::string meshFileName;
            if (!parseLegacyImportedTextureKey(face.materialName, meshName, meshFileName))
                continue;
            uniqueMaterials.insert(face.materialName);
        }

        for (const std::string& materialName : uniqueMaterials)
        {
            if (!resolveLegacyImportedTexturePath(materialName, assetRoot, lastImportDir).empty())
                ++loadedCount;
        }
    }
    return loadedCount;
}

bool LoadHeightmapImage(const std::string& path,
                        std::vector<float>& outHeights,
                        int& outWidth,
                        int& outHeight,
                        std::string& outError)
{
    outHeights.clear();
    outWidth = 0;
    outHeight = 0;
    outError.clear();

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (!pixels)
    {
        outError = stbi_failure_reason() ? stbi_failure_reason() : "unknown image load error";
        return false;
    }

    if (width < 2 || height < 2 || channels < 1)
    {
        stbi_image_free(pixels);
        outError = "heightmap must be at least 2x2";
        return false;
    }

    outHeights.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t srcIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x))
                * static_cast<std::size_t>(channels);
            float value = 0.0f;
            if (channels == 1 || channels == 2)
            {
                value = static_cast<float>(pixels[srcIndex]) / 255.0f;
            }
            else
            {
                const float r = static_cast<float>(pixels[srcIndex + 0]) / 255.0f;
                const float g = static_cast<float>(pixels[srcIndex + 1]) / 255.0f;
                const float b = static_cast<float>(pixels[srcIndex + 2]) / 255.0f;
                value = r * 0.299f + g * 0.587f + b * 0.114f;
            }
            outHeights[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = value;
        }
    }

    stbi_image_free(pixels);
    outWidth = width;
    outHeight = height;
    return true;
}

bool detectTerrainGridDimensions(const EditableMesh& mesh, int& outCols, int& outRows)
{
    outCols = 0;
    outRows = 0;
    const auto& vertices = mesh.vertices();
    const auto& faces = mesh.faces();
    if (vertices.size() < 4 || faces.empty())
        return false;

    for (const EditableFace& face : faces)
    {
        if (face.indices.size() != 4)
            return false;
    }

    const float firstZ = vertices.front().position.z;
    constexpr float eps = 1e-3f;
    int cols = 0;
    while (cols < static_cast<int>(vertices.size()) &&
           std::fabs(vertices[static_cast<std::size_t>(cols)].position.z - firstZ) <= eps)
    {
        ++cols;
    }
    if (cols < 2)
        return false;
    if (vertices.size() % static_cast<std::size_t>(cols) != 0)
        return false;

    const int rows = static_cast<int>(vertices.size() / static_cast<std::size_t>(cols));
    if (rows < 2)
        return false;
    if (static_cast<int>(faces.size()) != (cols - 1) * (rows - 1))
        return false;

    outCols = cols;
    outRows = rows;
    return true;
}

float sampleTerrainHeightLocal(const EditableMesh& mesh, int cols, int rows, float x, float z)
{
    const auto& vertices = mesh.vertices();
    if (vertices.empty() || cols < 2 || rows < 2)
        return 0.0f;

    const float minX = vertices.front().position.x;
    const float minZ = vertices.front().position.z;
    const float maxX = vertices[static_cast<std::size_t>(cols - 1)].position.x;
    const float maxZ = vertices[static_cast<std::size_t>((rows - 1) * cols)].position.z;
    const float cellX = (maxX - minX) / static_cast<float>(cols - 1);
    const float cellZ = (maxZ - minZ) / static_cast<float>(rows - 1);
    if (std::fabs(cellX) <= 1e-6f || std::fabs(cellZ) <= 1e-6f)
        return vertices.front().position.y;

    const float fx = glm::clamp((x - minX) / cellX, 0.0f, static_cast<float>(cols - 1));
    const float fz = glm::clamp((z - minZ) / cellZ, 0.0f, static_cast<float>(rows - 1));
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, cols - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, rows - 1);
    const int x1 = std::min(x0 + 1, cols - 1);
    const int z1 = std::min(z0 + 1, rows - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    const float h00 = vertices[static_cast<std::size_t>(z0 * cols + x0)].position.y;
    const float h10 = vertices[static_cast<std::size_t>(z0 * cols + x1)].position.y;
    const float h01 = vertices[static_cast<std::size_t>(z1 * cols + x0)].position.y;
    const float h11 = vertices[static_cast<std::size_t>(z1 * cols + x1)].position.y;
    const float hx0 = glm::mix(h00, h10, tx);
    const float hx1 = glm::mix(h01, h11, tx);
    return glm::mix(hx0, hx1, tz);
}

void recomputeEditableMeshNormals(EditableMesh& mesh)
{
    auto& vertices = mesh.verticesMutable();
    for (EditableVertex& vertex : vertices)
        vertex.normal = glm::vec3(0.0f);

    for (const EditableFace& face : mesh.faces())
    {
        if (face.indices.size() < 3)
            continue;

        const glm::vec3& origin = vertices[static_cast<std::size_t>(face.indices[0])].position;
        glm::vec3 faceNormal(0.0f);
        for (std::size_t i = 1; i + 1 < face.indices.size(); ++i)
        {
            const glm::vec3& b = vertices[static_cast<std::size_t>(face.indices[i])].position;
            const glm::vec3& c = vertices[static_cast<std::size_t>(face.indices[i + 1])].position;
            faceNormal += glm::cross(b - origin, c - origin);
        }

        if (glm::length2(faceNormal) <= 1e-10f)
            continue;

        for (int index : face.indices)
            vertices[static_cast<std::size_t>(index)].normal += faceNormal;
    }

    for (EditableVertex& vertex : vertices)
    {
        if (glm::length2(vertex.normal) > 1e-10f)
            vertex.normal = glm::normalize(vertex.normal);
        else
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void applyTerrainBrushStroke(EditableMesh& mesh,
                             int cols,
                             int rows,
                             const glm::vec3& centerLocal,
                             float radius,
                             float strength,
                             float flattenHeight,
                             LevelEditorApp::TerrainSculptMode mode)
{
    auto& vertices = mesh.verticesMutable();
    if (vertices.empty() || radius <= 0.0f)
        return;

    std::vector<float> sourceHeights(vertices.size(), 0.0f);
    for (std::size_t i = 0; i < vertices.size(); ++i)
        sourceHeights[i] = vertices[i].position.y;

    const float radiusSq = radius * radius;
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const int index = row * cols + col;
            EditableVertex& vertex = vertices[static_cast<std::size_t>(index)];
            const float dx = vertex.position.x - centerLocal.x;
            const float dz = vertex.position.z - centerLocal.z;
            const float distSq = dx * dx + dz * dz;
            if (distSq > radiusSq)
                continue;

            const float dist = std::sqrt(distSq);
            const float falloff = 1.0f - (dist / radius);
            const float weight = falloff * falloff * (3.0f - 2.0f * falloff);

            switch (mode)
            {
            case LevelEditorApp::TerrainSculptMode::Raise:
                vertex.position.y += strength * weight;
                break;
            case LevelEditorApp::TerrainSculptMode::Lower:
                vertex.position.y -= strength * weight;
                break;
            case LevelEditorApp::TerrainSculptMode::Smooth:
            {
                float sum = sourceHeights[static_cast<std::size_t>(index)];
                float count = 1.0f;
                if (col > 0) { sum += sourceHeights[static_cast<std::size_t>(index - 1)]; count += 1.0f; }
                if (col + 1 < cols) { sum += sourceHeights[static_cast<std::size_t>(index + 1)]; count += 1.0f; }
                if (row > 0) { sum += sourceHeights[static_cast<std::size_t>(index - cols)]; count += 1.0f; }
                if (row + 1 < rows) { sum += sourceHeights[static_cast<std::size_t>(index + cols)]; count += 1.0f; }
                const float average = sum / count;
                const float blend = glm::clamp(strength * 0.1f * weight, 0.0f, 1.0f);
                vertex.position.y = glm::mix(sourceHeights[static_cast<std::size_t>(index)], average, blend);
                break;
            }
            case LevelEditorApp::TerrainSculptMode::Flatten:
            {
                const float blend = glm::clamp(strength * 0.1f * weight, 0.0f, 1.0f);
                vertex.position.y = glm::mix(sourceHeights[static_cast<std::size_t>(index)], flattenHeight, blend);
                break;
            }
            }
        }
    }

    recomputeEditableMeshNormals(mesh);
}
}

bool LevelEditorApp::LoadSceneFromPath(const std::string& path)
{
    const std::filesystem::path sceneFilePath = ensureSceneExtension(std::filesystem::path(path));
    std::string error;
    LevelEditorScene loaded;
    if (!loadLevelEditorScene(sceneFilePath, loaded, error))
    {
        sceneStatusMessage_ = "Load failed: " + error;
        return false;
    }

    scene_ = loaded;
    assetRoot_ = resolveSceneRelativePath(scene_.assetRoot().empty() ? std::string("assets") : scene_.assetRoot(),
                                          sceneFilePath);
    scene_.assetRoot() = assetRoot_;
    RescanAssets();
    const int preloadedLegacyMaterials = preloadLegacyImportedMaterials(scene_, assetRoot_, lastImportDir_);
    scenePath_ = sceneFilePath.generic_string();
    sceneDirty_ = false;
    undoStack_.clear();
    redoStack_.clear();
    selectedMeshIndex_ = std::clamp(selectedMeshIndex_, 0, std::max(0, static_cast<int>(scene_.meshObjects().size()) - 1));
    SyncSelectedMeshes();
    SyncSelectedFaces();
    selectedEntityIndex_ = std::clamp(selectedEntityIndex_, 0, std::max(0, static_cast<int>(scene_.entities().size()) - 1));
    meshCacheValid_ = false;
    sceneStatusMessage_ = "Loaded: " + scenePath_;
    if (preloadedLegacyMaterials > 0)
        sceneStatusMessage_ += " (loaded " + std::to_string(preloadedLegacyMaterials) + " legacy texture sets)";
    return true;
}

void LevelEditorApp::HandleFileDialogs()
{
    if (assetFolderDialog_.HasResult())
    {
        const auto result = assetFolderDialog_.ConsumeResult();
        if (result.accepted)
        {
            assetRoot_ = result.path.generic_string();
            RescanAssets();
        }
    }
    ImGui::PushID("AssetFolderDialog");
    assetFolderDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
    ImGui::PopID();

    if (sceneDialog_.HasResult())
    {
        const auto result = sceneDialog_.ConsumeResult();
        if (result.accepted)
        {
            if (result.mode == ImGuiFileDialog::Mode::OpenFile)
            {
                LoadSceneFromPath(result.path.generic_string());
            }
            else if (result.mode == ImGuiFileDialog::Mode::SaveFile)
            {
                SaveSceneToPath(result.path.generic_string(), true);
            }
        }
    }
    ImGui::PushID("SceneDialog");
    sceneDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
    ImGui::PopID();

    if (importMeshDialog_.HasResult())
    {
        const auto result = importMeshDialog_.ConsumeResult();
        if (result.accepted)
        {
            const std::string meshPath = result.path.generic_string();
            const std::string meshDir = result.path.parent_path().generic_string();
            const std::string meshName = "import::" + result.path.filename().generic_string();
            lastImportDir_ = meshDir;

            // Use assetRoot_ as texture directory if set, otherwise fall back to mesh directory
            const std::string textureDir = assetRoot_.empty() ? meshDir : assetRoot_;
            Mesh* loaded = MeshManager::instance().load(meshName, meshPath, textureDir);
            if (loaded && loaded->buffer.vertices.size() > 0)
            {
                PushUndoState();
                EditableMesh editable = makeEditableFromRenderMesh(*loaded, glm::mat4(1.0f));

                LevelMeshObject object;
                object.name = result.path.stem().generic_string();
                object.primitive = LevelMeshPrimitive::Imported;
                object.mesh = editable;
                scene_.meshObjects().push_back(object);
                SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
                meshCacheValid_ = false;
                sceneDirty_ = true;
                sceneStatusMessage_ = "Imported: " + meshPath
                    + " (" + std::to_string(editable.vertexCount()) + " verts, "
                    + std::to_string(editable.faceCount()) + " faces)";

                // Don't unload - textures in TextureManager are referenced by materialName
                MeshManager::instance().unload(meshName);
            }
            else
            {
                sceneStatusMessage_ = "Import failed: " + meshPath;
            }
        }
    }
    ImGui::PushID("ImportMeshDialog");
    importMeshDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
    ImGui::PopID();

    if (terrainHeightmapDialog_.HasResult())
    {
        const auto result = terrainHeightmapDialog_.ConsumeResult();
        if (result.accepted)
        {
            std::vector<float> heights;
            int width = 0;
            int height = 0;
            std::string error;
            const std::string imagePath = result.path.generic_string();
            if (LoadHeightmapImage(imagePath, heights, width, height, error))
            {
                primHeightmapPath_ = imagePath;
                lastTerrainHeightmapDir_ = result.path.parent_path().generic_string();
                primSubdivX_ = std::max(1, width - 1);
                primSubdivZ_ = std::max(1, height - 1);
                sceneStatusMessage_ = "Heightmap selected: " + result.path.filename().generic_string() +
                    " (" + std::to_string(width) + "x" + std::to_string(height) + ")";
            }
            else
            {
                primHeightmapPath_.clear();
                sceneStatusMessage_ = "Heightmap load failed: " + error;
            }
        }
    }
    ImGui::PushID("TerrainHeightmapDialog");
    terrainHeightmapDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
    ImGui::PopID();

    if (refPlaneImageDialog_.HasResult())
    {
        const auto result = refPlaneImageDialog_.ConsumeResult();
        if (result.accepted && refPlaneDialogTarget_ >= 0
            && refPlaneDialogTarget_ < static_cast<int>(referencePlanes_.size()))
        {
            auto& plane = referencePlanes_[(size_t)refPlaneDialogTarget_];
            plane.imagePath = result.path.generic_string();
            plane.textureName = "refplane::" + plane.imagePath;
            TextureManager::instance().load(plane.textureName, plane.imagePath);
        }
        refPlaneDialogTarget_ = -1;
    }
    ImGui::PushID("RefPlaneImageDialog");
    refPlaneImageDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
    ImGui::PopID();

    // Export dialog
    if (exportDialog_.HasResult())
    {
        const auto result = exportDialog_.ConsumeResult();
        if (result.accepted)
        {
            const std::string ext = result.path.extension().string();
            if (ext == ".obj")
                ExportSceneOBJ(result.path.generic_string());
            else if (ext == ".h3d" || ext == ".mesh")
                ExportSceneH3D(result.path.generic_string());
            else
            {
                // Guess from filename
                const std::string s = result.path.generic_string();
                if (s.find(".obj") != std::string::npos)
                    ExportSceneOBJ(s);
                else
                    ExportSceneH3D(s);
            }
        }
    }
    ImGui::PushID("ExportDialog");
    exportDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
    ImGui::PopID();
}

void LevelEditorApp::PushUndoState()
{
    undoStack_.push_back(scene_);
    redoStack_.clear();
    sceneDirty_ = true;
    if (static_cast<int>(undoStack_.size()) > maxUndoStates_)
        undoStack_.erase(undoStack_.begin());
}

bool LevelEditorApp::PerformUndo()
{
    if (undoStack_.empty())
        return false;

    redoStack_.push_back(scene_);
    scene_ = undoStack_.back();
    undoStack_.pop_back();
    sceneDirty_ = true;

    selectedMeshIndex_ = std::clamp(selectedMeshIndex_, 0, std::max(0, static_cast<int>(scene_.meshObjects().size()) - 1));
    SyncSelectedMeshes();
    SyncSelectedFaces();
    selectedEntityIndex_ = std::clamp(selectedEntityIndex_, 0, std::max(0, static_cast<int>(scene_.entities().size()) - 1));
    meshCacheValid_ = false;
    return true;
}

bool LevelEditorApp::PerformRedo()
{
    if (redoStack_.empty())
        return false;

    undoStack_.push_back(scene_);
    scene_ = redoStack_.back();
    redoStack_.pop_back();
    sceneDirty_ = true;

    selectedMeshIndex_ = std::clamp(selectedMeshIndex_, 0, std::max(0, static_cast<int>(scene_.meshObjects().size()) - 1));
    SyncSelectedMeshes();
    SyncSelectedFaces();
    selectedEntityIndex_ = std::clamp(selectedEntityIndex_, 0, std::max(0, static_cast<int>(scene_.entities().size()) - 1));
    meshCacheValid_ = false;
    return true;
}

void LevelEditorApp::HandleUndoRedoShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;

    const bool ctrlDown = io.KeyCtrl;
    const bool shiftDown = io.KeyShift;
    if (!ctrlDown)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        if (shiftDown)
            PerformRedo();
        else
            PerformUndo();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Y))
    {
        PerformRedo();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_A) && selectionMode_ == SelectionMode::Face)
    {
        if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            const int faceCount = static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faceCount());
            if (faceCount > 0)
            {
                selectedFaceIndices_.clear();
                selectedFaceIndices_.reserve(faceCount);
                for (int faceIndex = 0; faceIndex < faceCount; ++faceIndex)
                    selectedFaceIndices_.push_back(faceIndex);
                selectedFaceIndex_ = 0;
                selectedVertexIndices_.clear();
                selectedEntityIndex_ = -1;
            }
        }
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_O))
    {
        sceneDialog_.Open(ImGuiFileDialog::Mode::OpenFile, std::filesystem::current_path(), "scene.mred");
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_S))
    {
        if (shiftDown || scenePath_.empty())
            sceneDialog_.Open(ImGuiFileDialog::Mode::SaveFile, std::filesystem::current_path(), "scene.mred");
        else
            SaveSceneToPath(scenePath_, false);
    }
}

void LevelEditorApp::HandleToolShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;

    Device& device = Device::Instance();
    if (ImGui::IsKeyPressed(ImGuiKey_F8))
    {
        if (device.IsGifRecording())
        {
            const std::string path = device.GetGifRecordingPath();
            if (device.EndGifRecording())
                sceneStatusMessage_ = "GIF saved: " + path;
            else
                sceneStatusMessage_ = "GIF finalize failed";
        }
        else
        {
            if (device.BeginGifRecording())
                sceneStatusMessage_ = "Recording GIF: " + device.GetGifRecordingPath();
            else
                sceneStatusMessage_ = "Failed to start GIF recording";
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F9))
    {
        if (device.IsFrameSequenceRecording())
        {
            const std::string directory = device.GetFrameSequenceDirectory();
            if (device.EndFrameSequenceRecording())
                sceneStatusMessage_ = "Frames saved: " + directory;
            else
                sceneStatusMessage_ = "Frame sequence finalize failed";
        }
        else
        {
            if (device.BeginFrameSequenceRecording(nullptr, "png"))
                sceneStatusMessage_ = "Recording PNG frames: " + device.GetFrameSequenceDirectory();
            else
                sceneStatusMessage_ = "Failed to start PNG frame recording";
        }
    }

    const bool ctrlDown = io.KeyCtrl;
    const bool hasSelectedMesh = selectedMeshIndex_ >= 0 &&
                                 selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size());
    const bool hasSelectedFace = hasSelectedMesh &&
                                 selectionMode_ == SelectionMode::Face &&
                                 selectedFaceIndex_ >= 0 &&
                                 selectedFaceIndex_ < static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faceCount());

    if (ctrlDown && hasSelectedFace && ImGui::IsKeyPressed(ImGuiKey_C))
    {
        const EditableFace& face = scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faces()[(size_t)selectedFaceIndex_];
        faceUvClipboard_.hasData = true;
        faceUvClipboard_.uvOffset = face.uvOffset;
        faceUvClipboard_.uvScale = face.uvScale;
        faceUvClipboard_.uvRotation = face.uvRotation;
        faceUvClipboard_.uvProjection = face.uvProjection;
        sceneStatusMessage_ = "Copied face UV";
    }
    else if (ctrlDown && faceUvClipboard_.hasData && hasSelectedFace && ImGui::IsKeyPressed(ImGuiKey_V))
    {
        PushUndoState();
        LevelMeshObject& obj = scene_.meshObjects()[(size_t)selectedMeshIndex_];
        for (int faceIndex : selectedFaceIndices_)
        {
            if (faceIndex < 0 || faceIndex >= static_cast<int>(obj.mesh.faceCount()))
                continue;
            EditableFace& face = obj.mesh.facesMutable()[(size_t)faceIndex];
            face.uvOffset = faceUvClipboard_.uvOffset;
            face.uvScale = faceUvClipboard_.uvScale;
            face.uvRotation = faceUvClipboard_.uvRotation;
            face.uvProjection = faceUvClipboard_.uvProjection;
        }
        meshCacheValid_ = false;
        sceneDirty_ = true;
        sceneStatusMessage_ = "Pasted UV to selected faces";
    }

    if (!ctrlDown)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_M)) currentTool_ = Tool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) currentTool_ = Tool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_S)) currentTool_ = Tool::Scale;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_1)) currentTool_ = Tool::Select;
    if (ImGui::IsKeyPressed(ImGuiKey_2)) currentTool_ = Tool::Move;
    if (ImGui::IsKeyPressed(ImGuiKey_3)) currentTool_ = Tool::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_4)) currentTool_ = Tool::Rotate;

    // Delete key: delete selected vertex/face/object depending on mode
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
    {
        if (selectionMode_ == SelectionMode::Vertex && !selectedVertexIndices_.empty())
        {
            PushUndoState();
            LevelMeshObject& obj = scene_.meshObjects()[(size_t)selectedMeshIndex_];
            std::vector<bool> removeVtx(obj.mesh.vertexCount(), false);
            for (int idx : selectedVertexIndices_)
                if (idx >= 0 && idx < static_cast<int>(removeVtx.size()))
                    removeVtx[(size_t)idx] = true;

            std::vector<int> remap(obj.mesh.vertexCount(), -1);
            std::vector<EditableVertex> keptVerts;
            for (std::size_t i = 0; i < obj.mesh.vertexCount(); ++i)
            {
                if (!removeVtx[i])
                {
                    remap[i] = static_cast<int>(keptVerts.size());
                    keptVerts.push_back(obj.mesh.vertices()[i]);
                }
            }

            std::vector<EditableFace> keptFaces;
            for (const auto& face : obj.mesh.faces())
            {
                EditableFace remapped;
                remapped.materialName = face.materialName;
                bool valid = true;
                for (int idx : face.indices)
                {
                    if (idx < 0 || idx >= static_cast<int>(remap.size()) || remap[(size_t)idx] < 0)
                    { valid = false; break; }
                    remapped.indices.push_back(remap[(size_t)idx]);
                }
                if (valid && remapped.indices.size() >= 3)
                    keptFaces.push_back(remapped);
            }

            obj.mesh.setData(keptVerts, keptFaces);
            meshCacheValid_ = false;
            selectedVertexIndices_.clear();
            selectedFaceIndex_ = -1;
            selectedFaceIndices_.clear();
            sceneDirty_ = true;
        }
        else if (selectionMode_ == SelectionMode::Face &&
                 selectedFaceIndex_ >= 0 &&
                 selectedFaceIndex_ < static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faceCount()))
        {
            PushUndoState();
            LevelMeshObject& obj = scene_.meshObjects()[(size_t)selectedMeshIndex_];
            auto& faces = obj.mesh.facesMutable();
            std::vector<int> toDelete = selectedFaceIndices_;
            if (toDelete.empty())
                toDelete.push_back(selectedFaceIndex_);
            std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());
            toDelete.erase(std::unique(toDelete.begin(), toDelete.end()), toDelete.end());
            for (int faceIndex : toDelete)
            {
                if (faceIndex >= 0 && faceIndex < static_cast<int>(faces.size()))
                    faces.erase(faces.begin() + faceIndex);
            }

            std::vector<bool> used(obj.mesh.vertexCount(), false);
            for (const auto& f : obj.mesh.faces())
                for (int idx : f.indices)
                    if (idx >= 0 && idx < static_cast<int>(used.size()))
                        used[(size_t)idx] = true;

            std::vector<int> remap(obj.mesh.vertexCount(), -1);
            std::vector<EditableVertex> keptVerts;
            for (std::size_t i = 0; i < used.size(); ++i)
            {
                if (used[i])
                {
                    remap[i] = static_cast<int>(keptVerts.size());
                    keptVerts.push_back(obj.mesh.vertices()[i]);
                }
            }

            std::vector<EditableFace> keptFaces;
            for (const auto& f : obj.mesh.faces())
            {
                EditableFace remapped;
                remapped.materialName = f.materialName;
                bool valid = true;
                for (int idx : f.indices)
                {
                    if (idx < 0 || idx >= static_cast<int>(remap.size()) || remap[(size_t)idx] < 0)
                    { valid = false; break; }
                    remapped.indices.push_back(remap[(size_t)idx]);
                }
                if (valid && remapped.indices.size() >= 3)
                    keptFaces.push_back(remapped);
            }

            obj.mesh.setData(keptVerts, keptFaces);
            meshCacheValid_ = false;
            selectedFaceIndex_ = -1;
            selectedFaceIndices_.clear();
            selectedVertexIndices_.clear();
            sceneDirty_ = true;
        }
        else if (selectionMode_ == SelectionMode::Object)
        {
            PushUndoState();
            std::vector<LevelMeshObject>& meshObjects = scene_.meshObjects();
            SyncSelectedMeshes();
            // Delete current selection. If we have an explicit multi-selection, delete all selected;
            // otherwise delete only the primary selected mesh index.
            std::vector<int> toDelete;
            if (selectedMeshIndices_.size() > 1 && IsMeshSelected(selectedMeshIndex_))
                toDelete = selectedMeshIndices_;
            else if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(meshObjects.size()))
                toDelete.push_back(selectedMeshIndex_);
            std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());
            // Remove duplicates
            toDelete.erase(std::unique(toDelete.begin(), toDelete.end()), toDelete.end());
            for (int idx : toDelete)
            {
                if (idx >= 0 && idx < static_cast<int>(meshObjects.size()))
                    meshObjects.erase(meshObjects.begin() + idx);
            }
            meshCacheValid_ = false;
            selectedMeshIndex_ = -1;
            selectedMeshIndices_.clear();
            selectedFaceIndex_ = -1;
            selectedFaceIndices_.clear();
            selectedVertexIndices_.clear();
            sceneDirty_ = true;
        }
    }

    // Delete key for entities (when no mesh is selected or in object mode with entity selected)
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
        selectedEntityIndex_ >= 0 &&
        selectedEntityIndex_ < static_cast<int>(scene_.entities().size()) &&
        (selectedMeshIndex_ < 0 || selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size())))
    {
        PushUndoState();
        scene_.entities().erase(scene_.entities().begin() + selectedEntityIndex_);
        if (selectedEntityIndex_ >= static_cast<int>(scene_.entities().size()))
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        sceneDirty_ = true;
    }

    // Ctrl+D: Duplicate selected mesh or entity
    if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_D))
    {
        if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            PushUndoState();
            LevelMeshObject copy = scene_.meshObjects()[(size_t)selectedMeshIndex_];
            copy.name += " (copy)";
            copy.position += glm::vec3(16.0f, 0.0f, 0.0f);
            scene_.meshObjects().push_back(copy);
            SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
            meshCacheValid_ = false;
            sceneDirty_ = true;
        }
        else if (selectedEntityIndex_ >= 0 && selectedEntityIndex_ < static_cast<int>(scene_.entities().size()))
        {
            PushUndoState();
            LevelEntityObject copy = scene_.entities()[(size_t)selectedEntityIndex_];
            copy.name += " (copy)";
            copy.position += glm::vec3(16.0f, 0.0f, 0.0f);
            scene_.entities().push_back(copy);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
            sceneDirty_ = true;
        }
    }

    // F: Frame selected — center camera on selection
    if (!ctrlDown && ImGui::IsKeyPressed(ImGuiKey_F))
    {
        glm::vec3 target(0.0f);
        float radius = 64.0f;
        bool hasTarget = false;

        if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            const LevelMeshObject& obj = scene_.meshObjects()[(size_t)selectedMeshIndex_];
            const BoundingBox localBounds = editableMeshLocalBounds(obj.mesh);
            const BoundingBox worldBounds = localBounds.transformed(meshObjectModelMatrix(obj));
            target = (worldBounds.min + worldBounds.max) * 0.5f;
            radius = glm::length(worldBounds.max - worldBounds.min) * 0.5f;
            if (radius < 1.0f) radius = 64.0f;
            hasTarget = true;
        }
        else if (selectedEntityIndex_ >= 0 && selectedEntityIndex_ < static_cast<int>(scene_.entities().size()))
        {
            target = scene_.entities()[(size_t)selectedEntityIndex_].position;
            hasTarget = true;
        }

        if (hasTarget)
        {
            for (int vi = 0; vi < activeViewCount_; ++vi)
            {
                views_[vi].focus = target;
                if (views_[vi].type == ViewType::Perspective)
                    views_[vi].perspectiveDistance = radius * 3.0f;
                else
                    views_[vi].orthoSize = radius * 2.5f;
            }
        }
    }
}

void LevelEditorApp::InitializeViews()
{
    views_[0].type = ViewType::Top;
    views_[1].type = ViewType::Front;
    views_[2].type = ViewType::Right;
    views_[3].type = ViewType::Perspective;

    for (LevelEditorView& view : views_)
    {
        view.label = viewTypeName(view.type);
        view.focus = glm::vec3(0.0f, 64.0f, 0.0f);
    }
}

void LevelEditorApp::LayoutViews(const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    viewportCanvasPos_ = canvasPos;
    viewportCanvasSize_ = canvasSize;

    const int gap = 8;
    const int x = static_cast<int>(canvasPos.x);
    const int y = static_cast<int>(canvasPos.y);
    const int w = std::max(0, static_cast<int>(canvasSize.x));
    const int h = std::max(0, static_cast<int>(canvasSize.y));
    for (LevelEditorView& view : views_)
        view.rect = {0, 0, 0, 0};

    activeViewCount_ = static_cast<int>(viewLayout_);
    if (activeViewIndex_ >= activeViewCount_)
        activeViewIndex_ = std::max(0, activeViewCount_ - 1);
    if (viewLayout_ == ViewLayout::One)
    {
        views_[0].rect = {x, y, w, h};
        return;
    }
    if (viewLayout_ == ViewLayout::Two)
    {
        const int halfW = std::max(0, (w - gap) / 2);
        views_[0].rect = {x, y, halfW, h};
        views_[1].rect = {x + halfW + gap, y, halfW, h};
        return;
    }
    if (viewLayout_ == ViewLayout::Three)
    {
        const int leftW = std::max(0, (w - gap) * 3 / 5);
        const int rightW = std::max(0, w - gap - leftW);
        const int halfH = std::max(0, (h - gap) / 2);
        views_[0].rect = {x, y, leftW, h};
        views_[1].rect = {x + leftW + gap, y, rightW, halfH};
        views_[2].rect = {x + leftW + gap, y + halfH + gap, rightW, halfH};
        return;
    }

    const int halfW = std::max(0, (w - gap) / 2);
    const int halfH = std::max(0, (h - gap) / 2);
    views_[0].rect = {x, y, halfW, halfH};
    views_[1].rect = {x + halfW + gap, y, halfW, halfH};
    views_[2].rect = {x, y + halfH + gap, halfW, halfH};
    views_[3].rect = {x + halfW + gap, y + halfH + gap, halfW, halfH};
}

void LevelEditorApp::UpdateViewCameras()
{
    constexpr float orthoDistance = 1024.0f;
    const float nearPlane = std::max(0.001f, perspectiveNearPlane_);
    const float farPlane = std::max(nearPlane + 1.0f, perspectiveFarPlane_);

    for (int i = 0; i < static_cast<int>(views_.size()); ++i)
    {
        LevelEditorView& view = views_[i];
        if (i >= activeViewCount_ || view.rect.w <= 0 || view.rect.h <= 0)
            continue;

        view.camera.setViewport(0, 0, view.rect.w, view.rect.h);

        if (view.type == ViewType::Perspective)
        {
            view.perspectiveDistance = std::max(view.perspectiveDistance, perspectiveMinDistance_);
            view.camera.setViewPlanes(nearPlane, farPlane);
            view.camera.setProjectionType(ProjectionType::Perspective);
            view.camera.setFov(60.0f);

            const float yaw = glm::radians(view.perspectiveYaw);
            const float pitch = glm::radians(view.perspectivePitch);
            const glm::vec3 offset(
                std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                std::cos(pitch) * std::cos(yaw));

            view.camera.setPosition(view.focus + offset * view.perspectiveDistance);
            view.camera.lookAt(view.focus, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        else
        {
            view.camera.setViewPlanes(0.1f, 8192.0f);
            view.camera.setProjectionType(ProjectionType::Orthographic);
            view.camera.orthoSize = view.orthoSize;

            switch (view.type)
            {
            case ViewType::Top:
                view.camera.setPosition(view.focus + glm::vec3(0.0f, orthoDistance, 0.0f));
                view.camera.lookAt(view.focus, glm::vec3(0.0f, 0.0f, -1.0f));
                break;
            case ViewType::Bottom:
                view.camera.setPosition(view.focus + glm::vec3(0.0f, -orthoDistance, 0.0f));
                view.camera.lookAt(view.focus, glm::vec3(0.0f, 0.0f, 1.0f));
                break;
            case ViewType::Front:
                view.camera.setPosition(view.focus + glm::vec3(0.0f, 0.0f, orthoDistance));
                view.camera.lookAt(view.focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case ViewType::Back:
                view.camera.setPosition(view.focus + glm::vec3(0.0f, 0.0f, -orthoDistance));
                view.camera.lookAt(view.focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case ViewType::Left:
                view.camera.setPosition(view.focus + glm::vec3(-orthoDistance, 0.0f, 0.0f));
                view.camera.lookAt(view.focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case ViewType::Right:
                view.camera.setPosition(view.focus + glm::vec3(orthoDistance, 0.0f, 0.0f));
                view.camera.lookAt(view.focus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case ViewType::Perspective:
                break;
            }
        }

        view.camera.updateMatrices();
    }
}

const LevelEditorApp::LevelEditorView* LevelEditorApp::HoveredView() const
{
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const glm::vec2 mouse(mousePos.x, mousePos.y);
    for (int i = 0; i < activeViewCount_; ++i)
    {
        const LevelEditorView& view = views_[i];
        if (view.rect.contains(mouse))
            return &view;
    }
    return nullptr;
}

LevelEditorApp::LevelEditorView* LevelEditorApp::HoveredView()
{
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const glm::vec2 mouse(mousePos.x, mousePos.y);
    for (int i = 0; i < activeViewCount_; ++i)
    {
        LevelEditorView& view = views_[i];
        if (view.rect.contains(mouse))
            return &view;
    }
    return nullptr;
}

int LevelEditorApp::PickMeshInPerspectiveView(const LevelEditorView& view, const glm::vec2& mouseScreen) const
{
    if (view.type != ViewType::Perspective)
        return -1;

    const glm::vec2 localMouse(
        mouseScreen.x - static_cast<float>(view.rect.x),
        mouseScreen.y - static_cast<float>(view.rect.y));
    const Ray ray = view.camera.getRay(localMouse.x, localMouse.y);

    int bestIndex = -1;
    float bestHit = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(scene_.meshObjects().size()); ++i)
    {
        const LevelMeshObject& object = scene_.meshObjects()[i];
        if (!object.visible || object.locked) continue;
        BoundingBox localBounds;
        for (const EditableVertex& vertex : object.mesh.vertices())
            localBounds.expand(vertex.position);
        if (!localBounds.is_valid())
            continue;

        const BoundingBox worldBounds = localBounds.transformed(meshObjectModelMatrix(object));
        const float boundsHit = worldBounds.intersects_ray(ray.origin, ray.direction);
        if (boundsHit < 0.0f || boundsHit >= bestHit)
            continue;

        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
        float objectBestHit = std::numeric_limits<float>::max();
        for (std::size_t faceIndex = 0; faceIndex < object.mesh.faces().size(); ++faceIndex)
        {
            const EditableFace& face = object.mesh.faces()[faceIndex];
            if (face.indices.size() < 3)
                continue;

            const int baseIndex = face.indices[0];
            if (baseIndex < 0 || baseIndex >= static_cast<int>(object.mesh.vertices().size()))
                continue;

            const glm::vec3 baseVertex = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[baseIndex].position, 1.0f));
            for (std::size_t triIndex = 1; triIndex + 1 < face.indices.size(); ++triIndex)
            {
                const int i1 = face.indices[triIndex];
                const int i2 = face.indices[triIndex + 1];
                if (i1 < 0 || i1 >= static_cast<int>(object.mesh.vertices().size()) ||
                    i2 < 0 || i2 >= static_cast<int>(object.mesh.vertices().size()))
                {
                    continue;
                }

                Triangle tri;
                tri.v0 = baseVertex;
                tri.v1 = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[i1].position, 1.0f));
                tri.v2 = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[i2].position, 1.0f));
                const float triHit = tri.intersect_ray(ray.origin, ray.direction);
                if (triHit >= 0.0f && triHit < objectBestHit)
                    objectBestHit = triHit;
            }
        }

        if (objectBestHit < bestHit)
        {
            bestHit = objectBestHit;
            bestIndex = i;
        }
    }

    return bestIndex;
}

int LevelEditorApp::PickFaceInPerspectiveView(const LevelEditorView& view, const glm::vec2& mouseScreen, int& outMeshIndex) const
{
    outMeshIndex = -1;
    if (view.type != ViewType::Perspective)
        return -1;

    const glm::vec2 localMouse(
        mouseScreen.x - static_cast<float>(view.rect.x),
        mouseScreen.y - static_cast<float>(view.rect.y));
    const Ray ray = view.camera.getRay(localMouse.x, localMouse.y);

    int bestFaceIndex = -1;
    float bestHit = std::numeric_limits<float>::max();
    for (int meshIndex = 0; meshIndex < static_cast<int>(scene_.meshObjects().size()); ++meshIndex)
    {
        const LevelMeshObject& object = scene_.meshObjects()[(size_t)meshIndex];
        if (!object.visible || object.locked)
            continue;

        BoundingBox localBounds;
        for (const EditableVertex& vertex : object.mesh.vertices())
            localBounds.expand(vertex.position);
        if (!localBounds.is_valid())
            continue;

        const BoundingBox worldBounds = localBounds.transformed(meshObjectModelMatrix(object));
        const float boundsHit = worldBounds.intersects_ray(ray.origin, ray.direction);
        if (boundsHit < 0.0f || boundsHit >= bestHit)
            continue;

        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
        for (std::size_t faceIndex = 0; faceIndex < object.mesh.faces().size(); ++faceIndex)
        {
            const EditableFace& face = object.mesh.faces()[faceIndex];
            if (face.indices.size() < 3)
                continue;

            const int baseIndex = face.indices[0];
            if (baseIndex < 0 || baseIndex >= static_cast<int>(object.mesh.vertices().size()))
                continue;

            const glm::vec3 baseVertex = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)baseIndex].position, 1.0f));
            float faceBestHit = std::numeric_limits<float>::max();
            for (std::size_t triIndex = 1; triIndex + 1 < face.indices.size(); ++triIndex)
            {
                const int i1 = face.indices[triIndex];
                const int i2 = face.indices[triIndex + 1];
                if (i1 < 0 || i1 >= static_cast<int>(object.mesh.vertices().size()) ||
                    i2 < 0 || i2 >= static_cast<int>(object.mesh.vertices().size()))
                {
                    continue;
                }

                Triangle tri;
                tri.v0 = baseVertex;
                tri.v1 = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)i1].position, 1.0f));
                tri.v2 = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)i2].position, 1.0f));
                const float triHit = tri.intersect_ray(ray.origin, ray.direction);
                if (triHit >= 0.0f && triHit < faceBestHit)
                    faceBestHit = triHit;
            }

            if (faceBestHit < bestHit)
            {
                bestHit = faceBestHit;
                bestFaceIndex = static_cast<int>(faceIndex);
                outMeshIndex = meshIndex;
            }
        }
    }

    return bestFaceIndex;
}

bool LevelEditorApp::PickTerrainLocalPointPerspective(const LevelEditorView& view,
                                                      const glm::vec2& mouseScreen,
                                                      const LevelMeshObject& object,
                                                      glm::vec3& outLocalPoint) const
{
    const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
    const glm::mat4 inverseModel = glm::inverse(modelMatrix);

    const glm::vec2 localMouse(
        mouseScreen.x - static_cast<float>(view.rect.x),
        mouseScreen.y - static_cast<float>(view.rect.y));
    const Ray ray = view.camera.getRay(localMouse.x, localMouse.y);
    float bestHit = std::numeric_limits<float>::max();
    bool found = false;

    for (const EditableFace& face : object.mesh.faces())
    {
        if (face.indices.size() < 3)
            continue;

        const int baseIndex = face.indices[0];
        if (baseIndex < 0 || baseIndex >= static_cast<int>(object.mesh.vertices().size()))
            continue;

        const glm::vec3 baseVertex = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[static_cast<std::size_t>(baseIndex)].position, 1.0f));
        for (std::size_t triIndex = 1; triIndex + 1 < face.indices.size(); ++triIndex)
        {
            const int i1 = face.indices[triIndex];
            const int i2 = face.indices[triIndex + 1];
            if (i1 < 0 || i1 >= static_cast<int>(object.mesh.vertices().size()) ||
                i2 < 0 || i2 >= static_cast<int>(object.mesh.vertices().size()))
            {
                continue;
            }

            Triangle tri;
            tri.v0 = baseVertex;
            tri.v1 = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[static_cast<std::size_t>(i1)].position, 1.0f));
            tri.v2 = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[static_cast<std::size_t>(i2)].position, 1.0f));
            const float triHit = tri.intersect_ray(ray.origin, ray.direction);
            if (triHit >= 0.0f && triHit < bestHit)
            {
                bestHit = triHit;
                outLocalPoint = glm::vec3(inverseModel * glm::vec4(ray.at(triHit), 1.0f));
                found = true;
            }
        }
    }

    return found;
}

int LevelEditorApp::PickVertexInOrthoView(const LevelEditorView& view, const glm::vec2& mouseScreen) const
{
    if (view.type == ViewType::Perspective ||
        selectedMeshIndex_ < 0 ||
        selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
    {
        return -1;
    }

    const LevelMeshObject& object = scene_.meshObjects()[(size_t)selectedMeshIndex_];
    const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
    constexpr float pickRadius = 8.0f;

    struct VertexHit
    {
        int index = -1;
        ImVec2 point = ImVec2(0.0f, 0.0f);
        float depth = 0.0f;
        float distanceSq = std::numeric_limits<float>::max();
    };

    std::vector<VertexHit> hits;
    hits.reserve(object.mesh.vertices().size());
    for (int vertexIndex = 0; vertexIndex < static_cast<int>(object.mesh.vertices().size()); ++vertexIndex)
    {
        ImVec2 point;
        float depth = 0.0f;
        const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)vertexIndex].position, 1.0f));
        if (!ProjectWorldToView(view, world, point, depth))
            continue;

        const float dx = point.x - mouseScreen.x;
        const float dy = point.y - mouseScreen.y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq > pickRadius * pickRadius)
            continue;

        hits.push_back({vertexIndex, point, depth, distanceSq});
    }

    if (hits.empty())
        return -1;

    if (!vertexFrontOnly_)
    {
        std::sort(hits.begin(), hits.end(), [](const VertexHit& a, const VertexHit& b)
        {
            if (a.distanceSq != b.distanceSq)
                return a.distanceSq < b.distanceSq;
            return a.depth < b.depth;
        });
        return hits.front().index;
    }

    constexpr float overlapTolerance = 6.0f;
    int bestIndex = -1;
    float bestDistanceSq = std::numeric_limits<float>::max();
    float bestDepth = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        bool occluded = false;
        for (std::size_t j = 0; j < hits.size(); ++j)
        {
            if (i == j)
                continue;
            if (std::fabs(hits[j].point.x - hits[i].point.x) <= overlapTolerance &&
                std::fabs(hits[j].point.y - hits[i].point.y) <= overlapTolerance &&
                hits[j].depth < hits[i].depth)
            {
                occluded = true;
                break;
            }
        }
        if (occluded)
            continue;

        if (hits[i].distanceSq < bestDistanceSq ||
            (hits[i].distanceSq == bestDistanceSq && hits[i].depth < bestDepth))
        {
            bestIndex = hits[i].index;
            bestDistanceSq = hits[i].distanceSq;
            bestDepth = hits[i].depth;
        }
    }

    return bestIndex;
}

int LevelEditorApp::PickVertexInPerspectiveView(const LevelEditorView& view, const glm::vec2& mouseScreen, int& outMeshIndex) const
{
    outMeshIndex = -1;
    if (view.type != ViewType::Perspective)
        return -1;

    constexpr float pickRadius = 10.0f;
    int bestVertexIndex = -1;
    float bestDistanceSq = std::numeric_limits<float>::max();
    float bestDepth = std::numeric_limits<float>::max();

    for (int meshIndex = 0; meshIndex < static_cast<int>(scene_.meshObjects().size()); ++meshIndex)
    {
        const LevelMeshObject& object = scene_.meshObjects()[(size_t)meshIndex];
        if (!object.visible || object.locked)
            continue;

        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
        for (int vertexIndex = 0; vertexIndex < static_cast<int>(object.mesh.vertices().size()); ++vertexIndex)
        {
            ImVec2 point;
            float depth = 0.0f;
            const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)vertexIndex].position, 1.0f));
            if (!ProjectWorldToView(view, world, point, depth))
                continue;

            const float dx = point.x - mouseScreen.x;
            const float dy = point.y - mouseScreen.y;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq > pickRadius * pickRadius)
                continue;

            if (distanceSq < bestDistanceSq || (distanceSq == bestDistanceSq && depth < bestDepth))
            {
                bestDistanceSq = distanceSq;
                bestDepth = depth;
                bestVertexIndex = vertexIndex;
                outMeshIndex = meshIndex;
            }
        }
    }

    return bestVertexIndex;
}

bool LevelEditorApp::ProjectWorldToView(const LevelEditorView& view, const glm::vec3& world, ImVec2& outPoint, float& outDepth) const
{
    const float headerHeight = 26.0f;
    const ImVec2 minPos(static_cast<float>(view.rect.x), static_cast<float>(view.rect.y));
    const ImVec2 maxPos(static_cast<float>(view.rect.x + view.rect.w), static_cast<float>(view.rect.y + view.rect.h));
    const glm::vec4 clip = view.camera.viewProjection * glm::vec4(world, 1.0f);
    if (std::fabs(clip.w) <= 1e-6f)
        return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (view.type == ViewType::Perspective && (ndc.z < -1.0f || ndc.z > 1.0f))
        return false;

    const float nx = (ndc.x * 0.5f) + 0.5f;
    const float ny = 1.0f - ((ndc.y * 0.5f) + 0.5f);
    outPoint.x = minPos.x + nx * (maxPos.x - minPos.x);
    outPoint.y = (minPos.y + headerHeight) + ny * (maxPos.y - (minPos.y + headerHeight));
    outDepth = ndc.z;
    return std::isfinite(outPoint.x) && std::isfinite(outPoint.y);
}

std::vector<int> LevelEditorApp::PickMeshesInOrthoRect(const LevelEditorView& view, const glm::vec2& startScreen, const glm::vec2& endScreen) const
{
    if (view.type == ViewType::Perspective)
        return {};

    const float headerHeight = 26.0f;
    const float minX = static_cast<float>(view.rect.x);
    const float maxX = static_cast<float>(view.rect.x + view.rect.w);
    const float minY = static_cast<float>(view.rect.y) + headerHeight;
    const float maxY = static_cast<float>(view.rect.y + view.rect.h);

    float selMinX = std::clamp(std::min(startScreen.x, endScreen.x), minX, maxX);
    float selMaxX = std::clamp(std::max(startScreen.x, endScreen.x), minX, maxX);
    float selMinY = std::clamp(std::min(startScreen.y, endScreen.y), minY, maxY);
    float selMaxY = std::clamp(std::max(startScreen.y, endScreen.y), minY, maxY);

    if ((selMaxX - selMinX) < 4.0f)
    {
        selMinX -= 3.0f;
        selMaxX += 3.0f;
    }
    if ((selMaxY - selMinY) < 4.0f)
    {
        selMinY -= 3.0f;
        selMaxY += 3.0f;
    }

    std::vector<std::pair<float, int>> picked;

    for (int objectIndex = 0; objectIndex < static_cast<int>(scene_.meshObjects().size()); ++objectIndex)
    {
        const LevelMeshObject& object = scene_.meshObjects()[objectIndex];
        if (!object.visible || object.locked) continue;
        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);

        bool hasPoint = false;
        float objMinX = 0.0f;
        float objMaxX = 0.0f;
        float objMinY = 0.0f;
        float objMaxY = 0.0f;

        for (const EditableVertex& vertex : object.mesh.vertices())
        {
            ImVec2 point;
            float depth = 0.0f;
            const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
            if (!ProjectWorldToView(view, world, point, depth))
                continue;

            if (!hasPoint)
            {
                objMinX = objMaxX = point.x;
                objMinY = objMaxY = point.y;
                hasPoint = true;
            }
            else
            {
                objMinX = std::min(objMinX, point.x);
                objMaxX = std::max(objMaxX, point.x);
                objMinY = std::min(objMinY, point.y);
                objMaxY = std::max(objMaxY, point.y);
            }
        }

        if (!hasPoint)
            continue;

        const bool overlaps = objMaxX >= selMinX &&
                              objMinX <= selMaxX &&
                              objMaxY >= selMinY &&
                              objMinY <= selMaxY;
        if (!overlaps)
            continue;

        const float area = (objMaxX - objMinX) * (objMaxY - objMinY);
        picked.emplace_back(area, objectIndex);
    }

    std::sort(picked.begin(), picked.end(), [](const auto& a, const auto& b)
    {
        if (a.first != b.first)
            return a.first < b.first;
        return a.second < b.second;
    });

    std::vector<int> indices;
    indices.reserve(picked.size());
    for (const auto& item : picked)
        indices.push_back(item.second);
    return indices;
}

std::vector<int> LevelEditorApp::PickVerticesInOrthoRect(const LevelEditorView& view, const glm::vec2& startScreen, const glm::vec2& endScreen) const
{
    std::vector<int> indices;
    if (view.type == ViewType::Perspective ||
        selectedMeshIndex_ < 0 ||
        selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
    {
        return indices;
    }

    const LevelMeshObject& object = scene_.meshObjects()[(size_t)selectedMeshIndex_];
    const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
    const float headerHeight = 26.0f;
    const float minX = static_cast<float>(view.rect.x);
    const float maxX = static_cast<float>(view.rect.x + view.rect.w);
    const float minY = static_cast<float>(view.rect.y) + headerHeight;
    const float maxY = static_cast<float>(view.rect.y + view.rect.h);

    float selMinX = std::clamp(std::min(startScreen.x, endScreen.x), minX, maxX);
    float selMaxX = std::clamp(std::max(startScreen.x, endScreen.x), minX, maxX);
    float selMinY = std::clamp(std::min(startScreen.y, endScreen.y), minY, maxY);
    float selMaxY = std::clamp(std::max(startScreen.y, endScreen.y), minY, maxY);

    if ((selMaxX - selMinX) < 6.0f)
    {
        selMinX -= 5.0f;
        selMaxX += 5.0f;
    }
    if ((selMaxY - selMinY) < 6.0f)
    {
        selMinY -= 5.0f;
        selMaxY += 5.0f;
    }

    struct VertexHit
    {
        int index = -1;
        ImVec2 point = ImVec2(0.0f, 0.0f);
        float depth = 0.0f;
    };
    std::vector<VertexHit> hits;

    for (int vertexIndex = 0; vertexIndex < static_cast<int>(object.mesh.vertices().size()); ++vertexIndex)
    {
        ImVec2 point;
        float depth = 0.0f;
        const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)vertexIndex].position, 1.0f));
        if (!ProjectWorldToView(view, world, point, depth))
            continue;
        if (point.x >= selMinX && point.x <= selMaxX &&
            point.y >= selMinY && point.y <= selMaxY)
        {
            hits.push_back({vertexIndex, point, depth});
        }
    }

    if (!vertexFrontOnly_)
    {
        indices.reserve(hits.size());
        for (const VertexHit& hit : hits)
            indices.push_back(hit.index);
        return indices;
    }

    constexpr float overlapTolerance = 6.0f;
    std::vector<bool> consumed(hits.size(), false);
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        if (consumed[i])
            continue;

        std::size_t best = i;
        for (std::size_t j = i + 1; j < hits.size(); ++j)
        {
            if (std::fabs(hits[j].point.x - hits[i].point.x) <= overlapTolerance &&
                std::fabs(hits[j].point.y - hits[i].point.y) <= overlapTolerance)
            {
                consumed[j] = true;
                if (hits[j].depth < hits[best].depth)
                    best = j;
            }
        }
        indices.push_back(hits[best].index);
    }

    return indices;
}

std::vector<int> LevelEditorApp::PickFacesInOrthoRect(const LevelEditorView& view, const glm::vec2& startScreen, const glm::vec2& endScreen) const
{
    std::vector<int> indices;
    if (view.type == ViewType::Perspective ||
        selectedMeshIndex_ < 0 ||
        selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
    {
        return indices;
    }

    const LevelMeshObject& object = scene_.meshObjects()[(size_t)selectedMeshIndex_];
    const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
    const float headerHeight = 26.0f;
    const float minX = static_cast<float>(view.rect.x);
    const float maxX = static_cast<float>(view.rect.x + view.rect.w);
    const float minY = static_cast<float>(view.rect.y) + headerHeight;
    const float maxY = static_cast<float>(view.rect.y + view.rect.h);

    float selMinX = std::clamp(std::min(startScreen.x, endScreen.x), minX, maxX);
    float selMaxX = std::clamp(std::max(startScreen.x, endScreen.x), minX, maxX);
    float selMinY = std::clamp(std::min(startScreen.y, endScreen.y), minY, maxY);
    float selMaxY = std::clamp(std::max(startScreen.y, endScreen.y), minY, maxY);

    if ((selMaxX - selMinX) < 6.0f)
    {
        selMinX -= 5.0f;
        selMaxX += 5.0f;
    }
    if ((selMaxY - selMinY) < 6.0f)
    {
        selMinY -= 5.0f;
        selMaxY += 5.0f;
    }

    std::vector<std::pair<float, int>> picked;
    for (int faceIndex = 0; faceIndex < static_cast<int>(object.mesh.faces().size()); ++faceIndex)
    {
        const EditableFace& face = object.mesh.faces()[(size_t)faceIndex];
        if (face.indices.size() < 3)
            continue;

        bool hasPoint = false;
        float faceMinX = 0.0f;
        float faceMaxX = 0.0f;
        float faceMinY = 0.0f;
        float faceMaxY = 0.0f;
        for (int index : face.indices)
        {
            if (index < 0 || index >= static_cast<int>(object.mesh.vertices().size()))
            {
                hasPoint = false;
                break;
            }

            ImVec2 point;
            float depth = 0.0f;
            const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)index].position, 1.0f));
            if (!ProjectWorldToView(view, world, point, depth))
            {
                hasPoint = false;
                break;
            }

            if (!hasPoint)
            {
                faceMinX = faceMaxX = point.x;
                faceMinY = faceMaxY = point.y;
                hasPoint = true;
            }
            else
            {
                faceMinX = std::min(faceMinX, point.x);
                faceMaxX = std::max(faceMaxX, point.x);
                faceMinY = std::min(faceMinY, point.y);
                faceMaxY = std::max(faceMaxY, point.y);
            }
        }

        if (!hasPoint)
            continue;

        const bool overlaps = faceMaxX >= selMinX &&
                              faceMinX <= selMaxX &&
                              faceMaxY >= selMinY &&
                              faceMinY <= selMaxY;
        if (!overlaps)
            continue;

        const float area = (faceMaxX - faceMinX) * (faceMaxY - faceMinY);
        picked.emplace_back(area, faceIndex);
    }

    std::sort(picked.begin(), picked.end(), [](const auto& a, const auto& b)
    {
        if (a.first != b.first)
            return a.first < b.first;
        return a.second < b.second;
    });

    indices.reserve(picked.size());
    for (const auto& item : picked)
        indices.push_back(item.second);
    return indices;
}

glm::vec3 LevelEditorApp::OrthoPointFromScreen(const LevelEditorView& view, const glm::vec3& focus, const glm::vec2& mousePos) const
{
    const float aspect = (view.rect.h > 0) ? (static_cast<float>(view.rect.w) / static_cast<float>(view.rect.h)) : 1.0f;
    const float halfH = view.orthoSize;
    const float halfW = halfH * aspect;

    const float localX = ((mousePos.x - static_cast<float>(view.rect.x)) / static_cast<float>(view.rect.w)) * 2.0f - 1.0f;
    const float localY = 1.0f - ((mousePos.y - static_cast<float>(view.rect.y)) / static_cast<float>(view.rect.h)) * 2.0f;

    switch (view.type)
    {
    case ViewType::Top:
        return glm::vec3(focus.x + localX * halfW, focus.y, focus.z - localY * halfH);
    case ViewType::Bottom:
        return glm::vec3(focus.x + localX * halfW, focus.y, focus.z + localY * halfH);
    case ViewType::Front:
        return glm::vec3(focus.x + localX * halfW, focus.y + localY * halfH, focus.z);
    case ViewType::Back:
        return glm::vec3(focus.x - localX * halfW, focus.y + localY * halfH, focus.z);
    case ViewType::Left:
        return glm::vec3(focus.x, focus.y + localY * halfH, focus.z + localX * halfW);
    case ViewType::Right:
        return glm::vec3(focus.x, focus.y + localY * halfH, focus.z - localX * halfW);
    case ViewType::Perspective:
        break;
    }

    return focus;
}

glm::vec3 LevelEditorApp::ApplyViewDelta(const glm::vec3& delta, ViewType viewType) const
{
    switch (viewType)
    {
    case ViewType::Top:
    case ViewType::Bottom:
        return glm::vec3(delta.x, 0.0f, delta.z);
    case ViewType::Front:
    case ViewType::Back:
        return glm::vec3(delta.x, delta.y, 0.0f);
    case ViewType::Left:
    case ViewType::Right:
        return glm::vec3(0.0f, delta.y, delta.z);
    case ViewType::Perspective:
        break;
    }
    return delta;
}

void LevelEditorApp::HandleViewportInput(bool viewportHovered)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!viewportHovered)
        return;

    LevelEditorView* hovered = HoveredView();
    if (!hovered)
        return;

    for (int i = 0; i < activeViewCount_; ++i)
    {
        if (&views_[i] == hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            activeViewIndex_ = i;
            break;
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.KeyShift && !io.KeyCtrl)
    {
        contextViewIndex_ = -1;
        for (int i = 0; i < activeViewCount_; ++i)
        {
            if (&views_[i] == hovered)
            {
                contextViewIndex_ = i;
                break;
            }
        }
        if (contextViewIndex_ >= 0)
            ImGui::OpenPopup("ViewContextMenu");
    }

    const bool ctrlDown = io.KeyCtrl;
    const bool shiftDown = io.KeyShift;
    const glm::vec2 mouseScreen(io.MousePos.x, io.MousePos.y);
    const glm::vec2 delta(io.MouseDelta.x, io.MouseDelta.y);
    const bool leftDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
    const bool rightDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2.0f);
    const glm::vec2 dragMouseDelta = mouseScreen - dragStartMouse_;

    const bool expensivePerspectiveHover =
        hovered->type == ViewType::Perspective &&
        !ImGuizmo::IsUsing() &&
        (selectionMode_ == SelectionMode::Face || selectionMode_ == SelectionMode::Vertex);
    const bool shouldRefreshPerspectiveHover =
        expensivePerspectiveHover &&
        (glm::length2(delta) > 0.0f ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right));

    if (!expensivePerspectiveHover)
    {
        hoveredFaceMeshIndex_ = -1;
        hoveredFaceIndex_ = -1;
        hoveredVertexMeshIndex_ = -1;
        hoveredVertexIndex_ = -1;
    }

    if (selectionMode_ == SelectionMode::Face)
    {
        if (hovered->type == ViewType::Perspective)
        {
            if (shouldRefreshPerspectiveHover)
                hoveredFaceIndex_ = PickFaceInPerspectiveView(*hovered, mouseScreen, hoveredFaceMeshIndex_);
        }
        else
        {
            hoveredFaceMeshIndex_ = -1;
            hoveredFaceIndex_ = -1;
        }

        hoveredVertexMeshIndex_ = -1;
        hoveredVertexIndex_ = -1;
    }
    else if (selectionMode_ == SelectionMode::Vertex && !ImGuizmo::IsUsing())
    {
        if (hovered->type == ViewType::Perspective)
        {
            if (shouldRefreshPerspectiveHover)
                hoveredVertexIndex_ = PickVertexInPerspectiveView(*hovered, mouseScreen, hoveredVertexMeshIndex_);
        }
        else
        {
            hoveredVertexMeshIndex_ = selectedMeshIndex_;
            hoveredVertexIndex_ = PickVertexInOrthoView(*hovered, mouseScreen);
        }

        hoveredFaceMeshIndex_ = -1;
        hoveredFaceIndex_ = -1;
    }
    else
    {
        hoveredFaceMeshIndex_ = -1;
        hoveredFaceIndex_ = -1;
        hoveredVertexMeshIndex_ = -1;
        hoveredVertexIndex_ = -1;
    }

    const bool canSculptTerrain =
        terrainSculptEnabled_ &&
        currentTool_ == Tool::Select &&
        !ctrlDown &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing() &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size());

    int terrainCols = 0;
    int terrainRows = 0;
    const bool selectedMeshIsTerrain = canSculptTerrain && SelectedMeshIsTerrain(&terrainCols, &terrainRows);
    if (selectedMeshIsTerrain)
    {
        LevelMeshObject& terrainObject = scene_.meshObjects()[static_cast<std::size_t>(selectedMeshIndex_)];
        glm::vec3 sculptCenterLocal(0.0f);
        bool hasSculptCenter = false;
        if (hovered->type == ViewType::Perspective)
        {
            hasSculptCenter = PickTerrainLocalPointPerspective(*hovered, mouseScreen, terrainObject, sculptCenterLocal);
        }
        else if (hovered->type == ViewType::Top)
        {
            const glm::mat4 inverseModel = glm::inverse(meshObjectModelMatrix(terrainObject));
            const glm::vec3 worldPoint = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen);
            sculptCenterLocal = glm::vec3(inverseModel * glm::vec4(worldPoint, 1.0f));
            sculptCenterLocal.y = sampleTerrainHeightLocal(terrainObject.mesh, terrainCols, terrainRows, sculptCenterLocal.x, sculptCenterLocal.z);
            hasSculptCenter = true;
        }

        if (hasSculptCenter)
        {
            terrainBrushPreviewLocalCenter_ = sculptCenterLocal;
            terrainBrushPreviewValid_ = true;
        }
        else if (!terrainSculpting_)
        {
            terrainBrushPreviewValid_ = false;
        }

        if (terrainSculpting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            terrainSculpting_ = false;
            terrainSculptHasLastSample_ = false;
            dragUndoPushed_ = false;
        }

        if (hasSculptCenter && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (!terrainSculpting_)
            {
                PushUndoState();
                dragUndoPushed_ = true;
                terrainSculpting_ = true;
                terrainSculptHasLastSample_ = false;
            }

            const float spacing = std::max(1.0f, terrainBrushRadius_ * 0.2f);
            const bool shouldApplySample =
                !terrainSculptHasLastSample_ ||
                glm::distance(glm::vec2(sculptCenterLocal.x, sculptCenterLocal.z),
                              glm::vec2(terrainSculptLastLocalCenter_.x, terrainSculptLastLocalCenter_.z)) >= spacing;
            if (shouldApplySample)
            {
                applyTerrainBrushStroke(
                    terrainObject.mesh,
                    terrainCols,
                    terrainRows,
                    sculptCenterLocal,
                    terrainBrushRadius_,
                    terrainBrushStrength_,
                    terrainFlattenHeight_,
                    terrainSculptMode_);
                terrainSculptLastLocalCenter_ = sculptCenterLocal;
                terrainSculptHasLastSample_ = true;
                meshCacheValid_ = false;
                sceneDirty_ = true;
            }

            return;
        }
    }
    else if (terrainSculpting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        terrainSculpting_ = false;
        terrainSculptHasLastSample_ = false;
        dragUndoPushed_ = false;
    }
    else if (!selectedMeshIsTerrain)
    {
        terrainBrushPreviewValid_ = false;
    }

    if (boxSelecting_)
    {
        boxSelectCurrent_ = mouseScreen;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (boxSelectViewIndex_ >= 0 && boxSelectViewIndex_ < activeViewCount_)
            {
                if (boxSelectFaces_)
                {
                    const std::vector<int> picked = PickFacesInOrthoRect(views_[boxSelectViewIndex_], boxSelectStart_, boxSelectCurrent_);
                    if (boxSelectToggle_)
                    {
                        for (int pickedIndex : picked)
                            toggleIndexSelection(selectedFaceIndices_, pickedIndex);
                        selectedFaceIndex_ = selectedFaceIndices_.empty() ? -1 : selectedFaceIndices_.front();
                    }
                    else if (boxSelectAdditive_)
                    {
                        if (selectedFaceIndices_.empty() && !picked.empty())
                            selectedFaceIndex_ = picked.front();
                        addUniqueIndices(selectedFaceIndices_, picked);
                    }
                    else
                    {
                        selectedFaceIndices_ = picked;
                        selectedFaceIndex_ = picked.empty() ? -1 : picked.front();
                    }
                    if (!selectedFaceIndices_.empty())
                    {
                        std::sort(selectedFaceIndices_.begin(), selectedFaceIndices_.end());
                        selectedFaceIndices_.erase(std::unique(selectedFaceIndices_.begin(), selectedFaceIndices_.end()), selectedFaceIndices_.end());
                    }
                    selectedVertexIndices_.clear();
                    selectedEntityIndex_ = -1;
                }
                else if (boxSelectVertices_)
                {
                    const std::vector<int> picked = PickVerticesInOrthoRect(views_[boxSelectViewIndex_], boxSelectStart_, boxSelectCurrent_);
                    if (boxSelectToggle_)
                    {
                        for (int pickedIndex : picked)
                        {
                            const auto it = std::find(selectedVertexIndices_.begin(), selectedVertexIndices_.end(), pickedIndex);
                            if (it != selectedVertexIndices_.end())
                                selectedVertexIndices_.erase(it);
                            else
                                selectedVertexIndices_.push_back(pickedIndex);
                        }
                    }
                    else if (boxSelectAdditive_)
                    {
                        for (int pickedIndex : picked)
                        {
                            if (!IsVertexSelected(pickedIndex))
                                selectedVertexIndices_.push_back(pickedIndex);
                        }
                    }
                    else
                    {
                        selectedVertexIndices_ = picked;
                    }
                    std::sort(selectedVertexIndices_.begin(), selectedVertexIndices_.end());
                    selectedVertexIndices_.erase(std::unique(selectedVertexIndices_.begin(), selectedVertexIndices_.end()), selectedVertexIndices_.end());
                }
                else
                {
                    const std::vector<int> picked = PickMeshesInOrthoRect(views_[boxSelectViewIndex_], boxSelectStart_, boxSelectCurrent_);
                    if (boxSelectToggle_)
                    {
                        for (int pickedIndex : picked)
                        {
                            const auto it = std::find(selectedMeshIndices_.begin(), selectedMeshIndices_.end(), pickedIndex);
                            if (it != selectedMeshIndices_.end())
                                selectedMeshIndices_.erase(it);
                            else
                                selectedMeshIndices_.push_back(pickedIndex);
                        }
                        if (!selectedMeshIndices_.empty())
                            selectedMeshIndex_ = selectedMeshIndices_.front();
                        else
                            selectedMeshIndex_ = -1;
                    }
                    else if (boxSelectAdditive_)
                    {
                        if (selectedMeshIndices_.empty() && !picked.empty())
                            selectedMeshIndex_ = picked.front();
                        for (int pickedIndex : picked)
                        {
                            if (!IsMeshSelected(pickedIndex))
                                selectedMeshIndices_.push_back(pickedIndex);
                        }
                    }
                    else
                    {
                        selectedMeshIndices_ = picked;
                        selectedMeshIndex_ = picked.empty() ? -1 : picked.front();
                    }

                    if (!selectedMeshIndices_.empty())
                    {
                        std::sort(selectedMeshIndices_.begin(), selectedMeshIndices_.end());
                        selectedMeshIndices_.erase(std::unique(selectedMeshIndices_.begin(), selectedMeshIndices_.end()), selectedMeshIndices_.end());
                    }
                    selectedVertexIndices_.clear();
                    selectedFaceIndex_ = -1;
                    selectedFaceIndices_.clear();
                }
            }
            boxSelecting_ = false;
            boxSelectViewIndex_ = -1;
            boxSelectFaces_ = false;
            boxSelectVertices_ = false;
            boxSelectAdditive_ = false;
            boxSelectToggle_ = false;
        }
    }

    if (draggingVerticesInView_)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            leftDragging &&
            hovered->type == dragViewType_ &&
            selectedMeshIndex_ >= 0 &&
            selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            LevelMeshObject& object = scene_.meshObjects()[selectedMeshIndex_];
            const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
            const glm::mat4 inverseModel = glm::inverse(modelMatrix);
            const glm::vec3 hoveredWorld = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen);
            const glm::vec3 objectDelta = ApplyViewDelta(hoveredWorld - dragStartWorld_, dragViewType_);
            for (std::size_t i = 0; i < selectedVertexIndices_.size() && i < dragStartVertexPositions_.size(); ++i)
            {
                const int vertexIndex = selectedVertexIndices_[i];
                if (vertexIndex < 0 || vertexIndex >= static_cast<int>(object.mesh.vertices().size()))
                    continue;
                const glm::vec3 startLocal = dragStartVertexPositions_[i];
                const glm::vec3 startWorld = glm::vec3(modelMatrix * glm::vec4(startLocal, 1.0f));
                const glm::vec3 movedWorld = startWorld + objectDelta;
                glm::vec3 newLocal = glm::vec3(inverseModel * glm::vec4(movedWorld, 1.0f));
                if (snapEnabled_)
                {
                    newLocal.x = std::round(newLocal.x / gridSize_) * gridSize_;
                    newLocal.y = std::round(newLocal.y / gridSize_) * gridSize_;
                    newLocal.z = std::round(newLocal.z / gridSize_) * gridSize_;
                }
                if (snapToGeometry_)
                {
                    const glm::vec3 worldCandidate = glm::vec3(modelMatrix * glm::vec4(newLocal, 1.0f));
                    glm::vec3 snapped;
                    if (snapToNearestVertex(scene_, selectedMeshIndex_, worldCandidate, gridSize_, snapped))
                        newLocal = glm::vec3(inverseModel * glm::vec4(snapped, 1.0f));
                }
                object.mesh.verticesMutable()[(size_t)vertexIndex].position = newLocal;
            }
            meshCacheValid_ = false;
            sceneDirty_ = true;
        }
        else
        {
            draggingVerticesInView_ = false;
            dragUndoPushed_ = false;
            dragStartVertexPositions_.clear();
        }
    }

    if (draggingFaceInView_)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            leftDragging &&
            hovered->type == dragViewType_ &&
            selectedMeshIndex_ >= 0 &&
            selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            LevelMeshObject& object = scene_.meshObjects()[selectedMeshIndex_];
            const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
            const glm::mat4 inverseModel = glm::inverse(modelMatrix);
            const glm::vec3 hoveredWorld = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen);
            const glm::vec3 objectDelta = ApplyViewDelta(hoveredWorld - dragStartWorld_, dragViewType_);
            for (std::size_t i = 0; i < dragFaceVertexIndices_.size() && i < dragStartVertexPositions_.size(); ++i)
            {
                const int vertexIndex = dragFaceVertexIndices_[i];
                if (vertexIndex < 0 || vertexIndex >= static_cast<int>(object.mesh.vertices().size()))
                    continue;
                const glm::vec3 startLocal = dragStartVertexPositions_[i];
                const glm::vec3 startWorld = glm::vec3(modelMatrix * glm::vec4(startLocal, 1.0f));
                const glm::vec3 movedWorld = startWorld + objectDelta;
                glm::vec3 newLocal = glm::vec3(inverseModel * glm::vec4(movedWorld, 1.0f));
                if (snapEnabled_)
                {
                    newLocal.x = std::round(newLocal.x / gridSize_) * gridSize_;
                    newLocal.y = std::round(newLocal.y / gridSize_) * gridSize_;
                    newLocal.z = std::round(newLocal.z / gridSize_) * gridSize_;
                }
                if (snapToGeometry_)
                {
                    const glm::vec3 worldCandidate = glm::vec3(modelMatrix * glm::vec4(newLocal, 1.0f));
                    glm::vec3 snapped;
                    if (snapToNearestVertex(scene_, selectedMeshIndex_, worldCandidate, gridSize_, snapped))
                        newLocal = glm::vec3(inverseModel * glm::vec4(snapped, 1.0f));
                }
                object.mesh.verticesMutable()[(size_t)vertexIndex].position = newLocal;
            }
            meshCacheValid_ = false;
            sceneDirty_ = true;
        }
        else
        {
            draggingFaceInView_ = false;
            dragUndoPushed_ = false;
            dragFaceVertexIndices_.clear();
            dragStartVertexPositions_.clear();
        }
    }

    if (draggingFaceExtrudeInView_)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            leftDragging &&
            hovered->type == dragViewType_ &&
            selectedMeshIndex_ >= 0 &&
            selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            LevelMeshObject& object = scene_.meshObjects()[selectedMeshIndex_];
            const glm::vec3 hoveredWorld = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen);
            const glm::vec3 visibleWorldDelta = ApplyViewDelta(hoveredWorld - dragStartWorld_, dragViewType_);
            const glm::vec3 worldNormal = worldNormalForLocalDirection(object, dragFaceNormalLocal_);
            const glm::vec3 visibleWorldNormal = worldNormal * orthoVisibleAxisMask(dragViewType_);
            float worldDistance = glm::dot(visibleWorldDelta, worldNormal);

            if (glm::length2(visibleWorldNormal) <= 1e-6f)
            {
                const float pixelsToWorld = (hovered->rect.h > 0)
                    ? ((hovered->orthoSize * 2.0f) / static_cast<float>(hovered->rect.h))
                    : 1.0f;
                worldDistance = -dragMouseDelta.y * pixelsToWorld;
            }

            const glm::vec3 worldUnitDirection = glm::vec3(meshObjectModelMatrix(object) * glm::vec4(dragFaceNormalLocal_, 0.0f));
            const float worldUnitLength = std::max(1e-4f, glm::length(worldUnitDirection));
            const float localDistance = worldDistance / worldUnitLength;

            for (std::size_t i = 0; i < dragFaceVertexIndices_.size() && i < dragStartVertexPositions_.size(); ++i)
            {
                const int vertexIndex = dragFaceVertexIndices_[i];
                if (vertexIndex < 0 || vertexIndex >= static_cast<int>(object.mesh.vertices().size()))
                    continue;
                object.mesh.verticesMutable()[(size_t)vertexIndex].position = dragStartVertexPositions_[i] + dragFaceNormalLocal_ * localDistance;
            }
            meshCacheValid_ = false;
            sceneDirty_ = true;
        }
        else
        {
            draggingFaceExtrudeInView_ = false;
            dragUndoPushed_ = false;
            dragFaceVertexIndices_.clear();
            dragStartVertexPositions_.clear();
        }
    }

    if (draggingObjectInView_)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            leftDragging &&
            hovered->type == dragViewType_ &&
            selectedMeshIndex_ >= 0 &&
            selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            if (dragTool_ == DragTool::Move)
            {
                const glm::vec3 hoveredWorld = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen);
                const glm::vec3 objectDelta = ApplyViewDelta(hoveredWorld - dragStartWorld_, dragViewType_);
                for (const MeshTransformState& state : dragStartMeshStates_)
                {
                    if (state.index < 0 || state.index >= static_cast<int>(scene_.meshObjects().size()))
                        continue;
                    LevelMeshObject& object = scene_.meshObjects()[(size_t)state.index];
                    glm::vec3 newPos = state.position + objectDelta;
                    if (snapEnabled_)
                    {
                        newPos.x = std::round(newPos.x / gridSize_) * gridSize_;
                        newPos.y = std::round(newPos.y / gridSize_) * gridSize_;
                        newPos.z = std::round(newPos.z / gridSize_) * gridSize_;
                    }
                    object.position = newPos;
                }
                sceneDirty_ = true;
            }
            else if (dragTool_ == DragTool::Scale)
            {
                const glm::vec3 groupScale = orthoGroupScaleFromDrag(dragMouseDelta, dragViewType_, dragStartObjectScale_);
                for (const MeshTransformState& state : dragStartMeshStates_)
                {
                    if (state.index < 0 || state.index >= static_cast<int>(scene_.meshObjects().size()))
                        continue;
                    LevelMeshObject& object = scene_.meshObjects()[(size_t)state.index];
                    object.scale = glm::max(state.scale * groupScale, glm::vec3(0.01f));

                    const glm::vec3 startPivotWorld = meshPivotWorldPosition(state);
                    const glm::vec3 scaledOffset = (startPivotWorld - dragStartSelectionCenter_) * groupScale;
                    const glm::vec3 newPivotWorld = dragStartSelectionCenter_ + scaledOffset;
                    object.position = newPivotWorld - object.pivot;
                }
                sceneDirty_ = true;
            }
            else if (dragTool_ == DragTool::Rotate)
            {
                const float rotationSpeed = 0.5f;
                float rotationDeltaDegrees = 0.0f;
                switch (dragViewType_)
                {
                case ViewType::Top:
                case ViewType::Bottom:
                case ViewType::Front:
                case ViewType::Back:
                case ViewType::Left:
                case ViewType::Right:
                    rotationDeltaDegrees = dragMouseDelta.x * rotationSpeed;
                    break;
                case ViewType::Perspective:
                    break;
                }

                const glm::quat deltaRotation = glm::angleAxis(glm::radians(rotationDeltaDegrees),
                                                               orthoRotationAxis(dragViewType_));
                for (const MeshTransformState& state : dragStartMeshStates_)
                {
                    if (state.index < 0 || state.index >= static_cast<int>(scene_.meshObjects().size()))
                        continue;

                    LevelMeshObject& object = scene_.meshObjects()[(size_t)state.index];
                    glm::vec3 newRotation = state.rotationEuler;
                    switch (dragViewType_)
                    {
                    case ViewType::Top:
                    case ViewType::Bottom:
                        newRotation.y = state.rotationEuler.y + rotationDeltaDegrees;
                        break;
                    case ViewType::Front:
                    case ViewType::Back:
                        newRotation.z = state.rotationEuler.z + rotationDeltaDegrees;
                        break;
                    case ViewType::Left:
                    case ViewType::Right:
                        newRotation.x = state.rotationEuler.x + rotationDeltaDegrees;
                        break;
                    case ViewType::Perspective:
                        break;
                    }

                    const glm::vec3 startPivotWorld = meshPivotWorldPosition(state);
                    const glm::vec3 rotatedOffset = deltaRotation * (startPivotWorld - dragStartSelectionCenter_);
                    const glm::vec3 newPivotWorld = dragStartSelectionCenter_ + rotatedOffset;
                    object.position = newPivotWorld - object.pivot;
                    object.rotationEuler = normalizeEulerDegrees(newRotation);
                }
                sceneDirty_ = true;
            }
        }
        else
        {
            draggingObjectInView_ = false;
            dragTool_ = DragTool::None;
            dragUndoPushed_ = false;
            dragStartMeshStates_.clear();
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing() &&
        hovered->type == ViewType::Perspective)
    {
        if (selectionMode_ == SelectionMode::Vertex)
        {
            int pickedMeshIndex = -1;
            const int pickedVertexIndex = PickVertexInPerspectiveView(*hovered, mouseScreen, pickedMeshIndex);
            if (pickedVertexIndex >= 0 && pickedMeshIndex >= 0)
            {
                if (selectedMeshIndex_ != pickedMeshIndex)
                {
                    SetSingleSelectedMesh(pickedMeshIndex);
                    selectionMode_ = SelectionMode::Vertex;
                    selectedVertexIndices_ = {pickedVertexIndex};
                }
                else if (ctrlDown)
                {
                    toggleIndexSelection(selectedVertexIndices_, pickedVertexIndex);
                }
                else if (shiftDown)
                {
                    if (!IsVertexSelected(pickedVertexIndex))
                        selectedVertexIndices_.push_back(pickedVertexIndex);
                }
                else
                {
                    selectedVertexIndices_ = {pickedVertexIndex};
                }

                std::sort(selectedVertexIndices_.begin(), selectedVertexIndices_.end());
                selectedVertexIndices_.erase(std::unique(selectedVertexIndices_.begin(), selectedVertexIndices_.end()),
                                            selectedVertexIndices_.end());
                selectedFaceIndex_ = -1;
                selectedFaceIndices_.clear();
                selectedEntityIndex_ = -1;
            }
            else if (currentTool_ == Tool::Select && !ctrlDown && !shiftDown)
            {
                selectedVertexIndices_.clear();
            }
        }
        else if (selectionMode_ == SelectionMode::Face)
        {
            int pickedMeshIndex = -1;
            const int pickedFaceIndex = PickFaceInPerspectiveView(*hovered, mouseScreen, pickedMeshIndex);
            if (pickedFaceIndex >= 0 && pickedMeshIndex >= 0)
            {
                if (selectedMeshIndex_ != pickedMeshIndex)
                {
                    SetSingleSelectedMesh(pickedMeshIndex);
                    selectionMode_ = SelectionMode::Face;
                    SetSingleSelectedFace(pickedFaceIndex);
                }
                else if (ctrlDown)
                {
                    toggleIndexSelection(selectedFaceIndices_, pickedFaceIndex);
                    selectedFaceIndex_ = selectedFaceIndices_.empty() ? -1 : pickedFaceIndex;
                    SyncSelectedFaces();
                }
                else if (shiftDown)
                {
                    if (!IsFaceSelected(pickedFaceIndex))
                        selectedFaceIndices_.push_back(pickedFaceIndex);
                    selectedFaceIndex_ = pickedFaceIndex;
                    SyncSelectedFaces();
                }
                else
                {
                    SetSingleSelectedFace(pickedFaceIndex);
                }
                selectedEntityIndex_ = -1;
            }
            else if (currentTool_ == Tool::Select && !ctrlDown && !shiftDown)
            {
                selectedFaceIndex_ = -1;
                selectedFaceIndices_.clear();
            }
        }
        else
        {
            const int picked = PickMeshInPerspectiveView(*hovered, mouseScreen);
            if (picked >= 0)
            {
                if (ctrlDown)
                {
                    const auto it = std::find(selectedMeshIndices_.begin(), selectedMeshIndices_.end(), picked);
                    if (it != selectedMeshIndices_.end())
                    {
                        selectedMeshIndices_.erase(it);
                        selectedMeshIndex_ = selectedMeshIndices_.empty() ? -1 : selectedMeshIndices_.front();
                    }
                    else
                    {
                        selectedMeshIndices_.push_back(picked);
                        std::sort(selectedMeshIndices_.begin(), selectedMeshIndices_.end());
                        selectedMeshIndex_ = picked;
                    }
                    selectedEntityIndex_ = -1;
                }
                else if (shiftDown)
                {
                    if (!IsMeshSelected(picked))
                        selectedMeshIndices_.push_back(picked);
                    std::sort(selectedMeshIndices_.begin(), selectedMeshIndices_.end());
                    selectedMeshIndex_ = picked;
                    selectedEntityIndex_ = -1;
                }
                else
                {
                    SetSingleSelectedMesh(picked);
                }
            }
            else if (currentTool_ == Tool::Select && !ctrlDown && !shiftDown)
            {
                selectedMeshIndices_.clear();
                selectedMeshIndex_ = -1;
                selectedFaceIndex_ = -1;
                selectedFaceIndices_.clear();
            }
        }
    }

    if (!boxSelecting_ &&
        hovered->type != ViewType::Perspective &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // Place vertex mode: click in ortho view = add vertex
        if (currentTool_ == Tool::Select &&
            placeVertexMode_ && selectionMode_ == SelectionMode::Vertex &&
            selectedMeshIndex_ >= 0 &&
            selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            PushUndoState();
            LevelMeshObject& object = scene_.meshObjects()[(size_t)selectedMeshIndex_];
            const glm::mat4 inverseModel = glm::inverse(meshObjectModelMatrix(object));
            const glm::vec3 worldPos = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen);
            glm::vec3 localPos = glm::vec3(inverseModel * glm::vec4(worldPos, 1.0f));
            if (snapEnabled_)
            {
                localPos.x = std::round(localPos.x / gridSize_) * gridSize_;
                localPos.y = std::round(localPos.y / gridSize_) * gridSize_;
                localPos.z = std::round(localPos.z / gridSize_) * gridSize_;
            }
            if (snapToGeometry_)
            {
                const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
                const glm::vec3 worldCandidate = glm::vec3(modelMatrix * glm::vec4(localPos, 1.0f));
                glm::vec3 snapped;
                if (snapToNearestVertex(scene_, selectedMeshIndex_, worldCandidate, gridSize_, snapped))
                    localPos = glm::vec3(inverseModel * glm::vec4(snapped, 1.0f));
            }
            EditableVertex newVtx;
            newVtx.position = localPos;
            newVtx.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            object.mesh.verticesMutable().push_back(newVtx);
            const int newIdx = static_cast<int>(object.mesh.vertexCount()) - 1;
            selectedVertexIndices_.push_back(newIdx);
            sceneDirty_ = true;
        }
        else if (currentTool_ == Tool::Select)
        {
            boxSelecting_ = true;
            boxSelectStart_ = mouseScreen;
            boxSelectCurrent_ = mouseScreen;
            boxSelectViewIndex_ = activeViewIndex_;
            boxSelectFaces_ = selectionMode_ == SelectionMode::Face;
            boxSelectVertices_ = selectionMode_ == SelectionMode::Vertex;
            boxSelectAdditive_ = shiftDown;
            boxSelectToggle_ = ctrlDown;
        }
    }

    if (!draggingVerticesInView_ &&
        !draggingFaceInView_ &&
        !draggingFaceExtrudeInView_ &&
        !draggingObjectInView_ &&
        !boxSelecting_ &&
        selectionMode_ == SelectionMode::Vertex &&
        currentTool_ == Tool::Move &&
        hovered->type != ViewType::Perspective &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        !ctrlDown &&
        !shiftDown &&
        leftDragging)
    {
        const int pickedVertex = PickVertexInOrthoView(*hovered, mouseScreen);
        if (pickedVertex >= 0)
        {
            if (!IsVertexSelected(pickedVertex))
                selectedVertexIndices_ = {pickedVertex};

            if (!selectedVertexIndices_.empty())
            {
                if (!dragUndoPushed_)
                {
                    PushUndoState();
                    dragUndoPushed_ = true;
                }
                draggingVerticesInView_ = true;
                dragViewType_ = hovered->type;
                dragStartMouse_ = mouseScreen - delta;
                dragStartWorld_ = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen - delta);
                dragStartVertexPositions_.clear();
                const LevelMeshObject& object = scene_.meshObjects()[selectedMeshIndex_];
                for (int vertexIndex : selectedVertexIndices_)
                {
                    if (vertexIndex >= 0 && vertexIndex < static_cast<int>(object.mesh.vertices().size()))
                        dragStartVertexPositions_.push_back(object.mesh.vertices()[(size_t)vertexIndex].position);
                }
            }
        }
    }

    if (!draggingFaceInView_ &&
        !draggingVerticesInView_ &&
        !draggingFaceExtrudeInView_ &&
        !draggingObjectInView_ &&
        !boxSelecting_ &&
        selectionMode_ == SelectionMode::Face &&
        currentTool_ == Tool::Move &&
        hovered->type != ViewType::Perspective &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        selectedFaceIndex_ >= 0 &&
        selectedFaceIndex_ < static_cast<int>(scene_.meshObjects()[selectedMeshIndex_].mesh.faces().size()) &&
        !ctrlDown &&
        !shiftDown &&
        leftDragging)
    {
        const LevelMeshObject& object = scene_.meshObjects()[selectedMeshIndex_];
        const EditableFace& face = object.mesh.faces()[(size_t)selectedFaceIndex_];
        if (!face.indices.empty())
        {
            if (!dragUndoPushed_)
            {
                PushUndoState();
                dragUndoPushed_ = true;
            }
            draggingFaceInView_ = true;
            dragViewType_ = hovered->type;
            dragStartMouse_ = mouseScreen - delta;
            dragStartWorld_ = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen - delta);
            dragFaceVertexIndices_.clear();
            dragStartVertexPositions_.clear();
            for (int vertexIndex : face.indices)
            {
                if (vertexIndex < 0 || vertexIndex >= static_cast<int>(object.mesh.vertices().size()))
                    continue;
                if (std::find(dragFaceVertexIndices_.begin(), dragFaceVertexIndices_.end(), vertexIndex) != dragFaceVertexIndices_.end())
                    continue;
                dragFaceVertexIndices_.push_back(vertexIndex);
                dragStartVertexPositions_.push_back(object.mesh.vertices()[(size_t)vertexIndex].position);
            }
            if (dragFaceVertexIndices_.empty())
            {
                draggingFaceInView_ = false;
                dragUndoPushed_ = false;
            }
        }
    }

    if (!draggingFaceExtrudeInView_ &&
        !draggingFaceInView_ &&
        !draggingVerticesInView_ &&
        !draggingObjectInView_ &&
        !boxSelecting_ &&
        selectionMode_ == SelectionMode::Face &&
        currentTool_ == Tool::Scale &&
        hovered->type != ViewType::Perspective &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        selectedFaceIndex_ >= 0 &&
        selectedFaceIndex_ < static_cast<int>(scene_.meshObjects()[selectedMeshIndex_].mesh.faces().size()) &&
        !ctrlDown &&
        !shiftDown &&
        leftDragging)
    {
        LevelMeshObject& object = scene_.meshObjects()[selectedMeshIndex_];
        const EditableFace& selectedFace = object.mesh.faces()[(size_t)selectedFaceIndex_];
        if (selectedFace.indices.size() >= 3)
        {
            if (!dragUndoPushed_)
            {
                PushUndoState();
                dragUndoPushed_ = true;
            }
            dragFaceNormalLocal_ = editableFaceNormal(object.mesh, selectedFace);
            if (extrudeMeshFace(object, selectedFaceIndex_, 0.0f))
            {
                draggingFaceExtrudeInView_ = true;
                dragViewType_ = hovered->type;
                dragStartMouse_ = mouseScreen - delta;
                dragStartWorld_ = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen - delta);
                dragFaceVertexIndices_.clear();
                dragStartVertexPositions_.clear();

                const EditableFace& extrudedFace = object.mesh.faces()[(size_t)selectedFaceIndex_];
                for (int vertexIndex : extrudedFace.indices)
                {
                    if (vertexIndex < 0 || vertexIndex >= static_cast<int>(object.mesh.vertices().size()))
                        continue;
                    dragFaceVertexIndices_.push_back(vertexIndex);
                    dragStartVertexPositions_.push_back(object.mesh.vertices()[(size_t)vertexIndex].position);
                }

                if (dragFaceVertexIndices_.empty())
                {
                    draggingFaceExtrudeInView_ = false;
                    dragUndoPushed_ = false;
                }
                else
                {
                    sceneDirty_ = true;
                }
            }
        }
    }

    if (!draggingObjectInView_ &&
        !draggingVerticesInView_ &&
        !draggingFaceInView_ &&
        !draggingFaceExtrudeInView_ &&
        !boxSelecting_ &&
        (currentTool_ == Tool::Move || currentTool_ == Tool::Scale || currentTool_ == Tool::Rotate) &&
        selectionMode_ == SelectionMode::Object &&
        hovered->type != ViewType::Perspective &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        !ctrlDown &&
        !shiftDown &&
        leftDragging)
    {
        if (!dragUndoPushed_)
        {
            PushUndoState();
            dragUndoPushed_ = true;
        }
        draggingObjectInView_ = true;
        dragTool_ = (currentTool_ == Tool::Move) ? DragTool::Move :
                    (currentTool_ == Tool::Scale) ? DragTool::Scale :
                    (currentTool_ == Tool::Rotate) ? DragTool::Rotate :
                    DragTool::None;
        dragViewType_ = hovered->type;
        dragStartMouse_ = mouseScreen - delta;
        dragStartWorld_ = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen - delta);
        dragStartMeshStates_ = collectSelectedMeshTransformStates(scene_, selectedMeshIndices_, selectedMeshIndex_);
        dragStartSelectionCenter_ = selectionBoundsCenter(scene_, dragStartMeshStates_);
        dragStartObjectPosition_ = scene_.meshObjects()[selectedMeshIndex_].position;
        dragStartObjectRotation_ = scene_.meshObjects()[selectedMeshIndex_].rotationEuler;
        dragStartObjectScale_ = scene_.meshObjects()[selectedMeshIndex_].scale;
    }

    const float wheel = io.MouseWheel;
    if (std::abs(wheel) > 1e-4f &&
        !draggingObjectInView_ &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing())
    {
        if (hovered->type == ViewType::Perspective)
            hovered->perspectiveDistance = std::clamp(
                hovered->perspectiveDistance - wheel * 32.0f,
                std::max(0.01f, perspectiveMinDistance_),
                4096.0f);
        else
            hovered->orthoSize = std::clamp(hovered->orthoSize - wheel * (hovered->orthoSize * 0.1f), 8.0f, 4096.0f);
    }

    if (hovered->type == ViewType::Perspective &&
        shiftDown &&
        rightDragging &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing())
    {
        hovered->perspectiveYaw += delta.x * 0.35f;
        hovered->perspectivePitch = std::clamp(hovered->perspectivePitch - delta.y * 0.25f, -89.0f, 89.0f);
    }

    if (hovered->type != ViewType::Perspective &&
        ctrlDown &&
        leftDragging &&
        !draggingObjectInView_ &&
        !boxSelecting_)
    {
        if (hovered->rect.w > 0 && hovered->rect.h > 0)
        {
            const float aspect = static_cast<float>(hovered->rect.w) / static_cast<float>(hovered->rect.h);
            const float halfH = hovered->orthoSize;
            const float halfW = halfH * aspect;
            const float worldPerPixelX = (2.0f * halfW) / static_cast<float>(hovered->rect.w);
            const float worldPerPixelY = (2.0f * halfH) / static_cast<float>(hovered->rect.h);

            switch (hovered->type)
            {
            case ViewType::Top:
                hovered->focus.x -= delta.x * worldPerPixelX;
                hovered->focus.z += delta.y * worldPerPixelY;
                break;
            case ViewType::Bottom:
                hovered->focus.x -= delta.x * worldPerPixelX;
                hovered->focus.z -= delta.y * worldPerPixelY;
                break;
            case ViewType::Front:
                hovered->focus.x -= delta.x * worldPerPixelX;
                hovered->focus.y += delta.y * worldPerPixelY;
                break;
            case ViewType::Back:
                hovered->focus.x += delta.x * worldPerPixelX;
                hovered->focus.y += delta.y * worldPerPixelY;
                break;
            case ViewType::Left:
                hovered->focus.z -= delta.x * worldPerPixelX;
                hovered->focus.y += delta.y * worldPerPixelY;
                break;
            case ViewType::Right:
                hovered->focus.z += delta.x * worldPerPixelX;
                hovered->focus.y += delta.y * worldPerPixelY;
                break;
            case ViewType::Perspective:
                break;
            }
        }
    }

    if (hovered->type == ViewType::Perspective &&
        ctrlDown &&
        rightDragging &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing())
    {
        const float yaw = glm::radians(hovered->perspectiveYaw);
        const float pitch = glm::radians(hovered->perspectivePitch);
        const glm::vec3 offset(
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::cos(yaw));
        const glm::vec3 forward = glm::normalize(-offset);
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (glm::length2(right) < 1e-8f)
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));
        const float panScale = glm::max(hovered->perspectiveDistance * 0.0018f, 0.01f);
        hovered->focus += (-right * delta.x + up * delta.y) * panScale;
    }
}

void LevelEditorApp::DrawViewportToolbar()
{
    const Tool tools[] = {Tool::Select, Tool::Move, Tool::Scale, Tool::Rotate};
    for (Tool tool : tools)
    {
        if (tool != tools[0])
            ImGui::SameLine();

        const bool active = tool == currentTool_;
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.40f, 0.80f, 1.0f));
        }

        if (ImGui::SmallButton(toolName(tool)))
            currentTool_ = tool;

        if (active)
            ImGui::PopStyleColor(3);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::BeginDisabled(undoStack_.empty());
    if (ImGui::Button("Undo", ImVec2(70.0f, 0.0f)))
        PerformUndo();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(redoStack_.empty());
    if (ImGui::Button("Redo", ImVec2(70.0f, 0.0f)))
        PerformRedo();
    ImGui::EndDisabled();
}

void LevelEditorApp::DrawViewportContextMenu()
{
    if (!ImGui::BeginPopup("ViewContextMenu"))
        return;

    if (contextViewIndex_ >= 0 && contextViewIndex_ < activeViewCount_)
    {
        LevelEditorView& view = views_[contextViewIndex_];
        ImGui::Text("View: %s", view.label);
        ImGui::Separator();

        if (ImGui::BeginMenu("View Type"))
        {
            const ViewType types[] = {
                ViewType::Top, ViewType::Bottom, ViewType::Front, ViewType::Back,
                ViewType::Left, ViewType::Right, ViewType::Perspective
            };
            for (ViewType type : types)
            {
                const bool selected = view.type == type;
                if (ImGui::MenuItem(viewTypeName(type), nullptr, selected))
                {
                    view.type = type;
                    view.label = viewTypeName(type);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Reset Camera"))
        {
            view.orthoSize = 256.0f;
            view.perspectiveDistance = 720.0f;
            view.perspectiveYaw = 45.0f;
            view.perspectivePitch = 28.0f;
            view.focus = glm::vec3(0.0f, 64.0f, 0.0f);
        }

        ImGui::Separator();
        if (ImGui::BeginMenu("Render Mode"))
        {
            if (ImGui::MenuItem("Solid", nullptr, view.renderMode == RenderMode::Solid))
                view.renderMode = RenderMode::Solid;
            if (ImGui::MenuItem("Wireframe", nullptr, view.renderMode == RenderMode::Wireframe))
                view.renderMode = RenderMode::Wireframe;
            if (ImGui::MenuItem("Textured", nullptr, view.renderMode == RenderMode::Textured))
                view.renderMode = RenderMode::Textured;
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Show Grid", nullptr, showGrid_))
            showGrid_ = !showGrid_;
        if (ImGui::MenuItem("Snap", nullptr, snapEnabled_))
            snapEnabled_ = !snapEnabled_;
        if (ImGui::MenuItem("Snap to Geometry", nullptr, snapToGeometry_))
            snapToGeometry_ = !snapToGeometry_;
    }

    ImGui::EndPopup();
}

void LevelEditorApp::DrawTransformGizmo()
{
    constexpr float kViewHeaderHeight = 26.0f;

    if (currentTool_ != Tool::Move &&
        currentTool_ != Tool::Rotate &&
        currentTool_ != Tool::Scale)
    {
        gizmoWasUsing_ = false;
        return;
    }

    // Determine what we're transforming: mesh or entity
    const bool hasMesh = (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()));
    const bool hasEntity = (!hasMesh && selectedEntityIndex_ >= 0 && selectedEntityIndex_ < static_cast<int>(scene_.entities().size()));
    const bool hasVertexSelection = hasMesh &&
        selectionMode_ == SelectionMode::Vertex &&
        currentTool_ == Tool::Move &&
        !selectedVertexIndices_.empty();
    if (hasMesh && selectionMode_ != SelectionMode::Object && !hasVertexSelection)
    {
        gizmoWasUsing_ = false;
        return;
    }
    if (!hasMesh && !hasEntity)
    {
        gizmoWasUsing_ = false;
        return;
    }

    int gizmoViewIndex = -1;
    // Prefer active view, fall back to any perspective view
    if (activeViewIndex_ >= 0 && activeViewIndex_ < activeViewCount_)
    {
        gizmoViewIndex = activeViewIndex_;
    }
    else for (int i = 0; i < activeViewCount_; ++i)
    {
        if (views_[i].type == ViewType::Perspective)
        {
            gizmoViewIndex = i;
            break;
        }
    }
    if (gizmoViewIndex < 0)
    {
        gizmoWasUsing_ = false;
        return;
    }

    LevelEditorView& view = views_[gizmoViewIndex];
    if (view.rect.w <= 0 || view.rect.h <= 0)
    {
        gizmoWasUsing_ = false;
        return;
    }

    const std::vector<MeshTransformState> selectedMeshStates =
        hasMesh ? collectSelectedMeshTransformStates(scene_, selectedMeshIndices_, selectedMeshIndex_)
                : std::vector<MeshTransformState>{};
    const bool multiMeshSelection = hasMesh && !hasVertexSelection && selectedMeshStates.size() > 1;
    const glm::vec3 selectionCenter = multiMeshSelection
        ? selectionBoundsCenter(scene_, selectedMeshStates)
        : glm::vec3(0.0f);

    // Build gizmo matrix from the selected object or group pivot
    glm::mat4 gizmoMatrix(1.0f);
    bool entityHasDirection = false;
    if (hasMesh)
    {
        LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];
        if (hasVertexSelection)
        {
            const glm::mat4 modelMatrix = meshObjectModelMatrix(meshObject);
            glm::vec3 vertexCenter(0.0f);
            int validCount = 0;
            for (int vertexIndex : selectedVertexIndices_)
            {
                if (vertexIndex < 0 || vertexIndex >= static_cast<int>(meshObject.mesh.vertices().size()))
                    continue;
                vertexCenter += glm::vec3(modelMatrix * glm::vec4(meshObject.mesh.vertices()[(size_t)vertexIndex].position, 1.0f));
                ++validCount;
            }
            if (validCount <= 0)
            {
                gizmoWasUsing_ = false;
                return;
            }
            vertexCenter /= static_cast<float>(validCount);
            gizmoMatrix = glm::translate(glm::mat4(1.0f), vertexCenter);
        }
        else if (multiMeshSelection)
        {
            gizmoMatrix = glm::translate(glm::mat4(1.0f), selectionCenter);
        }
        else
        {
            gizmoMatrix = meshObjectPivotFrameMatrix(meshObject);
        }
    }
    else
    {
        LevelEntityObject& ent = scene_.entities()[selectedEntityIndex_];
        entityHasDirection = (ent.type == LevelEntityType::PlayerStart) ||
            (ent.type == LevelEntityType::Door &&
             (ent.doorType == DoorType::Slide || ent.doorType == DoorType::Shutter)) ||
            (ent.type == LevelEntityType::Light &&
             (ent.lightType == LightType::Directional || ent.lightType == LightType::Spot));
        if (entityHasDirection)
        {
            // Build rotation from direction vector: direction is where -Z points
            glm::vec3 dir = glm::length(ent.direction) > 1e-6f ? glm::normalize(ent.direction) : glm::vec3(0, -1, 0);
            // Create rotation that maps (0,0,-1) to dir
            glm::vec3 up = (std::abs(dir.y) > 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::mat4 lookMat = glm::lookAt(glm::vec3(0), dir, up);
            // lookAt gives view matrix (inverted), we need the model rotation
            glm::mat3 rot = glm::transpose(glm::mat3(lookMat));
            gizmoMatrix = glm::translate(glm::mat4(1.0f), ent.position) * glm::mat4(rot);
        }
        else
        {
            gizmoMatrix = glm::translate(glm::mat4(1.0f), ent.position);
        }
    }

    // Entities: Move always, Rotate for Dir/Spot only, no Scale
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (hasMesh)
    {
        if (currentTool_ == Tool::Rotate)
            operation = ImGuizmo::ROTATE;
        else if (currentTool_ == Tool::Scale)
            operation = ImGuizmo::SCALE;
    }
    else if (entityHasDirection && currentTool_ == Tool::Rotate)
    {
        operation = ImGuizmo::ROTATE;
    }

    ImGuizmo::SetOrthographic(view.type != ViewType::Perspective);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(static_cast<float>(view.rect.x),
                      static_cast<float>(view.rect.y) + kViewHeaderHeight,
                      static_cast<float>(view.rect.w),
                      std::max(1.0f, static_cast<float>(view.rect.h) - kViewHeaderHeight));

    float snap[3] = {gridSize_, gridSize_, gridSize_};
    if (operation == ImGuizmo::ROTATE)
        snap[0] = snap[1] = snap[2] = 1.0f;
    else if (operation == ImGuizmo::SCALE)
        snap[0] = snap[1] = snap[2] = 0.1f;

    glm::mat4 deltaMatrix(1.0f);

    ImGuizmo::Manipulate(glm::value_ptr(view.camera.view),
                         glm::value_ptr(view.camera.projection),
                         operation,
                         ImGuizmo::WORLD,
                         glm::value_ptr(gizmoMatrix),
                         glm::value_ptr(deltaMatrix),
                         snapEnabled_ ? snap : nullptr);

    const bool usingNow = ImGuizmo::IsUsing();
    if (usingNow && !gizmoWasUsing_)
    {
        PushUndoState();
        if (hasVertexSelection)
        {
            LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];
            const glm::mat4 modelMatrix = meshObjectModelMatrix(meshObject);
            gizmoStartVertexPositions_.clear();
            gizmoVertexSelectionCenter_ = glm::vec3(0.0f);
            int validCount = 0;
            for (int vertexIndex : selectedVertexIndices_)
            {
                if (vertexIndex < 0 || vertexIndex >= static_cast<int>(meshObject.mesh.vertices().size()))
                    continue;
                const glm::vec3 localPos = meshObject.mesh.vertices()[(size_t)vertexIndex].position;
                gizmoStartVertexPositions_.push_back(localPos);
                gizmoVertexSelectionCenter_ += glm::vec3(modelMatrix * glm::vec4(localPos, 1.0f));
                ++validCount;
            }
            if (validCount > 0)
                gizmoVertexSelectionCenter_ /= static_cast<float>(validCount);
        }
        else if (multiMeshSelection)
        {
            gizmoMultiSelectionCenter_ = selectionCenter;
            gizmoStartMeshStates_ = selectedMeshStates;
        }
    }

    if (usingNow)
    {
        float t[3] = {0.0f, 0.0f, 0.0f};
        float r[3] = {0.0f, 0.0f, 0.0f};
        float s[3] = {1.0f, 1.0f, 1.0f};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);

        if (hasEntity)
        {
            LevelEntityObject& ent = scene_.entities()[selectedEntityIndex_];
            const glm::vec3 newPos(t[0], t[1], t[2]);
            bool changed = false;
            if (newPos != ent.position)
            {
                ent.position = newPos;
                changed = true;
            }
            // Extract direction from gizmo rotation (direction = where local -Z points)
            if (entityHasDirection && operation == ImGuizmo::ROTATE)
            {
                glm::mat3 rot(gizmoMatrix);
                glm::vec3 newDir = glm::normalize(rot * glm::vec3(0, 0, -1));
                if (glm::length(newDir - ent.direction) > 1e-4f)
                {
                    ent.direction = newDir;
                    changed = true;
                }
            }
            if (changed) sceneDirty_ = true;
        }
        else
        {
            if (hasVertexSelection)
            {
                LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];
                const glm::mat4 modelMatrix = meshObjectModelMatrix(meshObject);
                const glm::mat4 inverseModel = glm::inverse(modelMatrix);
                const glm::vec3 translationDelta = glm::vec3(t[0], t[1], t[2]) - gizmoVertexSelectionCenter_;
                if (!nearlyEqualVec3(translationDelta, glm::vec3(0.0f)))
                {
                    std::size_t startIndex = 0;
                    for (int vertexIndex : selectedVertexIndices_)
                    {
                        if (vertexIndex < 0 || vertexIndex >= static_cast<int>(meshObject.mesh.vertices().size()))
                            continue;
                        if (startIndex >= gizmoStartVertexPositions_.size())
                            break;
                        const glm::vec3 startLocal = gizmoStartVertexPositions_[startIndex++];
                        const glm::vec3 startWorld = glm::vec3(modelMatrix * glm::vec4(startLocal, 1.0f));
                        glm::vec3 newLocal = glm::vec3(inverseModel * glm::vec4(startWorld + translationDelta, 1.0f));
                        if (snapEnabled_)
                        {
                            newLocal.x = std::round(newLocal.x / gridSize_) * gridSize_;
                            newLocal.y = std::round(newLocal.y / gridSize_) * gridSize_;
                            newLocal.z = std::round(newLocal.z / gridSize_) * gridSize_;
                        }
                        meshObject.mesh.verticesMutable()[(size_t)vertexIndex].position = newLocal;
                    }
                    meshCacheValid_ = false;
                    sceneDirty_ = true;
                }
            }
            else if (multiMeshSelection)
            {
                float dt[3] = {0.0f, 0.0f, 0.0f};
                float dr[3] = {0.0f, 0.0f, 0.0f};
                float ds[3] = {1.0f, 1.0f, 1.0f};
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(deltaMatrix), dt, dr, ds);
                const glm::quat deltaRotation = glm::quat(glm::radians(glm::vec3(dr[0], dr[1], dr[2])));

                bool changed = false;
                if (operation == ImGuizmo::TRANSLATE)
                {
                    const glm::vec3 translationDelta(dt[0], dt[1], dt[2]);
                    if (!nearlyEqualVec3(translationDelta, glm::vec3(0.0f)))
                    {
                        for (const MeshTransformState& state : selectedMeshStates)
                        {
                            LevelMeshObject& object = scene_.meshObjects()[(size_t)state.index];
                            object.position += translationDelta;
                        }
                        changed = true;
                    }
                }
                else if (operation == ImGuizmo::ROTATE)
                {
                    const glm::vec3 pivotCenter = gizmoMultiSelectionCenter_;
                    if (!nearlyEqualVec3(glm::vec3(dr[0], dr[1], dr[2]), glm::vec3(0.0f)))
                    {
                        for (const MeshTransformState& state : gizmoStartMeshStates_)
                        {
                            LevelMeshObject& object = scene_.meshObjects()[(size_t)state.index];
                            const glm::quat currentRotation = glm::quat(glm::radians(state.rotationEuler));
                            object.rotationEuler = normalizeEulerDegrees(
                                glm::degrees(glm::eulerAngles(glm::normalize(deltaRotation * currentRotation))));

                            const glm::vec3 startPivotWorld = meshPivotWorldPosition(state);
                            const glm::vec3 rotatedOffset = deltaRotation * (startPivotWorld - pivotCenter);
                            const glm::vec3 newPivotWorld = pivotCenter + rotatedOffset;
                            object.position = newPivotWorld - object.pivot;
                        }
                        changed = true;
                    }
                }
                else if (operation == ImGuizmo::SCALE)
                {
                    const glm::vec3 deltaScale(ds[0], ds[1], ds[2]);
                    if (!nearlyEqualVec3(deltaScale, glm::vec3(1.0f)))
                    {
                        for (const MeshTransformState& state : gizmoStartMeshStates_)
                        {
                            LevelMeshObject& object = scene_.meshObjects()[(size_t)state.index];
                            object.scale = glm::max(state.scale * deltaScale, glm::vec3(0.01f));

                            const glm::vec3 startPivotWorld = meshPivotWorldPosition(state);
                            const glm::vec3 scaledOffset = (startPivotWorld - gizmoMultiSelectionCenter_) * deltaScale;
                            const glm::vec3 newPivotWorld = gizmoMultiSelectionCenter_ + scaledOffset;
                            object.position = newPivotWorld - object.pivot;
                        }
                        changed = true;
                    }
                }

                if (changed)
                    sceneDirty_ = true;
            }
            else
            {
                LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];
                const glm::vec3 newPivotWorld(t[0], t[1], t[2]);
                const glm::vec3 newPosition = newPivotWorld - meshObject.pivot;
                const glm::vec3 newScale(s[0], s[1], s[2]);
                glm::vec3 newRotation = meshObject.rotationEuler;

                if (operation == ImGuizmo::ROTATE)
                {
                    float dt[3] = {0.0f, 0.0f, 0.0f};
                    float dr[3] = {0.0f, 0.0f, 0.0f};
                    float ds[3] = {1.0f, 1.0f, 1.0f};
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(deltaMatrix), dt, dr, ds);
                    const glm::quat currentRotation = glm::quat(glm::radians(meshObject.rotationEuler));
                    const glm::quat deltaRotation = glm::quat(glm::radians(glm::vec3(dr[0], dr[1], dr[2])));
                    newRotation = normalizeEulerDegrees(glm::degrees(glm::eulerAngles(glm::normalize(deltaRotation * currentRotation))));
                }
                else
                {
                    newRotation = normalizeEulerDegrees(glm::vec3(r[0], r[1], r[2]));
                }

                if (newPosition != meshObject.position ||
                    newRotation != meshObject.rotationEuler ||
                    newScale != meshObject.scale)
                {
                    meshObject.position = newPosition;
                    meshObject.rotationEuler = newRotation;
                    meshObject.scale = newScale;
                    sceneDirty_ = true;
                }
            }
        }
    }

    gizmoWasUsing_ = usingNow;
    if (!usingNow)
    {
        gizmoStartMeshStates_.clear();
        gizmoStartVertexPositions_.clear();
    }
}

void LevelEditorApp::Render3DView(const LevelEditorView& view, ImDrawList* drawList)
{
    const int w = view.rect.w;
    const int h = view.rect.h;
    const float headerHeight = 26.0f;
    const int contentH = std::max(1, h - static_cast<int>(headerHeight));
    if (w <= 0 || contentH <= 0)
        return;

    // Lazy create / resize per-view FBO
    LevelEditorView& mutableView = const_cast<LevelEditorView&>(view);
    if (!mutableView.rt || mutableView.rtWidth != w || mutableView.rtHeight != contentH)
    {
        delete mutableView.rt;
        mutableView.rt = new RenderTarget();
        mutableView.rt->create(w, contentH);
        mutableView.rt->addColorAttachment();
        mutableView.rt->addDepthAttachment();
        mutableView.rt->finalize();
        mutableView.rtWidth = w;
        mutableView.rtHeight = contentH;
    }

    RenderState& rs = RenderState::instance();

    mutableView.rt->bind();
    rs.setViewport(0, 0, w, contentH);
    rs.setScissorTest(false);
    rs.setClearColor(view.clearColor.r, view.clearColor.g, view.clearColor.b, view.clearColor.a);
    rs.clear(true, true);
    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setBlend(false);
    switch (cullMode_)
    {
    case CullMode::Off:
        rs.setCull(false);
        break;
    case CullMode::Front:
        rs.setCull(true);
        rs.setCullFace(GL_FRONT);
        break;
    case CullMode::Back:
    default:
        rs.setCull(true);
        rs.setCullFace(GL_BACK);
        break;
    }

    const glm::mat4 vp = view.camera.viewProjection;

    // 1) Draw grid first (behind everything) using Batch
    if (showGrid_)
    {
        rs.setDepthTest(true);
        rs.setDepthWrite(true);
        rs.setBlend(true);
        rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        viewBatch_->SetMatrix(vp);

        if (view.type == ViewType::Perspective)
        {
            const int slices = std::max(10, static_cast<int>(view.perspectiveDistance * 1.6f / std::max(1.0f, perspGridSize_)));
            viewBatch_->SetColor(52, 62, 78, 220);
            viewBatch_->Grid(slices, perspGridSize_, true);
        }
        else
        {
            // Ortho grid: draw lines in the view's plane using world coordinates
            const float halfH = view.orthoSize;
            const float aspect = (w > 0 && contentH > 0) ? (static_cast<float>(w) / static_cast<float>(contentH)) : 1.0f;
            const float halfW = halfH * aspect;

            const float pixelsPerWorld = static_cast<float>(contentH) / (2.0f * halfH);
            const float worldStep = niceGridStep(24.0f / std::max(1e-4f, pixelsPerWorld));
            const float majorWorldStep = worldStep * 4.0f;

            // Determine axis mapping for this view type
            // axisA = horizontal world axis index, axisB = vertical world axis index
            int axisA = 0, axisB = 2;
            switch (view.type)
            {
            case ViewType::Top:    axisA = 0; axisB = 2; break;
            case ViewType::Bottom: axisA = 0; axisB = 2; break;
            case ViewType::Front:  axisA = 0; axisB = 1; break;
            case ViewType::Back:   axisA = 0; axisB = 1; break;
            case ViewType::Left:   axisA = 2; axisB = 1; break;
            case ViewType::Right:  axisA = 2; axisB = 1; break;
            default: break;
            }

            const float focusA = view.focus[axisA];
            const float focusB = view.focus[axisB];
            const float minA = focusA - halfW;
            const float maxA = focusA + halfW;
            const float minB = focusB - halfH;
            const float maxB = focusB + halfH;

            const float startA = std::floor(minA / worldStep) * worldStep;
            const float startB = std::floor(minB / worldStep) * worldStep;

            // Vertical lines (along B axis)
            for (float a = startA; a <= maxA + worldStep; a += worldStep)
            {
                const bool major = std::fmod(std::fabs(a), majorWorldStep) < 0.001f;
                viewBatch_->SetColor(major ? 52 : 40, major ? 62 : 48, major ? 78 : 60, 255);
                glm::vec3 p0(0.0f), p1(0.0f);
                p0[axisA] = a; p0[axisB] = minB;
                p1[axisA] = a; p1[axisB] = maxB;
                viewBatch_->Line3D(p0, p1);
            }
            // Horizontal lines (along A axis)
            for (float b = startB; b <= maxB + worldStep; b += worldStep)
            {
                const bool major = std::fmod(std::fabs(b), majorWorldStep) < 0.001f;
                viewBatch_->SetColor(major ? 52 : 40, major ? 62 : 48, major ? 78 : 60, 255);
                glm::vec3 p0(0.0f), p1(0.0f);
                p0[axisA] = minA; p0[axisB] = b;
                p1[axisA] = maxA; p1[axisB] = b;
                viewBatch_->Line3D(p0, p1);
            }

            // Axis lines
            glm::vec3 axisH0(0.0f), axisH1(0.0f);
            axisH0[axisA] = minA; axisH0[axisB] = 0.0f;
            axisH1[axisA] = maxA; axisH1[axisB] = 0.0f;
            viewBatch_->SetColor(80, 120, 240, 180);
            viewBatch_->Line3D(axisH0, axisH1);

            glm::vec3 axisV0(0.0f), axisV1(0.0f);
            axisV0[axisA] = 0.0f; axisV0[axisB] = minB;
            axisV1[axisA] = 0.0f; axisV1[axisB] = maxB;
            viewBatch_->SetColor(220, 90, 90, 180);
            viewBatch_->Line3D(axisV0, axisV1);
        }

        viewBatch_->Render();
        rs.setBlend(false);
    }

    // 2) Draw meshes using editor shader + cached MeshBuffers
    //    Wireframe mode: glPolygonMode(GL_LINE) on same triangle buffer.
    //    Solid mode: 1 draw call per object.
    //    Textured mode: 1 draw call per material group.
    if (solidShader_)
    {
        if (!meshCacheValid_)
            RebuildMeshCache();

        rs.setDepthTest(true);
        rs.setDepthWrite(true);
        rs.useProgram(solidShader_->getId());
        solidShader_->setMat4("u_viewProj", vp);
        solidShader_->setVec4("u_color", glm::vec4(1.0f)); // default: full opacity for textured meshes

        const bool wireMode = view.renderMode == RenderMode::Wireframe;
        const bool texturedMode = view.renderMode == RenderMode::Textured;

        // Keep Solid/Wire readable even when a baked lightmap exists.
        const bool useEditorLightmap = texturedMode && useLightmap_ && lightmapTexture_;
        solidShader_->setInt("u_useLightmap", useEditorLightmap ? 1 : 0);
        solidShader_->setInt("u_lightmap", 1); // texture unit 1
        if (useEditorLightmap)
            rs.bindTexture(1, GL_TEXTURE_2D, lightmapTexture_);

        if (wireMode && s_glPolygonMode)
            s_glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        for (std::size_t objectIndex = 0; objectIndex < scene_.meshObjects().size(); ++objectIndex)
        {
            if (objectIndex >= meshGPUCacheCount_)
                continue;

            const LevelMeshObject& object = scene_.meshObjects()[objectIndex];
            if (!object.visible) continue;
            const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
            solidShader_->setMat4("u_model", modelMatrix);

            CachedMeshGPU& cached = meshGPUCache_[objectIndex];

            if (!wireMode)
            {
                if (texturedMode)
                {
                    TextureManager& texMgr = TextureManager::instance();

                    for (const auto& range : cached.materialRanges)
                    {
                        if (range.indexCount == 0) continue;

                        Texture* faceTex = nullptr;
                        if (!range.materialName.empty() && range.materialName != "default")
                        {
                            faceTex = texMgr.get(range.materialName);
                            if (!faceTex)
                            {
                                const std::string texName = "level_face_tex::" + range.materialName;
                                faceTex = texMgr.get(texName);
                                if (!faceTex && failedTextureLoads_.find(texName) == failedTextureLoads_.end())
                                {
                                    std::string resolvedPath = range.materialName;
                                    const std::string baseDir = assetRoot_.empty() ? "assets" : assetRoot_;
                                    std::string candidate = ResolveTexturePath(baseDir, range.materialName);
                                    if (candidate.empty())
                                        candidate = ResolveTexturePath(baseDir, PathFilename(range.materialName));
                                    if (candidate.empty())
                                    {
                                        const std::string stem = PathStem(range.materialName);
                                        if (!stem.empty() && stem != range.materialName)
                                            candidate = ResolveTexturePath(baseDir, stem);
                                    }
                                    if (!candidate.empty())
                                        resolvedPath = candidate;

                                    faceTex = texMgr.load(texName, resolvedPath);
                                    if (!faceTex)
                                        failedTextureLoads_.insert(texName);
                                }
                            }
                        }

                        solidShader_->setInt("u_useTexture", 1);
                        solidShader_->setInt("u_albedo", 0);
                        rs.bindTexture(0, GL_TEXTURE_2D, (faceTex && faceTex->id != 0) ? faceTex->id : texMgr.getPattern()->id);
                        cached.buffer.drawRange(range.indexStart, range.indexCount);
                    }
                    solidShader_->setVec4("u_color", glm::vec4(1.0f));
                }
                else
                {
                    solidShader_->setInt("u_useTexture", 0);
                    const std::size_t hv = std::hash<std::string>{}(object.name) ^ (objectIndex * 2654435761u);
                    const float r = (80 + static_cast<int>((hv >> 0) & 0x7F)) / 255.0f;
                    const float g = (90 + static_cast<int>((hv >> 8) & 0x7F)) / 255.0f;
                    const float b = (100 + static_cast<int>((hv >> 16) & 0x7F)) / 255.0f;
                    solidShader_->setVec4("u_color", glm::vec4(r, g, b, 1.0f));

                    for (const auto& range : cached.materialRanges)
                    {
                        if (range.indexCount == 0) continue;
                        cached.buffer.drawRange(range.indexStart, range.indexCount);
                    }
                }
            }
            else
            {
                // Wireframe: 1 draw call per object
                solidShader_->setInt("u_useTexture", 0);
                const std::size_t hv = std::hash<std::string>{}(object.name) ^ (objectIndex * 2654435761u);
                const float r = (80 + static_cast<int>((hv >> 0) & 0x7F)) / 255.0f;
                const float g = (90 + static_cast<int>((hv >> 8) & 0x7F)) / 255.0f;
                const float b = (100 + static_cast<int>((hv >> 16) & 0x7F)) / 255.0f;
                solidShader_->setVec4("u_color", glm::vec4(r, g, b, 1.0f));
                cached.buffer.draw();
            }
        }

        if (wireMode && s_glPolygonMode)
            s_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        rs.useProgram(0);
    }

    // 2b) Reference image planes
    if (!referencePlanes_.empty())
    {
        rs.setDepthTest(true);
        rs.setDepthWrite(false);
        rs.setBlend(true);
        rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        rs.useProgram(solidShader_->getId());
        solidShader_->setMat4("u_viewProj", vp);
        solidShader_->setInt("u_useLightmap", 0); // No lightmap for reference planes

        for (const auto& refPlane : referencePlanes_)
        {
            if (!refPlane.visible || refPlane.textureName.empty()) continue;
            Texture* tex = TextureManager::instance().get(refPlane.textureName);
            if (!tex || tex->id == 0) continue;

            const float halfS = refPlane.scale * 0.5f;
            const float d = refPlane.offset;
            // Compute aspect ratio from texture
            const float aspect = (tex->height > 0) ? (static_cast<float>(tex->width) / static_cast<float>(tex->height)) : 1.0f;
            const float halfW = halfS * aspect;
            const float halfH = halfS;

            // Build quad vertices based on axis
            glm::vec3 corners[4];
            switch (refPlane.axis)
            {
            case RefPlaneAxis::Front:  // -Z facing +Z
                corners[0] = glm::vec3(-halfW, -halfH, d);
                corners[1] = glm::vec3( halfW, -halfH, d);
                corners[2] = glm::vec3( halfW,  halfH, d);
                corners[3] = glm::vec3(-halfW,  halfH, d);
                break;
            case RefPlaneAxis::Back:   // +Z facing -Z
                corners[0] = glm::vec3( halfW, -halfH, d);
                corners[1] = glm::vec3(-halfW, -halfH, d);
                corners[2] = glm::vec3(-halfW,  halfH, d);
                corners[3] = glm::vec3( halfW,  halfH, d);
                break;
            case RefPlaneAxis::Left:   // -X facing +X
                corners[0] = glm::vec3(d, -halfH,  halfW);
                corners[1] = glm::vec3(d, -halfH, -halfW);
                corners[2] = glm::vec3(d,  halfH, -halfW);
                corners[3] = glm::vec3(d,  halfH,  halfW);
                break;
            case RefPlaneAxis::Right:  // +X facing -X
                corners[0] = glm::vec3(d, -halfH, -halfW);
                corners[1] = glm::vec3(d, -halfH,  halfW);
                corners[2] = glm::vec3(d,  halfH,  halfW);
                corners[3] = glm::vec3(d,  halfH, -halfW);
                break;
            case RefPlaneAxis::Top:    // +Y facing -Y
                corners[0] = glm::vec3(-halfW, d,  halfH);
                corners[1] = glm::vec3( halfW, d,  halfH);
                corners[2] = glm::vec3( halfW, d, -halfH);
                corners[3] = glm::vec3(-halfW, d, -halfH);
                break;
            case RefPlaneAxis::Bottom: // -Y facing +Y
                corners[0] = glm::vec3(-halfW, d, -halfH);
                corners[1] = glm::vec3( halfW, d, -halfH);
                corners[2] = glm::vec3( halfW, d,  halfH);
                corners[3] = glm::vec3(-halfW, d,  halfH);
                break;
            }

            const glm::vec2 uvs[4] = {{0,1}, {1,1}, {1,0}, {0,0}};

            MeshBuffer quadBuf;
            for (int i = 0; i < 4; ++i)
            {
                Vertex v{};
                v.position = corners[i];
                v.normal = glm::vec3(0,0,1);
                v.uv = uvs[i];
                v.tangent = glm::vec4(1,0,0,1);
                quadBuf.vertices.push_back(v);
            }
            quadBuf.indices = {0, 1, 2, 0, 2, 3};
            quadBuf.upload();

            solidShader_->setMat4("u_model", glm::mat4(1.0f));
            solidShader_->setInt("u_useTexture", 1);
            solidShader_->setInt("u_albedo", 0);
            solidShader_->setVec4("u_color", glm::vec4(1.0f, 1.0f, 1.0f, refPlane.opacity));
            rs.bindTexture(0, GL_TEXTURE_2D, tex->id);
            quadBuf.draw();
            quadBuf.free();
        }

        rs.setDepthWrite(true);
        rs.useProgram(0);
    }

    // 3) Batch: only selected face highlight (edges + fill)
    const int selectedFaceFillAlpha = std::clamp(static_cast<int>(faceHighlightFillAlpha_ * 255.0f), 0, 255);
    const int hoveredFaceFillAlpha = std::clamp(static_cast<int>(faceHighlightFillAlpha_ * 0.45f * 255.0f), 0, 255);

    if (selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
    {
        rs.setDepthTest(false);
        rs.setBlend(true);
        rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const LevelMeshObject& object = scene_.meshObjects()[(size_t)selectedMeshIndex_];
        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
        viewBatch_->SetMatrix(vp);
        viewBatch_->BeginTransform(modelMatrix);

        if (selectionMode_ == SelectionMode::Face &&
            !selectedFaceIndices_.empty())
        {
            const auto& verts = object.mesh.vertices();
            for (int faceIndex : selectedFaceIndices_)
            {
                if (faceIndex < 0 || faceIndex >= static_cast<int>(object.mesh.faces().size()))
                    continue;

                const EditableFace& selFace = object.mesh.faces()[(size_t)faceIndex];

                viewBatch_->SetColor(255, 235, 90, 255);
                for (std::size_t e = 0; e < selFace.indices.size(); ++e)
                {
                    const int ci = selFace.indices[e];
                    const int ni = selFace.indices[(e + 1) % selFace.indices.size()];
                    if (ci >= 0 && ci < static_cast<int>(verts.size()) &&
                        ni >= 0 && ni < static_cast<int>(verts.size()))
                    {
                        viewBatch_->Line3D(verts[(size_t)ci].position, verts[(size_t)ni].position);
                    }
                }

                if (faceHighlightFillEnabled_ && selFace.indices.size() >= 3 && selectedFaceFillAlpha > 0)
                {
                    viewBatch_->SetColor(255, 186, 79, selectedFaceFillAlpha);
                    const glm::vec3 p0 = verts[(size_t)selFace.indices[0]].position;
                    for (size_t t = 1; t + 1 < selFace.indices.size(); ++t)
                    {
                        viewBatch_->Triangle(
                            p0,
                            verts[(size_t)selFace.indices[t]].position,
                            verts[(size_t)selFace.indices[t + 1]].position);
                    }
                }
            }
        }

        viewBatch_->EndTransform();
        viewBatch_->Render();
    }

    if (selectionMode_ == SelectionMode::Face &&
        hoveredFaceMeshIndex_ >= 0 &&
        hoveredFaceMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        hoveredFaceIndex_ >= 0)
    {
        const LevelMeshObject& hoveredObject = scene_.meshObjects()[(size_t)hoveredFaceMeshIndex_];
        if (hoveredFaceIndex_ < static_cast<int>(hoveredObject.mesh.faces().size()) &&
            !(hoveredFaceMeshIndex_ == selectedMeshIndex_ && IsFaceSelected(hoveredFaceIndex_)))
        {
            const auto& verts = hoveredObject.mesh.vertices();
            const EditableFace& hoverFace = hoveredObject.mesh.faces()[(size_t)hoveredFaceIndex_];
            const glm::mat4 hoverModelMatrix = meshObjectModelMatrix(hoveredObject);

            viewBatch_->SetMatrix(vp);
            viewBatch_->BeginTransform(hoverModelMatrix);
            viewBatch_->SetColor(120, 240, 255, 255);
            for (std::size_t e = 0; e < hoverFace.indices.size(); ++e)
            {
                const int ci = hoverFace.indices[e];
                const int ni = hoverFace.indices[(e + 1) % hoverFace.indices.size()];
                if (ci >= 0 && ci < static_cast<int>(verts.size()) &&
                    ni >= 0 && ni < static_cast<int>(verts.size()))
                {
                    viewBatch_->Line3D(verts[(size_t)ci].position, verts[(size_t)ni].position);
                }
            }

            if (faceHighlightFillEnabled_ && hoverFace.indices.size() >= 3 && hoveredFaceFillAlpha > 0)
            {
                viewBatch_->SetColor(120, 240, 255, hoveredFaceFillAlpha);
                const glm::vec3 p0 = verts[(size_t)hoverFace.indices[0]].position;
                for (size_t t = 1; t + 1 < hoverFace.indices.size(); ++t)
                {
                    viewBatch_->Triangle(
                        p0,
                        verts[(size_t)hoverFace.indices[t]].position,
                        verts[(size_t)hoverFace.indices[t + 1]].position);
                }
            }
            viewBatch_->EndTransform();
            viewBatch_->Render();
        }
    }

    // 3.5) Debug: draw vertex normals and tangents
    if (debugDrawNormals_ || debugDrawTangents_)
    {
        rs.setDepthTest(true);
        viewBatch_->SetMatrix(vp);
        const float len = debugNormalLength_;

        for (int mi = 0; mi < static_cast<int>(scene_.meshObjects().size()); ++mi)
        {
            const auto& obj = scene_.meshObjects()[mi];
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position)
                * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotationEuler.y), glm::vec3(0,1,0))
                * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotationEuler.x), glm::vec3(1,0,0))
                * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotationEuler.z), glm::vec3(0,0,1))
                * glm::scale(glm::mat4(1.0f), obj.scale);
            const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

            for (const auto& vert : obj.mesh.vertices())
            {
                const glm::vec3 wp = glm::vec3(model * glm::vec4(vert.position, 1.0f));

                if (debugDrawNormals_)
                {
                    glm::vec3 wn = normalMat * vert.normal;
                    if (glm::length(wn) > 1e-6f) wn = glm::normalize(wn);
                    viewBatch_->SetColor(0, 120, 255, 220); // blue = normal
                    viewBatch_->Line3D(wp, wp + wn * len);
                }
            }

            // Draw face tangents (computed from first edge of each face)
            if (debugDrawTangents_)
            {
                for (const auto& face : obj.mesh.faces())
                {
                    if (face.indices.size() < 3) continue;
                    // Compute face center
                    glm::vec3 center(0.0f);
                    for (int idx : face.indices)
                        center += glm::vec3(model * glm::vec4(obj.mesh.vertices()[(size_t)idx].position, 1.0f));
                    center /= static_cast<float>(face.indices.size());

                    // Tangent = first edge direction
                    const glm::vec3 p0 = glm::vec3(model * glm::vec4(obj.mesh.vertices()[(size_t)face.indices[0]].position, 1.0f));
                    const glm::vec3 p1 = glm::vec3(model * glm::vec4(obj.mesh.vertices()[(size_t)face.indices[1]].position, 1.0f));
                    glm::vec3 tangent = p1 - p0;
                    if (glm::length(tangent) > 1e-6f)
                    {
                        tangent = glm::normalize(tangent);
                        viewBatch_->SetColor(255, 60, 60, 220); // red = tangent
                        viewBatch_->Line3D(center, center + tangent * len);

                        // Bitangent (cross normal x tangent)
                        glm::vec3 faceN(0.0f);
                        for (int idx : face.indices)
                            faceN += normalMat * obj.mesh.vertices()[(size_t)idx].normal;
                        if (glm::length(faceN) > 1e-6f) faceN = glm::normalize(faceN);
                        glm::vec3 bitangent = glm::cross(faceN, tangent);
                        if (glm::length(bitangent) > 1e-6f)
                        {
                            bitangent = glm::normalize(bitangent);
                            viewBatch_->SetColor(60, 255, 60, 220); // green = bitangent
                            viewBatch_->Line3D(center, center + bitangent * len);
                        }
                    }
                }
            }
        }
        viewBatch_->Render();
    }

    // 4) Draw entity icons (lights, player starts, etc.)
    {
        rs.setDepthTest(true);
        rs.setBlend(true);
        rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        viewBatch_->SetMatrix(vp);

        for (int ei = 0; ei < static_cast<int>(scene_.entities().size()); ++ei)
        {
            const LevelEntityObject& ent = scene_.entities()[ei];
            const bool selected = (ei == selectedEntityIndex_);
            const glm::vec3& p = ent.position;

            if (ent.type == LevelEntityType::Light)
            {
                const float s = 8.0f;
                const uint8_t alpha = selected ? 255 : 150;
                const uint8_t cr = static_cast<uint8_t>(std::min(1.0f, ent.color.r) * 255.0f);
                const uint8_t cg = static_cast<uint8_t>(std::min(1.0f, ent.color.g) * 255.0f);
                const uint8_t cb = static_cast<uint8_t>(std::min(1.0f, ent.color.b) * 255.0f);
                viewBatch_->SetColor(cr, cg, cb, alpha);

                // Diamond (octahedron) — all light types
                const glm::vec3 top   = p + glm::vec3(0, s, 0);
                const glm::vec3 bot   = p + glm::vec3(0, -s, 0);
                const glm::vec3 right = p + glm::vec3(s, 0, 0);
                const glm::vec3 left  = p + glm::vec3(-s, 0, 0);
                const glm::vec3 front = p + glm::vec3(0, 0, s);
                const glm::vec3 back  = p + glm::vec3(0, 0, -s);
                viewBatch_->Line3D(top, right); viewBatch_->Line3D(top, left);
                viewBatch_->Line3D(top, front); viewBatch_->Line3D(top, back);
                viewBatch_->Line3D(bot, right); viewBatch_->Line3D(bot, left);
                viewBatch_->Line3D(bot, front); viewBatch_->Line3D(bot, back);
                viewBatch_->Line3D(right, front); viewBatch_->Line3D(front, left);
                viewBatch_->Line3D(left, back); viewBatch_->Line3D(back, right);

                // Helper: draw circle in arbitrary plane
                auto drawCircle = [&](const glm::vec3& center, const glm::vec3& ax1, const glm::vec3& ax2, float radius, int segs, uint8_t ca) {
                    viewBatch_->SetColor(cr, cg, cb, ca);
                    for (int si = 0; si < segs; ++si) {
                        const float a0 = static_cast<float>(si) / static_cast<float>(segs) * 6.2831853f;
                        const float a1 = static_cast<float>(si + 1) / static_cast<float>(segs) * 6.2831853f;
                        viewBatch_->Line3D(
                            center + ax1 * (std::cos(a0) * radius) + ax2 * (std::sin(a0) * radius),
                            center + ax1 * (std::cos(a1) * radius) + ax2 * (std::sin(a1) * radius));
                    }
                };

                // Helper: draw arrow from a to b
                auto drawArrow = [&](const glm::vec3& from, const glm::vec3& to, float headSize) {
                    viewBatch_->Line3D(from, to);
                    const glm::vec3 d = glm::normalize(to - from);
                    glm::vec3 px = glm::cross(d, glm::vec3(0,1,0));
                    if (glm::length(px) < 0.01f) px = glm::cross(d, glm::vec3(1,0,0));
                    px = glm::normalize(px);
                    const glm::vec3 py = glm::normalize(glm::cross(d, px));
                    viewBatch_->Line3D(to, to - d * headSize + px * (headSize * 0.4f));
                    viewBatch_->Line3D(to, to - d * headSize - px * (headSize * 0.4f));
                    viewBatch_->Line3D(to, to - d * headSize + py * (headSize * 0.4f));
                    viewBatch_->Line3D(to, to - d * headSize - py * (headSize * 0.4f));
                };

                if (ent.lightType == LightType::Point)
                {
                    // Always show 3 radius circles (XZ, XY, YZ)
                    const uint8_t circleAlpha = selected ? 100 : 40;
                    drawCircle(p, glm::vec3(1,0,0), glm::vec3(0,0,1), ent.radius, 32, circleAlpha); // XZ
                    drawCircle(p, glm::vec3(1,0,0), glm::vec3(0,1,0), ent.radius, 32, circleAlpha); // XY
                    drawCircle(p, glm::vec3(0,1,0), glm::vec3(0,0,1), ent.radius, 32, circleAlpha); // YZ

                    // Small rays from diamond
                    viewBatch_->SetColor(cr, cg, cb, alpha);
                    const float r = s * 1.8f;
                    const float r2 = s * 2.5f;
                    viewBatch_->Line3D(p + glm::vec3(r, 0, 0), p + glm::vec3(r2, 0, 0));
                    viewBatch_->Line3D(p + glm::vec3(-r, 0, 0), p + glm::vec3(-r2, 0, 0));
                    viewBatch_->Line3D(p + glm::vec3(0, r, 0), p + glm::vec3(0, r2, 0));
                    viewBatch_->Line3D(p + glm::vec3(0, -r, 0), p + glm::vec3(0, -r2, 0));
                    viewBatch_->Line3D(p + glm::vec3(0, 0, r), p + glm::vec3(0, 0, r2));
                    viewBatch_->Line3D(p + glm::vec3(0, 0, -r), p + glm::vec3(0, 0, -r2));
                }
                else if (ent.lightType == LightType::Directional)
                {
                    const glm::vec3 dir = glm::length(ent.direction) > 1e-4f ? glm::normalize(ent.direction) : glm::vec3(0,-1,0);

                    // Build perpendicular axes
                    glm::vec3 perp1 = glm::cross(dir, glm::vec3(0,1,0));
                    if (glm::length(perp1) < 0.01f) perp1 = glm::cross(dir, glm::vec3(1,0,0));
                    perp1 = glm::normalize(perp1);
                    const glm::vec3 perp2 = glm::normalize(glm::cross(dir, perp1));

                    // Main direction arrow (long)
                    viewBatch_->SetColor(cr, cg, cb, alpha);
                    const float arrowLen = s * 6.0f;
                    drawArrow(p, p + dir * arrowLen, s);

                    // Parallel arrows to show it's directional (all rays parallel)
                    const uint8_t paraAlpha = selected ? 180 : 100;
                    viewBatch_->SetColor(cr, cg, cb, paraAlpha);
                    const float spread = s * 1.5f;
                    const float paraLen = s * 4.0f;
                    for (int ai = -1; ai <= 1; ai += 2)
                    {
                        for (int bi = -1; bi <= 1; bi += 2)
                        {
                            const glm::vec3 off = perp1 * (static_cast<float>(ai) * spread) + perp2 * (static_cast<float>(bi) * spread);
                            viewBatch_->Line3D(p + off, p + off + dir * paraLen);
                            // Small arrowhead
                            viewBatch_->Line3D(p + off + dir * paraLen, p + off + dir * (paraLen - s*0.4f) + perp1 * (s*0.2f));
                            viewBatch_->Line3D(p + off + dir * paraLen, p + off + dir * (paraLen - s*0.4f) - perp1 * (s*0.2f));
                        }
                    }

                    // Circle around origin to show "sun disc"
                    if (selected)
                        drawCircle(p, perp1, perp2, s * 2.0f, 16, 80);

                    // Long direction ray for aiming (visible while rotating)
                    viewBatch_->SetColor(cr, cg, cb, selected ? 200 : 80);
                    const float rayLen = 500.0f;
                    const int dashes = 20;
                    for (int di = 0; di < dashes; ++di)
                    {
                        const float t0 = static_cast<float>(di) / static_cast<float>(dashes);
                        const float t1 = (static_cast<float>(di) + 0.6f) / static_cast<float>(dashes);
                        viewBatch_->Line3D(p + dir * (t0 * rayLen), p + dir * (t1 * rayLen));
                    }
                }
                else if (ent.lightType == LightType::Spot)
                {
                    const glm::vec3 dir = glm::length(ent.direction) > 1e-4f ? glm::normalize(ent.direction) : glm::vec3(0,-1,0);
                    const float coneLen = std::min(ent.radius, 300.0f);
                    const float outerRadius = coneLen * std::tan(glm::radians(ent.spotAngle));
                    const float innerRadius = outerRadius * (1.0f - std::clamp(ent.spotSoftness, 0.0f, 1.0f));

                    // Build perpendicular axes
                    glm::vec3 perp1 = glm::cross(dir, glm::vec3(0,1,0));
                    if (glm::length(perp1) < 0.01f) perp1 = glm::cross(dir, glm::vec3(1,0,0));
                    perp1 = glm::normalize(perp1);
                    const glm::vec3 perp2 = glm::normalize(glm::cross(dir, perp1));

                    const glm::vec3 coneCenter = p + dir * coneLen;

                    // Direction arrow
                    viewBatch_->SetColor(cr, cg, cb, alpha);
                    drawArrow(p, p + dir * (s * 3.0f), s * 0.5f);

                    // 4 cone side lines (outer)
                    for (int ci = 0; ci < 4; ++ci)
                    {
                        const float a0 = static_cast<float>(ci) / 4.0f * 6.2831853f;
                        const glm::vec3 rim = coneCenter + (perp1 * std::cos(a0) + perp2 * std::sin(a0)) * outerRadius;
                        viewBatch_->Line3D(p, rim);
                    }

                    // Outer cone base circle (always visible)
                    drawCircle(coneCenter, perp1, perp2, outerRadius, 24, selected ? 120 : 60);

                    // Inner cone (softness boundary) — dashed look via fewer segments
                    if (selected && ent.spotSoftness > 0.01f)
                    {
                        drawCircle(coneCenter, perp1, perp2, innerRadius, 16, 60);
                        // Inner cone side lines (dimmer)
                        viewBatch_->SetColor(cr, cg, cb, 60);
                        for (int ci = 0; ci < 4; ++ci)
                        {
                            const float a0 = (static_cast<float>(ci) + 0.5f) / 4.0f * 6.2831853f;
                            const glm::vec3 rim = coneCenter + (perp1 * std::cos(a0) + perp2 * std::sin(a0)) * innerRadius;
                            viewBatch_->Line3D(p, rim);
                        }
                    }

                    // Radius circle at origin (XZ plane)
                    if (selected)
                        drawCircle(p, glm::vec3(1,0,0), glm::vec3(0,0,1), ent.radius, 32, 40);

                    // Long direction ray for aiming (visible while rotating)
                    viewBatch_->SetColor(cr, cg, cb, selected ? 200 : 80);
                    const float rayLen2 = std::min(ent.radius, 500.0f);
                    const int dashes2 = 15;
                    for (int di = 0; di < dashes2; ++di)
                    {
                        const float t0 = static_cast<float>(di) / static_cast<float>(dashes2);
                        const float t1 = (static_cast<float>(di) + 0.6f) / static_cast<float>(dashes2);
                        viewBatch_->Line3D(p + dir * (t0 * rayLen2), p + dir * (t1 * rayLen2));
                    }
                }

                // Selection highlight: brighter outline
                if (selected)
                {
                    viewBatch_->SetColor(255, 255, 255, 255);
                    const float ss = s * 1.3f;
                    const glm::vec3 st = p + glm::vec3(0, ss, 0);
                    const glm::vec3 sb = p + glm::vec3(0, -ss, 0);
                    const glm::vec3 sr = p + glm::vec3(ss, 0, 0);
                    const glm::vec3 sl = p + glm::vec3(-ss, 0, 0);
                    const glm::vec3 sf = p + glm::vec3(0, 0, ss);
                    const glm::vec3 sbk = p + glm::vec3(0, 0, -ss);
                    viewBatch_->Line3D(st, sr); viewBatch_->Line3D(st, sl);
                    viewBatch_->Line3D(st, sf); viewBatch_->Line3D(st, sbk);
                    viewBatch_->Line3D(sb, sr); viewBatch_->Line3D(sb, sl);
                    viewBatch_->Line3D(sb, sf); viewBatch_->Line3D(sb, sbk);
                }
            }
            else if (ent.type == LevelEntityType::PlayerStart)
            {
                // Stick figure + direction arrow
                const uint8_t alpha = selected ? 255 : 180;
                viewBatch_->SetColor(80, 255, 80, alpha);

                // Body proportions (total height ~32 units)
                const float headR = 3.0f;
                const glm::vec3 feet      = p;
                const glm::vec3 hip       = p + glm::vec3(0, 14, 0);
                const glm::vec3 shoulders = p + glm::vec3(0, 24, 0);
                const glm::vec3 neck      = p + glm::vec3(0, 26, 0);
                const glm::vec3 headC     = p + glm::vec3(0, 26 + headR, 0);

                // Legs
                viewBatch_->Line3D(feet + glm::vec3(-4, 0, 0), hip);
                viewBatch_->Line3D(feet + glm::vec3(4, 0, 0), hip);

                // Spine
                viewBatch_->Line3D(hip, shoulders);

                // Arms
                viewBatch_->Line3D(shoulders, shoulders + glm::vec3(-6, -4, 0));
                viewBatch_->Line3D(shoulders, shoulders + glm::vec3(6, -4, 0));

                // Neck
                viewBatch_->Line3D(shoulders, neck);

                // Head circle (8 segments)
                const int headSegs = 8;
                for (int ci = 0; ci < headSegs; ++ci)
                {
                    const float a0 = static_cast<float>(ci) / headSegs * 6.2831853f;
                    const float a1 = static_cast<float>(ci + 1) / headSegs * 6.2831853f;
                    viewBatch_->Line3D(
                        headC + glm::vec3(std::cos(a0) * headR, std::sin(a0) * headR, 0),
                        headC + glm::vec3(std::cos(a1) * headR, std::sin(a1) * headR, 0));
                    viewBatch_->Line3D(
                        headC + glm::vec3(0, std::sin(a0) * headR, std::cos(a0) * headR),
                        headC + glm::vec3(0, std::sin(a1) * headR, std::cos(a1) * headR));
                }

                // Direction arrow (forward = ent.direction or -Z default)
                glm::vec3 dir = glm::normalize(ent.direction);
                if (glm::length(ent.direction) < 0.01f) dir = glm::vec3(0, 0, -1);
                const glm::vec3 arrowStart = hip;
                const glm::vec3 arrowEnd = hip + dir * 16.0f;
                viewBatch_->SetColor(255, 255, 80, alpha);
                viewBatch_->Line3D(arrowStart, arrowEnd);
                // Arrowhead
                glm::vec3 side = glm::cross(dir, glm::vec3(0, 1, 0));
                if (glm::length(side) < 0.01f) side = glm::cross(dir, glm::vec3(1, 0, 0));
                side = glm::normalize(side) * 2.5f;
                const glm::vec3 back = arrowEnd - dir * 4.0f;
                viewBatch_->Line3D(arrowEnd, back + side);
                viewBatch_->Line3D(arrowEnd, back - side);
                viewBatch_->Line3D(arrowEnd, back + glm::vec3(0, 2.5f, 0));

                // Ground cross
                if (selected)
                {
                    viewBatch_->SetColor(80, 255, 80, 100);
                    viewBatch_->Line3D(feet + glm::vec3(-6, 0, 0), feet + glm::vec3(6, 0, 0));
                    viewBatch_->Line3D(feet + glm::vec3(0, 0, -6), feet + glm::vec3(0, 0, 6));
                }
            }
            else if (ent.type == LevelEntityType::Door)
            {
                const uint8_t alpha = selected ? 255 : 180;
                const float s = 8.0f;

                // Door rectangle outline
                viewBatch_->SetColor(180, 120, 60, alpha);
                const glm::vec3 up(0, s * 3, 0);
                glm::vec3 right = glm::cross(glm::vec3(0, 1, 0), ent.direction);
                if (glm::length(right) < 0.01f) right = glm::vec3(1, 0, 0);
                right = glm::normalize(right) * s;

                const glm::vec3 bl = p - right;
                const glm::vec3 br = p + right;
                const glm::vec3 tl = bl + up;
                const glm::vec3 tr = br + up;
                viewBatch_->Line3D(bl, br);
                viewBatch_->Line3D(br, tr);
                viewBatch_->Line3D(tr, tl);
                viewBatch_->Line3D(tl, bl);

                // Hinge indicator for Turn
                if (ent.doorType == DoorType::Turn)
                {
                    viewBatch_->SetColor(255, 200, 80, alpha);
                    viewBatch_->Line3D(bl, bl + glm::vec3(0, s * 0.5f, 0));
                    viewBatch_->Line3D(bl, bl - glm::vec3(0, s * 0.5f, 0));
                }

                // Slide direction arrow
                if (ent.doorType == DoorType::Slide || ent.doorType == DoorType::Shutter)
                {
                    viewBatch_->SetColor(255, 200, 80, alpha);
                    glm::vec3 dir = glm::length(ent.direction) > 0.01f ? glm::normalize(ent.direction) : glm::vec3(1, 0, 0);
                    const glm::vec3 mid = p + up * 0.5f;
                    viewBatch_->Line3D(mid, mid + dir * s * 2.0f);
                    // Arrowhead
                    glm::vec3 as = glm::cross(dir, glm::vec3(0, 1, 0));
                    if (glm::length(as) < 0.01f) as = glm::cross(dir, glm::vec3(1, 0, 0));
                    as = glm::normalize(as) * 2.0f;
                    const glm::vec3 ab = mid + dir * s * 2.0f - dir * 3.0f;
                    viewBatch_->Line3D(mid + dir * s * 2.0f, ab + as);
                    viewBatch_->Line3D(mid + dir * s * 2.0f, ab - as);

                    // Shutter: second arrow in opposite direction
                    if (ent.doorType == DoorType::Shutter)
                    {
                        viewBatch_->Line3D(mid, mid - dir * s * 2.0f);
                        const glm::vec3 ab2 = mid - dir * s * 2.0f + dir * 3.0f;
                        viewBatch_->Line3D(mid - dir * s * 2.0f, ab2 + as);
                        viewBatch_->Line3D(mid - dir * s * 2.0f, ab2 - as);
                    }
                }

                // Selection highlight
                if (selected)
                {
                    viewBatch_->SetColor(255, 255, 255, 100);
                    viewBatch_->Line3D(bl, br);
                    viewBatch_->Line3D(br, tr);
                    viewBatch_->Line3D(tr, tl);
                    viewBatch_->Line3D(tl, bl);
                }
            }
            else if (ent.type == LevelEntityType::Elevator || ent.type == LevelEntityType::Platform)
            {
                const uint8_t alpha = selected ? 255 : 180;
                const float s = 6.0f;

                // Platform icon: flat square
                viewBatch_->SetColor(100, 180, 255, alpha);
                viewBatch_->Line3D(p + glm::vec3(-s, 0, -s), p + glm::vec3(s, 0, -s));
                viewBatch_->Line3D(p + glm::vec3(s, 0, -s), p + glm::vec3(s, 0, s));
                viewBatch_->Line3D(p + glm::vec3(s, 0, s), p + glm::vec3(-s, 0, s));
                viewBatch_->Line3D(p + glm::vec3(-s, 0, s), p + glm::vec3(-s, 0, -s));
                // Cross
                viewBatch_->Line3D(p + glm::vec3(-s, 0, -s), p + glm::vec3(s, 0, s));
                viewBatch_->Line3D(p + glm::vec3(s, 0, -s), p + glm::vec3(-s, 0, s));

                // Line to end position
                viewBatch_->SetColor(100, 255, 100, selected ? 200 : 100);
                viewBatch_->Line3D(p, ent.endPosition);

                // Small cross at end position
                viewBatch_->SetColor(100, 255, 100, selected ? 180 : 80);
                const float es = 3.0f;
                viewBatch_->Line3D(ent.endPosition + glm::vec3(-es, 0, 0), ent.endPosition + glm::vec3(es, 0, 0));
                viewBatch_->Line3D(ent.endPosition + glm::vec3(0, -es, 0), ent.endPosition + glm::vec3(0, es, 0));
                viewBatch_->Line3D(ent.endPosition + glm::vec3(0, 0, -es), ent.endPosition + glm::vec3(0, 0, es));
            }
            else if (ent.type == LevelEntityType::Placement)
            {
                const uint8_t alpha = selected ? 255 : 180;
                const float s = 5.0f;

                // Diamond shape (top view)
                viewBatch_->SetColor(255, 180, 255, alpha);
                viewBatch_->Line3D(p + glm::vec3(0, 0, -s), p + glm::vec3(s, 0, 0));
                viewBatch_->Line3D(p + glm::vec3(s, 0, 0), p + glm::vec3(0, 0, s));
                viewBatch_->Line3D(p + glm::vec3(0, 0, s), p + glm::vec3(-s, 0, 0));
                viewBatch_->Line3D(p + glm::vec3(-s, 0, 0), p + glm::vec3(0, 0, -s));
                // Vertical line
                viewBatch_->Line3D(p, p + glm::vec3(0, s * 2, 0));

                // Rotation arrow (rotationY)
                const float rad = glm::radians(ent.rotationY);
                const glm::vec3 fwd(std::sin(rad), 0, -std::cos(rad));
                viewBatch_->SetColor(255, 255, 80, alpha);
                viewBatch_->Line3D(p, p + fwd * s * 2.0f);
            }
            else
            {
                // Generic entity: small cross
                const float s = 6.0f;
                viewBatch_->SetColor(selected ? 255 : 180, selected ? 255 : 180, 80, 220);
                viewBatch_->Line3D(p + glm::vec3(-s, 0, 0), p + glm::vec3(s, 0, 0));
                viewBatch_->Line3D(p + glm::vec3(0, -s, 0), p + glm::vec3(0, s, 0));
                viewBatch_->Line3D(p + glm::vec3(0, 0, -s), p + glm::vec3(0, 0, s));
            }
        }

        viewBatch_->Render();
        rs.setBlend(false);
    }

    rs.setDepthTest(false);
    mutableView.rt->unbind();

    // Restore GL state for ImGui
    rs.setBlend(true);
    rs.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    rs.setScissorTest(true);
    rs.setDepthTest(false);
    rs.setDepthWrite(true);

    // Display the FBO as ImGui::Image
    const ImVec2 imgMin(static_cast<float>(view.rect.x), static_cast<float>(view.rect.y) + headerHeight);
    const ImVec2 imgMax(static_cast<float>(view.rect.x + w), static_cast<float>(view.rect.y + h));
    drawList->AddImage(
        static_cast<ImTextureID>(static_cast<uintptr_t>(mutableView.rt->colorTex()->id)),
        imgMin, imgMax,
        ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f)); // UV flipped (FBO is bottom-up)
}

void LevelEditorApp::DrawViewTile(const LevelEditorView& view, ImDrawList* drawList)
{
    const ImVec2 minPos(static_cast<float>(view.rect.x), static_cast<float>(view.rect.y));
    const ImVec2 maxPos(static_cast<float>(view.rect.x + view.rect.w), static_cast<float>(view.rect.y + view.rect.h));
    const bool active = &view == &views_[activeViewIndex_];
    const float headerHeight = 26.0f;

    // Hard clip for this tile (equivalent viewport scissor at UI level).
    drawList->PushClipRect(minPos, maxPos, true);

    drawList->AddRectFilled(minPos, maxPos, tileFillForView(view.type), 6.0f);
    drawList->AddRect(minPos, maxPos, active ? IM_COL32(96, 160, 255, 255) : IM_COL32(72, 96, 128, 255), 6.0f, 0, active ? 2.0f : 1.2f);

    drawList->AddRectFilled(minPos, ImVec2(maxPos.x, minPos.y + headerHeight), IM_COL32(14, 18, 24, 220), 6.0f, ImDrawFlags_RoundCornersTop);
    const char* renderModeLabel = view.renderMode == RenderMode::Wireframe ? " [Wire]" :
                                  view.renderMode == RenderMode::Textured ? " [Tex]" : "";
    char headerBuf[64];
    std::snprintf(headerBuf, sizeof(headerBuf), "%s%s", view.label, renderModeLabel);
    drawList->AddText(ImVec2(minPos.x + 10.0f, minPos.y + 6.0f), IM_COL32(235, 240, 245, 255), headerBuf);

    // Content clip excludes header to prevent bleed into title bar.
    const ImVec2 contentMin(minPos.x, minPos.y + headerHeight);
    const ImVec2 contentMax(maxPos.x, maxPos.y);
    drawList->PushClipRect(contentMin, contentMax, true);

    // ALL views: render mesh + grid + wireframe via GPU into per-view FBO.
    Render3DView(view, drawList);

    // --- remaining ImGui overlays (selection rects, vertices, entities) below ---
    auto projectWorld = [&](const glm::vec3& world, ImVec2& outPoint, float& outDepth) -> bool
    {
        return ProjectWorldToView(view, world, outPoint, outDepth);
    };

    int terrainCols = 0;
    int terrainRows = 0;
    if (terrainSculptEnabled_ &&
        SelectedMeshIsTerrain(&terrainCols, &terrainRows) &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
    {
        const LevelMeshObject& terrainObject = scene_.meshObjects()[static_cast<std::size_t>(selectedMeshIndex_)];
        glm::vec3 brushCenterLocal(0.0f);
        bool hasBrushCenter = false;
        if (terrainBrushPreviewValid_)
        {
            brushCenterLocal = terrainBrushPreviewLocalCenter_;
            hasBrushCenter = true;
        }
        else if (terrainSculpting_ && terrainSculptHasLastSample_)
        {
            brushCenterLocal = terrainSculptLastLocalCenter_;
            hasBrushCenter = true;
        }

        if (hasBrushCenter)
        {
            const glm::mat4 modelMatrix = meshObjectModelMatrix(terrainObject);
            constexpr int kBrushSegments = 48;
            std::array<ImVec2, kBrushSegments> brushPoints {};
            int projectedCount = 0;
            for (int i = 0; i < kBrushSegments; ++i)
            {
                const float angle = (static_cast<float>(i) / static_cast<float>(kBrushSegments)) * glm::two_pi<float>();
                const glm::vec3 localPoint(
                    brushCenterLocal.x + std::cos(angle) * terrainBrushRadius_,
                    sampleTerrainHeightLocal(
                        terrainObject.mesh,
                        terrainCols,
                        terrainRows,
                        brushCenterLocal.x + std::cos(angle) * terrainBrushRadius_,
                        brushCenterLocal.z + std::sin(angle) * terrainBrushRadius_) + 0.25f,
                    brushCenterLocal.z + std::sin(angle) * terrainBrushRadius_);
                ImVec2 screenPoint;
                float depth = 0.0f;
                if (!projectWorld(glm::vec3(modelMatrix * glm::vec4(localPoint, 1.0f)), screenPoint, depth))
                    continue;
                brushPoints[projectedCount++] = screenPoint;
            }

            ImVec2 centerPoint;
            float centerDepth = 0.0f;
            const glm::vec3 centerLocalPoint(
                brushCenterLocal.x,
                sampleTerrainHeightLocal(terrainObject.mesh, terrainCols, terrainRows, brushCenterLocal.x, brushCenterLocal.z) + 0.25f,
                brushCenterLocal.z);
            const bool hasCenterPoint = projectWorld(
                glm::vec3(modelMatrix * glm::vec4(centerLocalPoint, 1.0f)),
                centerPoint,
                centerDepth);

            if (projectedCount >= 3)
            {
                const ImU32 fillColor = IM_COL32(255, 196, 96, 30);
                const ImU32 strokeColor = IM_COL32(255, 220, 140, 240);
                drawList->AddConvexPolyFilled(brushPoints.data(), projectedCount, fillColor);
                drawList->AddPolyline(brushPoints.data(), projectedCount, strokeColor, ImDrawFlags_Closed, 2.0f);
            }

            if (hasCenterPoint)
            {
                drawList->AddCircleFilled(centerPoint, 4.0f, IM_COL32(255, 214, 122, 255), 12);
                drawList->AddCircle(centerPoint, 7.0f, IM_COL32(255, 245, 210, 255), 12, 1.4f);
            }
        }
    }

    for (std::size_t objectIndex = 0; objectIndex < scene_.meshObjects().size(); ++objectIndex)
    {
        if (!IsMeshSelected(static_cast<int>(objectIndex)))
            continue;

        const LevelMeshObject& object = scene_.meshObjects()[objectIndex];
        ImVec2 pivotPoint;
        float pivotDepth = 0.0f;
        const glm::vec3 pivotWorld = object.position + object.pivot;
        if (!projectWorld(pivotWorld, pivotPoint, pivotDepth))
            continue;

        const bool primarySelected = static_cast<int>(objectIndex) == selectedMeshIndex_;
        const float outerRadius = primarySelected ? 8.0f : 6.5f;
        const float innerRadius = std::max(2.0f, outerRadius - 3.0f);
        const float armLength = outerRadius + 4.0f;
        const ImU32 ringColor = primarySelected ? IM_COL32(255, 210, 80, 255) : IM_COL32(255, 235, 170, 240);
        const ImU32 crossColor = IM_COL32(255, 250, 230, 255);

        drawList->AddCircleFilled(pivotPoint, innerRadius, IM_COL32(24, 28, 34, 230), 16);
        drawList->AddCircle(pivotPoint, outerRadius, ringColor, 16, 2.0f);
        drawList->AddLine(ImVec2(pivotPoint.x - armLength, pivotPoint.y), ImVec2(pivotPoint.x + armLength, pivotPoint.y), crossColor, 1.4f);
        drawList->AddLine(ImVec2(pivotPoint.x, pivotPoint.y - armLength), ImVec2(pivotPoint.x, pivotPoint.y + armLength), crossColor, 1.4f);
    }

    if (boxSelecting_ &&
        boxSelectViewIndex_ >= 0 &&
        boxSelectViewIndex_ < activeViewCount_ &&
        &view == &views_[boxSelectViewIndex_])
    {
        const float minX = std::clamp(std::min(boxSelectStart_.x, boxSelectCurrent_.x), contentMin.x, contentMax.x);
        const float maxX = std::clamp(std::max(boxSelectStart_.x, boxSelectCurrent_.x), contentMin.x, contentMax.x);
        const float minY = std::clamp(std::min(boxSelectStart_.y, boxSelectCurrent_.y), contentMin.y, contentMax.y);
        const float maxY = std::clamp(std::max(boxSelectStart_.y, boxSelectCurrent_.y), contentMin.y, contentMax.y);
        drawList->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(88, 150, 255, 42));
        drawList->AddRect(ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(120, 185, 255, 235), 0.0f, 0, 1.3f);
    }

    if (selectionMode_ == SelectionMode::Vertex &&
        selectedMeshIndex_ >= 0 &&
        selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
    {
        const LevelMeshObject& object = scene_.meshObjects()[(size_t)selectedMeshIndex_];
        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
        const bool drawFullVertexOverlay =
            view.type != ViewType::Perspective &&
            &view == &views_[activeViewIndex_];
        struct DrawVertex
        {
            int index = -1;
            ImVec2 point = ImVec2(0.0f, 0.0f);
            float depth = 0.0f;
        };
        std::vector<DrawVertex> drawVertices;
        if (!drawFullVertexOverlay)
            drawVertices.reserve(selectedVertexIndices_.size());
        else
            drawVertices.reserve(object.mesh.vertices().size());

        auto appendProjectedVertex = [&](int vertexIndex)
        {
            if (vertexIndex < 0 || vertexIndex >= static_cast<int>(object.mesh.vertices().size()))
                return;
            ImVec2 point;
            float depth = 0.0f;
            const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)vertexIndex].position, 1.0f));
            if (!projectWorld(world, point, depth))
                return;
            drawVertices.push_back({vertexIndex, point, depth});
        };

        if (!drawFullVertexOverlay)
        {
            for (int vertexIndex : selectedVertexIndices_)
                appendProjectedVertex(vertexIndex);
        }
        else
        {
            for (int vertexIndex = 0; vertexIndex < static_cast<int>(object.mesh.vertices().size()); ++vertexIndex)
                appendProjectedVertex(vertexIndex);
        }

        if (vertexFrontOnly_)
        {
            constexpr float overlapTolerance = 6.0f;
            std::vector<bool> hidden(drawVertices.size(), false);
            for (std::size_t i = 0; i < drawVertices.size(); ++i)
            {
                if (hidden[i])
                    continue;
                for (std::size_t j = i + 1; j < drawVertices.size(); ++j)
                {
                    if (std::fabs(drawVertices[j].point.x - drawVertices[i].point.x) <= overlapTolerance &&
                        std::fabs(drawVertices[j].point.y - drawVertices[i].point.y) <= overlapTolerance)
                    {
                        if (drawVertices[j].depth < drawVertices[i].depth)
                        {
                            hidden[i] = true;
                            break;
                        }
                        hidden[j] = true;
                    }
                }
            }

            for (std::size_t i = 0; i < drawVertices.size(); ++i)
            {
                if (hidden[i])
                    continue;
                const bool selected = IsVertexSelected(drawVertices[i].index);
                const float radius = selected ? 5.5f : 4.0f;
                const ImU32 fillColor = selected ? IM_COL32(255, 210, 90, 255) : IM_COL32(235, 240, 245, 230);
                const ImU32 strokeColor = selected ? IM_COL32(255, 250, 215, 255) : IM_COL32(60, 75, 92, 255);
                drawList->AddCircleFilled(drawVertices[i].point, radius, fillColor, 10);
                drawList->AddCircle(drawVertices[i].point, radius, strokeColor, 10, 1.4f);
            }
        }
        else
        {
            for (const DrawVertex& drawVertex : drawVertices)
            {
                const bool selected = IsVertexSelected(drawVertex.index);
                const float radius = selected ? 5.5f : 4.0f;
                const ImU32 fillColor = selected ? IM_COL32(255, 210, 90, 255) : IM_COL32(235, 240, 245, 230);
                const ImU32 strokeColor = selected ? IM_COL32(255, 250, 215, 255) : IM_COL32(60, 75, 92, 255);
                drawList->AddCircleFilled(drawVertex.point, radius, fillColor, 10);
                drawList->AddCircle(drawVertex.point, radius, strokeColor, 10, 1.4f);
            }
        }
    }

    if (selectionMode_ == SelectionMode::Vertex &&
        hoveredVertexMeshIndex_ >= 0 &&
        hoveredVertexMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        hoveredVertexIndex_ >= 0)
    {
        const LevelMeshObject& hoveredObject = scene_.meshObjects()[(size_t)hoveredVertexMeshIndex_];
        if (hoveredVertexIndex_ < static_cast<int>(hoveredObject.mesh.vertexCount()))
        {
            const glm::mat4 hoveredModelMatrix = meshObjectModelMatrix(hoveredObject);
            ImVec2 hoverPoint;
            float hoverDepth = 0.0f;
            const glm::vec3 hoverWorld = glm::vec3(hoveredModelMatrix * glm::vec4(
                hoveredObject.mesh.vertices()[(size_t)hoveredVertexIndex_].position, 1.0f));
            if (projectWorld(hoverWorld, hoverPoint, hoverDepth))
            {
                const bool hoverSelected = hoveredVertexMeshIndex_ == selectedMeshIndex_ &&
                                           IsVertexSelected(hoveredVertexIndex_);
                const float hoverRadius = hoverSelected ? 7.0f : 6.0f;
                const ImU32 hoverFill = hoverSelected ? IM_COL32(255, 235, 150, 255) : IM_COL32(110, 240, 255, 245);
                const ImU32 hoverStroke = hoverSelected ? IM_COL32(255, 255, 230, 255) : IM_COL32(225, 255, 255, 255);
                drawList->AddCircleFilled(hoverPoint, hoverRadius, hoverFill, 14);
                drawList->AddCircle(hoverPoint, hoverRadius + 1.5f, hoverStroke, 14, 1.8f);
            }
        }
    }

    drawList->PopClipRect(); // content clip

    if (view.type == ViewType::Perspective)
    {
        const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
        drawList->AddLine(ImVec2(center.x, center.y), ImVec2(center.x + 48.0f, center.y), IM_COL32(220, 70, 70, 255), 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y), ImVec2(center.x, center.y - 48.0f), IM_COL32(80, 220, 110, 255), 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y), ImVec2(center.x - 34.0f, center.y + 34.0f), IM_COL32(80, 120, 240, 255), 2.0f);

        char cameraInfo[128];
        std::snprintf(cameraInfo, sizeof(cameraInfo), "Yaw %.0f  Pitch %.0f  Dist %.0f",
                      view.perspectiveYaw, view.perspectivePitch, view.perspectiveDistance);
        drawList->AddText(ImVec2(minPos.x + 10.0f, minPos.y + 34.0f), IM_COL32(170, 184, 198, 255), "3D camera");
        drawList->AddText(ImVec2(minPos.x + 10.0f, minPos.y + 54.0f), IM_COL32(170, 184, 198, 255), cameraInfo);
        drawList->AddText(ImVec2(minPos.x + 10.0f, maxPos.y - 22.0f), IM_COL32(140, 150, 162, 255), "Shift+RMB orbit  Ctrl+RMB pan  Wheel zoom");
    }
    else
    {
        char orthoInfo[64];
        std::snprintf(orthoInfo, sizeof(orthoInfo), "Ortho %.0f", view.orthoSize);
        drawList->AddText(ImVec2(minPos.x + 10.0f, minPos.y + 34.0f), IM_COL32(170, 184, 198, 255), orthoInfo);
    }

    drawList->AddText(ImVec2(maxPos.x - 120.0f, minPos.y + 6.0f), IM_COL32(155, 170, 190, 255), "Mesh");
    drawList->PopClipRect(); // tile clip
}

void LevelEditorApp::ShowMenuBar()
{
    Device& device = Device::Instance();
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Level"))
        {
            PushUndoState();
            scene_.reset();
            if (lightmapTexture_)
                glDeleteTextures(1, &lightmapTexture_);
            lightmapTexture_ = 0;
            lightmapResult_ = {};
            useLightmap_ = false;
            currentTexturePath_.clear();
            failedTextureLoads_.clear();
            SyncSelectedMeshes();
            SyncSelectedFaces();
            selectedEntityIndex_ = std::clamp(selectedEntityIndex_, 0, std::max(0, static_cast<int>(scene_.entities().size()) - 1));
            meshCacheValid_ = false;
            scenePath_.clear();
            sceneStatusMessage_ = "New scene";
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            sceneDialog_.Open(ImGuiFileDialog::Mode::OpenFile, std::filesystem::current_path(), "scene.mred");
        if (ImGui::MenuItem("Save", "Ctrl+S"))
        {
            if (!scenePath_.empty())
                SaveSceneToPath(scenePath_, false);
            else
                sceneDialog_.Open(ImGuiFileDialog::Mode::SaveFile, std::filesystem::current_path(), "scene.mred");
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
            sceneDialog_.Open(ImGuiFileDialog::Mode::SaveFile, std::filesystem::current_path(), "scene.mred");
        ImGui::Separator();
        if (ImGui::MenuItem("Import Mesh..."))
        {
            const std::filesystem::path startDir = lastImportDir_.empty()
                ? std::filesystem::current_path()
                : std::filesystem::path(lastImportDir_);
            importMeshDialog_.Open(ImGuiFileDialog::Mode::OpenFile, startDir, "mesh");
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Export"))
        {
            if (ImGui::MenuItem("Export OBJ..."))
                exportDialog_.Open(ImGuiFileDialog::Mode::SaveFile, std::filesystem::current_path(), "scene.obj");
            if (ImGui::MenuItem("Export H3D..."))
                exportDialog_.Open(ImGuiFileDialog::Mode::SaveFile, std::filesystem::current_path(), "scene.h3d");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack_.empty()))
            PerformUndo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack_.empty()))
            PerformRedo();
        ImGui::Separator();
        ImGui::MenuItem("Duplicate", "Ctrl+D", false, true);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Record"))
    {
        if (device.IsGifRecording())
        {
            if (ImGui::MenuItem("Stop GIF", "F8"))
            {
                const std::string path = device.GetGifRecordingPath();
                if (device.EndGifRecording())
                    sceneStatusMessage_ = "GIF saved: " + path;
                else
                    sceneStatusMessage_ = "GIF finalize failed";
            }
        }
        else
        {
            if (ImGui::MenuItem("Record GIF", "F8"))
            {
                if (device.BeginGifRecording())
                    sceneStatusMessage_ = "Recording GIF: " + device.GetGifRecordingPath();
                else
                    sceneStatusMessage_ = "Failed to start GIF recording";
            }
        }

        ImGui::Separator();

        if (device.IsFrameSequenceRecording())
        {
            if (ImGui::MenuItem("Stop Frame Sequence", "F9"))
            {
                const std::string directory = device.GetFrameSequenceDirectory();
                if (device.EndFrameSequenceRecording())
                    sceneStatusMessage_ = "Frames saved: " + directory;
                else
                    sceneStatusMessage_ = "Frame sequence finalize failed";
            }
        }
        else
        {
            if (ImGui::MenuItem("Export Frames PNG", "F9"))
            {
                if (device.BeginFrameSequenceRecording(nullptr, "png"))
                    sceneStatusMessage_ = "Recording PNG frames: " + device.GetFrameSequenceDirectory();
                else
                    sceneStatusMessage_ = "Failed to start PNG frame recording";
            }
            if (ImGui::MenuItem("Export Frames JPG"))
            {
                if (device.BeginFrameSequenceRecording(nullptr, "jpg"))
                    sceneStatusMessage_ = "Recording JPG frames: " + device.GetFrameSequenceDirectory();
                else
                    sceneStatusMessage_ = "Failed to start JPG frame recording";
            }
        }

        ImGui::Separator();
        const bool hasLastSequence = !device.GetLastFrameSequenceDirectory().empty();
        if (ImGui::MenuItem("Frames -> MP4", nullptr, false, hasLastSequence && !device.IsFrameSequenceRecording()))
        {
            if (device.ExportLastFrameSequenceToVideo())
                sceneStatusMessage_ = "Video exported from: " + device.GetLastFrameSequenceDirectory();
            else
                sceneStatusMessage_ = "Failed to export MP4 from last frame sequence";
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Show Grid", nullptr, &showGrid_);
        ImGui::MenuItem("Snap Enabled", nullptr, &snapEnabled_);
        ImGui::Separator();
        if (ImGui::MenuItem("1 View", nullptr, viewLayout_ == ViewLayout::One))
            viewLayout_ = ViewLayout::One;
        if (ImGui::MenuItem("2 Views", nullptr, viewLayout_ == ViewLayout::Two))
            viewLayout_ = ViewLayout::Two;
        if (ImGui::MenuItem("3 Views", nullptr, viewLayout_ == ViewLayout::Three))
            viewLayout_ = ViewLayout::Three;
        if (ImGui::MenuItem("4 Views", nullptr, viewLayout_ == ViewLayout::Four))
            viewLayout_ = ViewLayout::Four;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Theme"))
    {
        auto themeItem = [&](const char* name, LevelEditorTheme t) {
            if (ImGui::MenuItem(name, nullptr, theme_ == t))
            {
                theme_ = t;
                applyLevelEditorTheme(theme_);
            }
        };
        themeItem("Dark", LevelEditorTheme::Dark);
        themeItem("Light", LevelEditorTheme::Light);
        themeItem("Classic", LevelEditorTheme::Classic);
        themeItem("Studio", LevelEditorTheme::Studio);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void LevelEditorApp::ShowLeftPanel()
{
    ImGui::SetNextWindowPos(leftPanelPos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(leftPanelSize_, ImGuiCond_Always);
    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Scene", nullptr, panelFlags))
    {
        ImGui::End();
        return;
    }

    if (Section("Tools"))
    {
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::BeginCombo("Selection Mode", selectionModeName(selectionMode_)))
        {
            const SelectionMode modes[] = {
                SelectionMode::Object,
                SelectionMode::Face,
                SelectionMode::Vertex
            };
            for (SelectionMode mode : modes)
            {
                bool selected = mode == selectionMode_;
                if (ImGui::Selectable(selectionModeName(mode), selected))
                {
                    selectionMode_ = mode;
                    if (mode != SelectionMode::Vertex)
                        placeVertexMode_ = false;
                }
            }
            ImGui::EndCombo();
        }

        if (selectionMode_ == SelectionMode::Vertex)
            ImGui::Checkbox("Front Vertices Only", &vertexFrontOnly_);
    }

    if (Section("Mesh Objects"))
    {
        const auto& meshObjects = scene_.meshObjects();
        const int meshCount = static_cast<int>(meshObjects.size());

        if (selectionMode_ == SelectionMode::Object && meshCount > 0)
        {
            if (ImGui::Button("<##PrevObj") && meshCount > 0)
            {
                int idx = (selectedMeshIndex_ - 1 + meshCount) % meshCount;
                SetSingleSelectedMesh(idx);
            }
            ImGui::SameLine();
            if (ImGui::Button(">##NextObj") && meshCount > 0)
            {
                int idx = (selectedMeshIndex_ + 1) % meshCount;
                SetSingleSelectedMesh(idx);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            int objSlider = selectedMeshIndex_;
            if (ImGui::SliderInt("##ObjSlider", &objSlider, 0, std::max(0, meshCount - 1), "Object %d"))
                SetSingleSelectedMesh(objSlider);
        }

        for (int i = 0; i < meshCount; ++i)
        {
            ImGui::PushID(i);
            auto& obj = scene_.meshObjects()[i];

            // Eye icon (visible toggle)
            if (obj.visible)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.4f,0.4f,0.5f));
            if (ImGui::SmallButton(obj.visible ? "V" : "."))
                obj.visible = !obj.visible;
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // Lock icon
            if (obj.locked)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.5f,0.2f,1));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,0.6f));
            if (ImGui::SmallButton(obj.locked ? "L" : " "))
                obj.locked = !obj.locked;
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // Name selectable (dim if hidden)
            if (!obj.visible)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,0.5f));
            const bool selected = IsMeshSelected(i);
            if (ImGui::Selectable(obj.name.c_str(), selected))
            {
                if (!obj.locked)
                {
                    const ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyCtrl)
                    {
                        const auto it = std::find(selectedMeshIndices_.begin(), selectedMeshIndices_.end(), i);
                        if (it != selectedMeshIndices_.end())
                        {
                            selectedMeshIndices_.erase(it);
                            selectedMeshIndex_ = selectedMeshIndices_.empty() ? -1 : selectedMeshIndices_.front();
                        }
                        else
                        {
                            selectedMeshIndices_.push_back(i);
                            std::sort(selectedMeshIndices_.begin(), selectedMeshIndices_.end());
                            selectedMeshIndex_ = i;
                        }
                        selectedVertexIndices_.clear();
                        selectedFaceIndex_ = -1;
                        selectedFaceIndices_.clear();
                        selectedEntityIndex_ = -1;
                    }
                    else if (io.KeyShift)
                    {
                        if (!IsMeshSelected(i))
                            selectedMeshIndices_.push_back(i);
                        std::sort(selectedMeshIndices_.begin(), selectedMeshIndices_.end());
                        selectedMeshIndex_ = i;
                        selectedVertexIndices_.clear();
                        selectedFaceIndex_ = -1;
                        selectedFaceIndices_.clear();
                        selectedEntityIndex_ = -1;
                    }
                    else
                    {
                        SetSingleSelectedMesh(i);
                    }
                }
            }
            if (!obj.visible)
                ImGui::PopStyleColor();

            ImGui::PopID();
        }

        // Visibility / lock buttons
        if (ImGui::Button("Show All"))
        {
            for (auto& o : scene_.meshObjects()) o.visible = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Hide All"))
        {
            for (auto& o : scene_.meshObjects()) o.visible = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Hide Others"))
        {
            for (int i = 0; i < meshCount; ++i)
                scene_.meshObjects()[i].visible = IsMeshSelected(i);
        }
        ImGui::SameLine();
        if (ImGui::Button("Invert"))
        {
            for (auto& o : scene_.meshObjects()) o.visible = !o.visible;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Create Primitive");
        
        {
            const char* primNames[] = {"Box", "Room", "Sector", "Room Boxes", "Cylinder", "Cone", "Sphere", "Torus", "Tube", "Pyramid", "Door Frame", "Terrain", "Pillar", "Plane", "Wedge", "Stairs", "Spiral Stairs", "Text"};
            int primIdx = static_cast<int>(primitiveType_);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Type##Primitive", &primIdx, primNames, IM_ARRAYSIZE(primNames)))
                primitiveType_ = static_cast<PrimitiveType>(primIdx);

            switch (primitiveType_)
            {
            case PrimitiveType::Box:
                ImGui::DragFloat3("Size##PrimBox", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                break;
            case PrimitiveType::Room:
                ImGui::DragFloat3("Size##PrimRoom", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                ImGui::DragFloat("Wall##PrimRoomWall", &primWallThickness_, 0.5f, 1.0f, 1024.0f);
                break;
            case PrimitiveType::Sector:
                ImGui::DragFloat3("Size##PrimSector", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                ImGui::DragFloat("Wall##PrimSectorWall", &primWallThickness_, 0.5f, 1.0f, 1024.0f);
                ImGui::Checkbox("Left##PrimSectorLeft", &primSectorLeft_);
                ImGui::SameLine();
                ImGui::Checkbox("Right##PrimSectorRight", &primSectorRight_);
                ImGui::Checkbox("Top##PrimSectorTop", &primSectorTop_);
                ImGui::SameLine();
                ImGui::Checkbox("Bottom##PrimSectorBottom", &primSectorBottom_);
                ImGui::Checkbox("Front##PrimSectorFront", &primSectorFront_);
                ImGui::SameLine();
                ImGui::Checkbox("Back##PrimSectorBack", &primSectorBack_);
                if (ImGui::Button("All##PrimSectorAll"))
                {
                    primSectorLeft_ = primSectorRight_ = primSectorTop_ = primSectorBottom_ = primSectorFront_ = primSectorBack_ = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear##PrimSectorClear"))
                {
                    primSectorLeft_ = primSectorRight_ = primSectorTop_ = primSectorBottom_ = primSectorFront_ = primSectorBack_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Walls##PrimSectorWalls"))
                {
                    primSectorLeft_ = primSectorRight_ = primSectorFront_ = primSectorBack_ = true;
                    primSectorTop_ = primSectorBottom_ = false;
                }
                break;
            case PrimitiveType::RoomBoxes:
                ImGui::DragFloat3("Size##PrimRoomBoxes", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                ImGui::DragFloat("Wall##PrimRoomBoxesWall", &primWallThickness_, 0.5f, 1.0f, 1024.0f);
                ImGui::TextDisabled("Creates 6 separate box walls.");
                break;
            case PrimitiveType::Cylinder:
                ImGui::DragFloat("Radius##PrimCyl", &primRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragFloat("Height##PrimCyl", &primHeight_, 1.0f, 1.0f, 4096.0f);
                ImGui::DragInt("Segments##PrimCyl", &primSegments_, 1, 3, 128);
                break;
            case PrimitiveType::Cone:
                ImGui::DragFloat("Radius##PrimCone", &primRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragFloat("Height##PrimCone", &primHeight_, 1.0f, 1.0f, 4096.0f);
                ImGui::DragInt("Segments##PrimCone", &primSegments_, 1, 3, 128);
                break;
            case PrimitiveType::Sphere:
                ImGui::DragFloat("Radius##PrimSph", &primRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragInt("Rings##PrimSph", &primRings_, 1, 2, 64);
                ImGui::DragInt("Segments##PrimSph", &primSegments_, 1, 3, 128);
                break;
            case PrimitiveType::Torus:
                ImGui::DragFloat("Major Radius##PrimTorusMajor", &primRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragFloat("Minor Radius##PrimTorusMinor", &primMinorRadius_, 0.5f, 1.0f, 1024.0f);
                ImGui::DragInt("Major Segments##PrimTorusMajorSeg", &primSegments_, 1, 3, 128);
                ImGui::DragInt("Minor Segments##PrimTorusMinorSeg", &primRings_, 1, 3, 128);
                break;
            case PrimitiveType::Tube:
                ImGui::DragFloat("Outer Radius##PrimTubeOuter", &primRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragFloat("Inner Radius##PrimTubeInner", &primMinorRadius_, 0.5f, 1.0f, 1024.0f);
                ImGui::DragFloat("Height##PrimTubeHeight", &primHeight_, 1.0f, 1.0f, 4096.0f);
                ImGui::DragInt("Segments##PrimTubeSeg", &primSegments_, 1, 3, 128);
                break;
            case PrimitiveType::Pyramid:
                ImGui::DragFloat3("Size##PrimPyramid", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                break;
            case PrimitiveType::DoorFrame:
                ImGui::DragFloat3("Size##PrimDoorFrame", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                ImGui::DragFloat("Door Width##PrimDoorWidth", &primDoorWidth_, 1.0f, 1.0f, 4096.0f);
                ImGui::DragFloat("Door Height##PrimDoorHeight", &primDoorHeight_, 1.0f, 1.0f, 4096.0f);
                ImGui::DragFloat("Wall Thickness##PrimDoorWall", &primWallThickness_, 0.5f, 1.0f, 1024.0f);
                break;
            case PrimitiveType::Terrain:
                ImGui::DragFloat("Width##PrimTerrainWidth", &primPlaneW_, 1.0f, 1.0f, 8192.0f);
                ImGui::DragFloat("Depth##PrimTerrainDepth", &primPlaneD_, 1.0f, 1.0f, 8192.0f);
                ImGui::DragInt("Subdiv X##PrimTerrainSubdivX", &primSubdivX_, 1, 1, 2048);
                ImGui::DragInt("Subdiv Z##PrimTerrainSubdivZ", &primSubdivZ_, 1, 1, 2048);
                ImGui::DragFloat("Height Scale##PrimTerrainScale", &primHeightScale_, 1.0f, 0.0f, 4096.0f);
                ImGui::TextWrapped("Heightmap");
                ImGui::InputText("##PrimTerrainHeightmapPath", &primHeightmapPath_, ImGuiInputTextFlags_ReadOnly);
                if (ImGui::Button("Choose Heightmap##PrimTerrain"))
                {
                    std::filesystem::path startDir;
                    if (!lastTerrainHeightmapDir_.empty())
                        startDir = std::filesystem::path(lastTerrainHeightmapDir_);
                    else if (!primHeightmapPath_.empty())
                        startDir = std::filesystem::path(primHeightmapPath_).parent_path();
                    else if (!assetRoot_.empty())
                        startDir = std::filesystem::path(assetRoot_);
                    else
                        startDir = std::filesystem::current_path();
                    terrainHeightmapDialog_.Open(ImGuiFileDialog::Mode::OpenFile, startDir, "image");
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Heightmap##PrimTerrain"))
                    primHeightmapPath_.clear();
                break;
            case PrimitiveType::Pillar:
                ImGui::DragFloat3("Size##PrimPillar", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                ImGui::DragFloat("Base Ratio##PrimPillarBase", &primPillarBaseRatio_, 0.01f, 0.0f, 0.45f, "%.2f");
                ImGui::DragFloat("Capital Ratio##PrimPillarCapital", &primPillarCapitalRatio_, 0.01f, 0.0f, 0.45f, "%.2f");
                ImGui::DragFloat("Flare Ratio##PrimPillarFlare", &primPillarFlareRatio_, 0.01f, 0.0f, 1.0f, "%.2f");
                break;
            case PrimitiveType::Plane:
            {
                const char* orientNames[] = {"Top", "Bottom", "Front", "Back", "Left", "Right"};
                ImGui::SetNextItemWidth(100.0f);
                ImGui::Combo("Orient##PrimPlane", &primPlaneOrient_, orientNames, 6);
                ImGui::DragFloat("Width##PrimPlane", &primPlaneW_, 1.0f, 1.0f, 8192.0f);
                ImGui::DragFloat("Depth##PrimPlane", &primPlaneD_, 1.0f, 1.0f, 8192.0f);
                ImGui::DragInt("Subdiv X##PrimPlane", &primSubdivX_, 1, 1, 256);
                ImGui::DragInt("Subdiv Z##PrimPlane", &primSubdivZ_, 1, 1, 256);
                break;
            }
            case PrimitiveType::Wedge:
                ImGui::DragFloat3("Size##PrimWedge", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                break;
            case PrimitiveType::Stairs:
                ImGui::DragFloat3("Size##PrimStairs", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                ImGui::DragInt("Steps##PrimStairs", &primStairSteps_, 1, 1, 128);
                break;
            case PrimitiveType::SpiralStairs:
                ImGui::DragFloat("Inner Radius##PrimSpiral", &primInnerRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragFloat("Outer Radius##PrimSpiral", &primOuterRadius_, 1.0f, 2.0f, 4096.0f);
                ImGui::DragFloat("Height##PrimSpiral", &primHeight_, 1.0f, 1.0f, 4096.0f);
                ImGui::DragInt("Steps##PrimSpiral", &primStairSteps_, 1, 1, 128);
                ImGui::DragFloat("Angle##PrimSpiral", &primSpiralAngle_, 1.0f, 10.0f, 3600.0f, "%.0f deg");
                break;
            case PrimitiveType::Text:
                ImGui::InputText("Text##PrimText", &primText_);
                ImGui::InputText("Font##PrimFont", &primFontPath_);
                ImGui::DragFloat("Size##PrimText", &primTextSize_, 1.0f, 4.0f, 512.0f);
                ImGui::DragFloat("Extrude##PrimText", &primTextExtrude_, 0.5f, 0.0f, 256.0f);
                ImGui::DragInt("Curve Quality##PrimText", &primTextCurveQuality_, 1, 1, 10);
                break;
            }

            if (ImGui::Button("Create##Primitive"))
            {
                PushUndoState();
                LevelMeshObject object;
                const glm::vec3 half = primSize_ * 0.5f;
                const PrimitiveType primitiveTypeAtCreate = primitiveType_;
                bool createdMultipleObjects = false;
                bool cancelCreation = false;

                switch (primitiveTypeAtCreate)
                {
                case PrimitiveType::Box:
                    object.primitive = LevelMeshPrimitive::Box;
                    object.name = "Box " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeBox(-half, half);
                    break;
                case PrimitiveType::Room:
                    object.primitive = LevelMeshPrimitive::Room;
                    object.name = "Room " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeRoom(-half, half, primWallThickness_);
                    break;
                case PrimitiveType::Sector:
                    object.primitive = LevelMeshPrimitive::Sector;
                    object.name = "Sector " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    if (!primSectorLeft_ && !primSectorRight_ && !primSectorTop_ && !primSectorBottom_ && !primSectorFront_ && !primSectorBack_)
                    {
                        sceneStatusMessage_ = "Sector needs at least one side enabled";
                        cancelCreation = true;
                        break;
                    }
                    object.mesh = EditableMesh::MakeSector(
                        -half, half, primWallThickness_,
                        primSectorLeft_, primSectorRight_, primSectorTop_, primSectorBottom_, primSectorFront_, primSectorBack_);
                    break;
                case PrimitiveType::RoomBoxes:
                {
                    const glm::vec3 minBounds = -half;
                    const glm::vec3 maxBounds = half;
                    const float maxThickness = std::min(std::min(primSize_.x, primSize_.y), primSize_.z) * 0.5f - 0.001f;
                    const float thickness = (maxThickness > 0.001f)
                        ? glm::clamp(primWallThickness_, 0.001f, maxThickness)
                        : 0.001f;
                    const float innerMinX = minBounds.x + thickness;
                    const float innerMaxX = maxBounds.x - thickness;
                    const float innerMinZ = minBounds.z + thickness;
                    const float innerMaxZ = maxBounds.z - thickness;
                    struct BoxDef
                    {
                        const char* name;
                        glm::vec3 min;
                        glm::vec3 max;
                    };
                    const std::vector<BoxDef> boxDefs = {
                        {"Floor",   {innerMinX, minBounds.y, innerMinZ}, {innerMaxX, minBounds.y + thickness, innerMaxZ}},
                        {"Ceiling", {innerMinX, maxBounds.y - thickness, innerMinZ}, {innerMaxX, maxBounds.y, innerMaxZ}},
                        {"Left",    {minBounds.x, minBounds.y, minBounds.z}, {minBounds.x + thickness, maxBounds.y, maxBounds.z}},
                        {"Right",   {maxBounds.x - thickness, minBounds.y, minBounds.z}, {maxBounds.x, maxBounds.y, maxBounds.z}},
                        {"Front",   {innerMinX, minBounds.y, minBounds.z}, {innerMaxX, maxBounds.y, minBounds.z + thickness}},
                        {"Back",    {innerMinX, minBounds.y, maxBounds.z - thickness}, {innerMaxX, maxBounds.y, maxBounds.z}},
                    };
                    const int baseIndex = static_cast<int>(scene_.meshObjects().size()) + 1;
                    for (std::size_t i = 0; i < boxDefs.size(); ++i)
                    {
                        LevelMeshObject part;
                        part.name = std::string(boxDefs[i].name) + " " + std::to_string(baseIndex);
                        part.primitive = LevelMeshPrimitive::RoomBoxesPart;
                        part.mesh = EditableMesh::MakeBox(boxDefs[i].min, boxDefs[i].max);
                        scene_.meshObjects().push_back(std::move(part));
                    }
                    SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
                    meshCacheValid_ = false;
                    createdMultipleObjects = true;
                    break;
                }
                case PrimitiveType::Cylinder:
                    object.primitive = LevelMeshPrimitive::Cylinder;
                    object.name = "Cylinder " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeCylinder(glm::vec3(0.0f), primRadius_, primHeight_, primSegments_);
                    break;
                case PrimitiveType::Cone:
                    object.primitive = LevelMeshPrimitive::Cone;
                    object.name = "Cone " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeCone(glm::vec3(0.0f), primRadius_, primHeight_, primSegments_);
                    break;
                case PrimitiveType::Sphere:
                    object.primitive = LevelMeshPrimitive::Sphere;
                    object.name = "Sphere " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeSphere(glm::vec3(0.0f), primRadius_, primRings_, primSegments_);
                    break;
                case PrimitiveType::Torus:
                    object.primitive = LevelMeshPrimitive::Torus;
                    object.name = "Torus " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeTorus(glm::vec3(0.0f), primRadius_, primMinorRadius_, primSegments_, primRings_);
                    break;
                case PrimitiveType::Tube:
                    object.primitive = LevelMeshPrimitive::Tube;
                    object.name = "Tube " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeTube(glm::vec3(0.0f), primRadius_, primMinorRadius_, primHeight_, primSegments_);
                    break;
                case PrimitiveType::Pyramid:
                    object.primitive = LevelMeshPrimitive::Pyramid;
                    object.name = "Pyramid " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakePyramid(glm::vec3(0.0f), primSize_.x, primSize_.z, primSize_.y);
                    break;
                case PrimitiveType::DoorFrame:
                    object.primitive = LevelMeshPrimitive::DoorFrame;
                    object.name = "Door Frame " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeDoorFrame(-half, half, primDoorWidth_, primDoorHeight_, primWallThickness_);
                    break;
                case PrimitiveType::Terrain:
                {
                    object.primitive = LevelMeshPrimitive::Terrain;
                    object.name = "Terrain " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    std::vector<float> heights;
                    if (!primHeightmapPath_.empty())
                    {
                        int width = 0;
                        int height = 0;
                        std::string error;
                        if (!LoadHeightmapImage(primHeightmapPath_, heights, width, height, error))
                        {
                            sceneStatusMessage_ = "Heightmap load failed: " + error;
                            cancelCreation = true;
                            break;
                        }
                        if (width != primSubdivX_ + 1 || height != primSubdivZ_ + 1)
                        {
                            sceneStatusMessage_ = "Heightmap size must match subdivs + 1";
                            cancelCreation = true;
                            break;
                        }
                    }
                    object.mesh = EditableMesh::MakeTerrain(glm::vec3(0.0f), primPlaneW_, primPlaneD_, primSubdivX_, primSubdivZ_, heights, primHeightScale_);
                    break;
                }
                case PrimitiveType::Pillar:
                    object.primitive = LevelMeshPrimitive::Pillar;
                    object.name = "Pillar " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakePillar(-half, half, primPillarBaseRatio_, primPillarCapitalRatio_, primPillarFlareRatio_);
                    break;
                case PrimitiveType::Plane:
                {
                    object.primitive = LevelMeshPrimitive::Plane;
                    object.name = "Plane " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakePlane(glm::vec3(0.0f), primPlaneW_, primPlaneD_, primSubdivX_, primSubdivZ_);
                    // Rotate plane to match selected orientation
                    // MakePlane generates Y-up (Top). Rotate verts for other orientations.
                    if (primPlaneOrient_ != 0)
                    {
                        glm::mat3 rot(1.0f);
                        switch (primPlaneOrient_)
                        {
                        case 1: rot = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1,0,0))); break; // Bottom
                        case 2: rot = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1,0,0))); break; // Front
                        case 3: rot = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0))); break;  // Back
                        case 4: rot = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0,0,1))); break;  // Left
                        case 5: rot = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1))); break; // Right
                        }
                        for (auto& v : object.mesh.verticesMutable())
                        {
                            v.position = rot * v.position;
                            v.normal = rot * v.normal;
                        }
                    }
                    break;
                }
                case PrimitiveType::Wedge:
                    object.primitive = LevelMeshPrimitive::Wedge;
                    object.name = "Wedge " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeWedge(-half, half);
                    break;
                case PrimitiveType::Stairs:
                    object.primitive = LevelMeshPrimitive::Stairs;
                    object.name = "Stairs " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeStairs(-half, half, primStairSteps_);
                    break;
                case PrimitiveType::SpiralStairs:
                    object.primitive = LevelMeshPrimitive::SpiralStairs;
                    object.name = "Spiral " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeSpiralStairs(glm::vec3(0.0f), primInnerRadius_, primOuterRadius_, primHeight_, primStairSteps_, primSpiralAngle_);
                    break;
                case PrimitiveType::Text:
                    object.primitive = LevelMeshPrimitive::Text;
                    object.name = "Text " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeText(primText_, primFontPath_, primTextSize_, primTextExtrude_, primTextCurveQuality_);
                    break;
                }

                if (!cancelCreation && !createdMultipleObjects)
                {
                    scene_.meshObjects().push_back(object);
                    SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
                    meshCacheValid_ = false;
                }
                if (!cancelCreation)
                    sceneDirty_ = true;
            }
        }
    }
 
    if (ImGui::Button("Create Empty"))
    {
        PushUndoState();
        LevelMeshObject object;
        object.name = "Empty " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
        object.primitive = LevelMeshPrimitive::Empty;
        object.mesh = EditableMesh::FromData({}, {});
        scene_.meshObjects().push_back(object);
        SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
        meshCacheValid_ = false;
        sceneDirty_ = true;
    }
    if (Section("Entities"))
    {
        const auto& entities = scene_.entities();
        for (int i = 0; i < static_cast<int>(entities.size()); ++i)
        {
            const bool selected = i == selectedEntityIndex_;
            ImGui::PushID(i);
            if (ImGui::Selectable(entities[i].name.c_str(), selected))
            {
                selectedEntityIndex_ = i;
                selectedMeshIndex_ = -1; // deselect mesh when selecting entity
            }
            ImGui::PopID();
        }

        if (ImGui::Button("Add Light"))
        {
            PushUndoState();
            LevelEntityObject entity;
            // Auto-number: find highest existing Light index
            int maxIdx = 0;
            for (const auto& e : scene_.entities())
                if (e.type == LevelEntityType::Light)
                    ++maxIdx;
            entity.name = "Light " + std::to_string(maxIdx + 1);
            entity.type = LevelEntityType::Light;
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Door"))
        {
            PushUndoState();
            LevelEntityObject entity;
            int maxIdx = 0;
            for (const auto& e : scene_.entities())
                if (e.type == LevelEntityType::Door)
                    ++maxIdx;
            entity.name = "Door " + std::to_string(maxIdx + 1);
            entity.type = LevelEntityType::Door;
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Player Start"))
        {
            PushUndoState();
            LevelEntityObject entity;
            int maxIdx = 0;
            for (const auto& e : scene_.entities())
                if (e.type == LevelEntityType::PlayerStart)
                    ++maxIdx;
            entity.name = "Player Start " + std::to_string(maxIdx + 1);
            entity.type = LevelEntityType::PlayerStart;
            entity.direction = glm::vec3(0, 0, -1);
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }

        if (ImGui::Button("Add Elevator"))
        {
            PushUndoState();
            LevelEntityObject entity;
            int maxIdx = 0;
            for (const auto& e : scene_.entities())
                if (e.type == LevelEntityType::Elevator)
                    ++maxIdx;
            entity.name = "Elevator " + std::to_string(maxIdx + 1);
            entity.type = LevelEntityType::Elevator;
            entity.endPosition = glm::vec3(0, 128, 0);
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Platform"))
        {
            PushUndoState();
            LevelEntityObject entity;
            int maxIdx = 0;
            for (const auto& e : scene_.entities())
                if (e.type == LevelEntityType::Platform)
                    ++maxIdx;
            entity.name = "Platform " + std::to_string(maxIdx + 1);
            entity.type = LevelEntityType::Platform;
            entity.endPosition = glm::vec3(128, 0, 0);
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Placement"))
        {
            PushUndoState();
            LevelEntityObject entity;
            int maxIdx = 0;
            for (const auto& e : scene_.entities())
                if (e.type == LevelEntityType::Placement)
                    ++maxIdx;
            entity.name = "Item " + std::to_string(maxIdx + 1);
            entity.type = LevelEntityType::Placement;
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }

        // Delete selected entity
        if (selectedEntityIndex_ >= 0 && selectedEntityIndex_ < static_cast<int>(scene_.entities().size()))
        {
            ImGui::SameLine();
            if (ImGui::Button("Delete##Entity"))
            {
                PushUndoState();
                scene_.entities().erase(scene_.entities().begin() + selectedEntityIndex_);
                if (selectedEntityIndex_ >= static_cast<int>(scene_.entities().size()))
                    selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
                sceneDirty_ = true;
            }
        }
    }

    if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
    {
        LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];

        if (Section("Mesh Edit"))
        {
            ImGui::PushID("MeshEditSection");

            ImGui::Text("Vertices: %d", static_cast<int>(meshObject.mesh.vertexCount()));
            ImGui::SameLine();
            ImGui::Text("Faces: %d", static_cast<int>(meshObject.mesh.faceCount()));

            if (ImGui::Button("Recalculate Normals"))
            {
                PushUndoState();
                auto& verts = meshObject.mesh.verticesMutable();
                // Reset all normals to zero
                for (auto& v : verts)
                    v.normal = glm::vec3(0.0f);
                // Accumulate area-weighted face normals
                for (const auto& face : meshObject.mesh.faces())
                {
                    if (face.indices.size() < 3) continue;
                    // Newell's method for polygon normal
                    glm::vec3 faceN(0.0f);
                    const int n = static_cast<int>(face.indices.size());
                    for (int i = 0; i < n; ++i)
                    {
                        const glm::vec3& curr = verts[(size_t)face.indices[i]].position;
                        const glm::vec3& next = verts[(size_t)face.indices[(i + 1) % n]].position;
                        faceN.x += (curr.y - next.y) * (curr.z + next.z);
                        faceN.y += (curr.z - next.z) * (curr.x + next.x);
                        faceN.z += (curr.x - next.x) * (curr.y + next.y);
                    }
                    // faceN length is proportional to area — don't normalize, so bigger faces contribute more
                    for (int idx : face.indices)
                        verts[(size_t)idx].normal += faceN;
                }
                // Normalize
                for (auto& v : verts)
                {
                    const float len = glm::length(v.normal);
                    v.normal = len > 1e-8f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
                }
                sceneDirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Flat Normals"))
            {
                PushUndoState();
                // Duplicate vertices per face so each face has independent flat normals
                std::vector<EditableVertex> newVerts;
                std::vector<EditableFace> newFaces;
                for (const auto& face : meshObject.mesh.faces())
                {
                    if (face.indices.size() < 3) continue;
                    // Newell's method
                    glm::vec3 faceN(0.0f);
                    const int n = static_cast<int>(face.indices.size());
                    for (int i = 0; i < n; ++i)
                    {
                        const glm::vec3& curr = meshObject.mesh.vertices()[(size_t)face.indices[i]].position;
                        const glm::vec3& next = meshObject.mesh.vertices()[(size_t)face.indices[(i + 1) % n]].position;
                        faceN.x += (curr.y - next.y) * (curr.z + next.z);
                        faceN.y += (curr.z - next.z) * (curr.x + next.x);
                        faceN.z += (curr.x - next.x) * (curr.y + next.y);
                    }
                    const float fLen = glm::length(faceN);
                    faceN = fLen > 1e-8f ? faceN / fLen : glm::vec3(0.0f, 1.0f, 0.0f);

                    EditableFace nf;
                    nf.materialName = face.materialName;
                    for (int idx : face.indices)
                    {
                        EditableVertex v = meshObject.mesh.vertices()[(size_t)idx];
                        v.normal = faceN;
                        nf.indices.push_back(static_cast<int>(newVerts.size()));
                        newVerts.push_back(v);
                    }
                    newFaces.push_back(nf);
                }
                meshObject.mesh.setData(newVerts, newFaces);
                selectedFaceIndex_ = -1;
                selectedFaceIndices_.clear();
                selectedVertexIndices_.clear();
                sceneDirty_ = true;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Hollow");
            ImGui::DragFloat("Wall Thickness##HollowWallThickness", &hollowWallThickness_, 0.5f, 0.5f, 4096.0f);
            if (ImGui::Button("Hollow Box"))
            {
                const BoundingBox bounds = editableMeshLocalBounds(meshObject.mesh);
                const glm::vec3 size = bounds.max - bounds.min;
                const float maxThickness = std::min(std::min(size.x, size.y), size.z) * 0.5f - 0.001f;
                if (maxThickness > 0.001f)
                {
                    PushUndoState();
                    meshObject.mesh = EditableMesh::MakeHollowBox(bounds.min, bounds.max, hollowWallThickness_);
                    selectedFaceIndex_ = -1;
                    selectedFaceIndices_.clear();
                    selectedVertexIndices_.clear();
                    sceneDirty_ = true;
                }
            }

            if (selectionMode_ == SelectionMode::Vertex)
            {
                ImGui::Separator();
                if (ImGui::Checkbox("Place Vertex Mode", &placeVertexMode_))
                {
                    if (placeVertexMode_)
                        selectedVertexIndices_.clear(); // start fresh
                }
                if (placeVertexMode_)
                    ImGui::TextDisabled("Click ortho view to place vertices");
                else
                    ImGui::TextDisabled("Box-select verts (Shift=add) then Create Face");
                const int vtxCount = static_cast<int>(meshObject.mesh.vertexCount());
                if (vtxCount > 0)
                {
                    int singleVtx = selectedVertexIndices_.empty() ? 0 : selectedVertexIndices_[0];
                    if (ImGui::Button("<##PrevVtx"))
                    {
                        singleVtx = (singleVtx - 1 + vtxCount) % vtxCount;
                        selectedVertexIndices_ = {singleVtx};
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(">##NextVtx"))
                    {
                        singleVtx = (singleVtx + 1) % vtxCount;
                        selectedVertexIndices_ = {singleVtx};
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::SliderInt("##VtxSlider", &singleVtx, 0, std::max(0, vtxCount - 1), "Vertex %d"))
                        selectedVertexIndices_ = {singleVtx};
                }

                if (!selectedVertexIndices_.empty())
                {
                    ImGui::Text("Selected: %d", static_cast<int>(selectedVertexIndices_.size()));

                    // Create face from selected vertices (need 3+)
                    ImGui::BeginDisabled(selectedVertexIndices_.size() < 3);
                    if (ImGui::Button("Create Face"))
                    {
                        PushUndoState();
                        EditableFace newFace;
                        newFace.materialName = currentTexturePath_.empty() ? "default" : currentTexturePath_;
                        for (int idx : selectedVertexIndices_)
                            newFace.indices.push_back(idx);
                        meshObject.mesh.facesMutable().push_back(newFace);
                        sceneDirty_ = true;
                    }
                    ImGui::EndDisabled();

                    // Merge 2+ selected vertices into one (midpoint)
                    ImGui::BeginDisabled(selectedVertexIndices_.size() < 2);
                    ImGui::SameLine();
                    if (ImGui::Button("Merge Vertices"))
                    {
                        PushUndoState();
                        auto& verts = meshObject.mesh.verticesMutable();
                        auto& faces = meshObject.mesh.facesMutable();

                        // Compute midpoint of selected vertices
                        glm::vec3 mid(0.0f);
                        glm::vec3 avgNormal(0.0f);
                        for (int idx : selectedVertexIndices_)
                        {
                            if (idx >= 0 && idx < static_cast<int>(verts.size()))
                            {
                                mid += verts[(size_t)idx].position;
                                avgNormal += verts[(size_t)idx].normal;
                            }
                        }
                        mid /= static_cast<float>(selectedVertexIndices_.size());
                        if (glm::length(avgNormal) > 1e-6f)
                            avgNormal = glm::normalize(avgNormal);
                        else
                            avgNormal = glm::vec3(0.0f, 1.0f, 0.0f);

                        // Keep the first selected vertex, move it to midpoint
                        const int keepIdx = selectedVertexIndices_[0];
                        if (keepIdx >= 0 && keepIdx < static_cast<int>(verts.size()))
                        {
                            verts[(size_t)keepIdx].position = mid;
                            verts[(size_t)keepIdx].normal = avgNormal;
                        }

                        // Build set of merged indices (all except keepIdx)
                        std::set<int> mergedSet(selectedVertexIndices_.begin(), selectedVertexIndices_.end());
                        mergedSet.erase(keepIdx);

                        // Remap face indices: merged verts → keepIdx
                        for (auto& face : faces)
                        {
                            for (auto& idx : face.indices)
                            {
                                if (mergedSet.count(idx))
                                    idx = keepIdx;
                            }
                            // Remove degenerate edges (same vertex twice in a row)
                            std::vector<int> cleaned;
                            for (std::size_t ci = 0; ci < face.indices.size(); ++ci)
                            {
                                const int next = face.indices[(ci + 1) % face.indices.size()];
                                if (face.indices[ci] != next)
                                    cleaned.push_back(face.indices[ci]);
                            }
                            face.indices = cleaned;
                        }

                        // Remove faces with < 3 vertices (degenerate)
                        faces.erase(std::remove_if(faces.begin(), faces.end(),
                            [](const EditableFace& f) { return f.indices.size() < 3; }),
                            faces.end());

                        // Remove unused vertices and remap
                        std::vector<bool> used(verts.size(), false);
                        for (const auto& f : faces)
                            for (int idx : f.indices)
                                if (idx >= 0 && idx < static_cast<int>(used.size()))
                                    used[(size_t)idx] = true;

                        std::vector<int> remap(verts.size(), -1);
                        std::vector<EditableVertex> keptVerts;
                        for (std::size_t i = 0; i < verts.size(); ++i)
                        {
                            if (used[i])
                            {
                                remap[i] = static_cast<int>(keptVerts.size());
                                keptVerts.push_back(verts[i]);
                            }
                        }
                        std::vector<EditableFace> keptFaces;
                        for (const auto& f : faces)
                        {
                            EditableFace remapped;
                            remapped.materialName = f.materialName;
                            remapped.uvOffset = f.uvOffset;
                            remapped.uvScale = f.uvScale;
                            remapped.uvRotation = f.uvRotation;
                            remapped.uvProjection = f.uvProjection;
                            bool valid = true;
                            for (int idx : f.indices)
                            {
                                if (idx < 0 || idx >= static_cast<int>(remap.size()) || remap[(size_t)idx] < 0)
                                { valid = false; break; }
                                remapped.indices.push_back(remap[(size_t)idx]);
                            }
                            if (valid && remapped.indices.size() >= 3)
                                keptFaces.push_back(remapped);
                        }

                        meshObject.mesh.setData(keptVerts, keptFaces);
                        selectedVertexIndices_.clear();
                        selectedFaceIndex_ = -1;
                        selectedFaceIndices_.clear();
                        sceneDirty_ = true;
                    }
                    ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (ImGui::Button("Clear Sel"))
                        selectedVertexIndices_.clear();
                    ImGui::SameLine();
                    if (ImGui::Button("Delete Selected Vertices"))
                    {
                        PushUndoState();
                        std::vector<bool> removeVtx(meshObject.mesh.vertexCount(), false);
                        for (int idx : selectedVertexIndices_)
                            if (idx >= 0 && idx < static_cast<int>(removeVtx.size()))
                                removeVtx[(size_t)idx] = true;

                        std::vector<int> remap(meshObject.mesh.vertexCount(), -1);
                        std::vector<EditableVertex> keptVerts;
                        for (std::size_t i = 0; i < meshObject.mesh.vertexCount(); ++i)
                        {
                            if (!removeVtx[i])
                            {
                                remap[i] = static_cast<int>(keptVerts.size());
                                keptVerts.push_back(meshObject.mesh.vertices()[i]);
                            }
                        }

                        std::vector<EditableFace> keptFaces;
                        for (const auto& face : meshObject.mesh.faces())
                        {
                            EditableFace remapped;
                            remapped.materialName = face.materialName;
                            bool valid = true;
                            for (int idx : face.indices)
                            {
                                if (idx < 0 || idx >= static_cast<int>(remap.size()) || remap[(size_t)idx] < 0)
                                { valid = false; break; }
                                remapped.indices.push_back(remap[(size_t)idx]);
                            }
                            if (valid && remapped.indices.size() >= 3)
                                keptFaces.push_back(remapped);
                        }

                        meshObject.mesh.setData(keptVerts, keptFaces);
                        selectedVertexIndices_.clear();
                        selectedFaceIndex_ = -1;
                        selectedFaceIndices_.clear();
                        sceneDirty_ = true;
                    }
                }
            }

            if (selectionMode_ == SelectionMode::Face &&
                selectedFaceIndex_ >= 0 && selectedFaceIndex_ < static_cast<int>(meshObject.mesh.faceCount()))
            {
                ImGui::Separator();
                {
                    const int faceCount = static_cast<int>(meshObject.mesh.faceCount());
                    if (ImGui::Button("<##PrevFace") && faceCount > 0)
                        SetSingleSelectedFace((selectedFaceIndex_ - 1 + faceCount) % faceCount);
                    ImGui::SameLine();
                    if (ImGui::Button(">##NextFace") && faceCount > 0)
                        SetSingleSelectedFace((selectedFaceIndex_ + 1) % faceCount);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    int faceSlider = selectedFaceIndex_;
                    if (ImGui::SliderInt("##FaceSlider", &faceSlider, 0, std::max(0, faceCount - 1), "Face %d"))
                        SetSingleSelectedFace(faceSlider);
                    ImGui::TextDisabled("Selected faces: %d", static_cast<int>(selectedFaceIndices_.size()));
                }
                ImGui::DragFloat("Extrude##FaceExtrudeDistance", &faceExtrudeDistance_, 0.5f, -1024.0f, 1024.0f);
                if (ImGui::Button("Extrude Face"))
                {
                    if (std::fabs(faceExtrudeDistance_) > 1e-4f)
                    {
                        PushUndoState();
                        if (extrudeMeshFace(meshObject, selectedFaceIndex_, faceExtrudeDistance_))
                            sceneDirty_ = true;
                    }
                }
                    ImGui::SameLine();
                if (ImGui::Button("Delete Face"))
                {
                    PushUndoState();
                    auto& faces = meshObject.mesh.facesMutable();
                    faces.erase(faces.begin() + selectedFaceIndex_);

                    std::vector<bool> used(meshObject.mesh.vertexCount(), false);
                    for (const auto& f : meshObject.mesh.faces())
                        for (int idx : f.indices)
                            if (idx >= 0 && idx < static_cast<int>(used.size()))
                                used[(size_t)idx] = true;

                    std::vector<int> remap(meshObject.mesh.vertexCount(), -1);
                    std::vector<EditableVertex> keptVerts;
                    for (std::size_t i = 0; i < used.size(); ++i)
                    {
                        if (used[i])
                        {
                            remap[i] = static_cast<int>(keptVerts.size());
                            keptVerts.push_back(meshObject.mesh.vertices()[i]);
                        }
                    }

                    std::vector<EditableFace> keptFaces;
                    for (const auto& f : meshObject.mesh.faces())
                    {
                        EditableFace remapped;
                        remapped.materialName = f.materialName;
                        bool valid = true;
                        for (int idx : f.indices)
                        {
                            if (idx < 0 || idx >= static_cast<int>(remap.size()) || remap[(size_t)idx] < 0)
                            { valid = false; break; }
                            remapped.indices.push_back(remap[(size_t)idx]);
                        }
                        if (valid && remapped.indices.size() >= 3)
                            keptFaces.push_back(remapped);
                    }

                    meshObject.mesh.setData(keptVerts, keptFaces);
                    selectedFaceIndex_ = -1;
                    selectedFaceIndices_.clear();
                    selectedVertexIndices_.clear();
                    sceneDirty_ = true;
                }

                if (ImGui::Button("Flip Normal"))
                {
                    PushUndoState();
                    auto& indices = meshObject.mesh.facesMutable()[(size_t)selectedFaceIndex_].indices;
                    std::reverse(indices.begin(), indices.end());
                    sceneDirty_ = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Flip All Normals"))
                {
                    PushUndoState();
                    for (auto& face : meshObject.mesh.facesMutable())
                        std::reverse(face.indices.begin(), face.indices.end());
                    sceneDirty_ = true;
                }

                ImGui::DragFloat("Inset##FaceInsetAmount", &faceInsetAmount_, 0.5f, 0.1f, 1024.0f);
                if (ImGui::Button("Inset Face"))
                {
                    PushUndoState();
                    const EditableFace& srcFace = meshObject.mesh.faces()[(size_t)selectedFaceIndex_];
                    const auto& verts = meshObject.mesh.vertices();
                    if (srcFace.indices.size() >= 3)
                    {
                        // Compute face center
                        glm::vec3 center(0.0f);
                        for (int idx : srcFace.indices)
                            center += verts[(size_t)idx].position;
                        center /= static_cast<float>(srcFace.indices.size());

                        // Create inner ring vertices (moved toward center)
                        const int innerBase = static_cast<int>(meshObject.mesh.vertexCount());
                        std::vector<EditableVertex> newVerts = meshObject.mesh.vertices();
                        for (int idx : srcFace.indices)
                        {
                            EditableVertex v = verts[(size_t)idx];
                            const glm::vec3 dir = center - v.position;
                            const float len = glm::length(dir);
                            if (len > 1e-6f)
                                v.position += glm::normalize(dir) * std::min(faceInsetAmount_, len * 0.99f);
                            newVerts.push_back(v);
                        }

                        // Build new faces: quads between outer and inner, plus inner face
                        std::vector<EditableFace> newFaces = meshObject.mesh.faces();
                        // Replace original face with inner face
                        EditableFace innerFace;
                        innerFace.materialName = srcFace.materialName;
                        for (int i = 0; i < static_cast<int>(srcFace.indices.size()); ++i)
                            innerFace.indices.push_back(innerBase + i);
                        newFaces[(size_t)selectedFaceIndex_] = innerFace;

                        // Add side quads (outer edge → inner edge)
                        const int n = static_cast<int>(srcFace.indices.size());
                        for (int i = 0; i < n; ++i)
                        {
                            const int next = (i + 1) % n;
                            EditableFace quad;
                            quad.materialName = srcFace.materialName;
                            quad.indices = {
                                srcFace.indices[i],
                                srcFace.indices[next],
                                innerBase + next,
                                innerBase + i
                            };
                            newFaces.push_back(quad);
                        }

                        meshObject.mesh.setData(newVerts, newFaces);
                        sceneDirty_ = true;
                    }
                }
            }

            if (selectionMode_ == SelectionMode::Object)
            {
                if (ImGui::Button("Invert Mesh"))
                {
                    PushUndoState();
                    int invertedCount = 0;
                    for (EditableFace& face : meshObject.mesh.facesMutable())
                    {
                        if (face.indices.size() >= 3)
                        {
                            std::reverse(face.indices.begin(), face.indices.end());
                            ++invertedCount;
                        }
                    }
                    if (invertedCount > 0)
                    {
                        meshCacheValid_ = false;
                        sceneDirty_ = true;
                        sceneStatusMessage_ = "Inverted mesh faces";
                    }
                }
            }

            // UV operations — per-face if face selected, otherwise whole object
            ImGui::Separator();
            ImGui::TextUnformatted("UV Projection");
            const bool uvPerFace = (selectionMode_ == SelectionMode::Face && selectedFaceIndex_ >= 0
                && selectedFaceIndex_ < static_cast<int>(meshObject.mesh.faceCount()));
            if (uvPerFace)
                ImGui::TextDisabled(selectedFaceIndices_.size() > 1 ? "(applying to selected faces)" : "(applying to selected face)");
            else
                ImGui::TextDisabled("(applying to all faces)");

            static const char* uvProjNames[] = {"Box", "Planar", "Cylindrical", "Spherical", "Mesh"};
            auto applyProjection = [&](UvProjection proj) {
                PushUndoState();
                auto apply = [&](EditableFace& f) {
                    f.uvProjection = proj;
                    f.uvOffset = glm::vec2(0.0f);
                    f.uvScale = glm::vec2(1.0f);
                    f.uvRotation = 0.0f;
                };
                if (uvPerFace)
                {
                    for (int faceIndex : selectedFaceIndices_)
                    {
                        if (faceIndex >= 0 && faceIndex < static_cast<int>(meshObject.mesh.faceCount()))
                            apply(meshObject.mesh.facesMutable()[(size_t)faceIndex]);
                    }
                }
                else
                    for (auto& f : meshObject.mesh.facesMutable()) apply(f);
                meshCacheValid_ = false;
                sceneDirty_ = true;
            };

            for (int pi = 0; pi < static_cast<int>(IM_ARRAYSIZE(uvProjNames)); ++pi)
            {
                if (pi > 0) ImGui::SameLine();
                if (ImGui::Button(uvProjNames[pi]))
                    applyProjection(static_cast<UvProjection>(pi));
            }

            if (ImGui::Button("Reset UV"))
            {
                applyProjection(UvProjection::Box);
            }

            if (uvPerFace)
            {
                if (ImGui::Button("Invert Face"))
                {
                    PushUndoState();
                    int invertedCount = 0;
                    for (int faceIndex : selectedFaceIndices_)
                    {
                        if (faceIndex >= 0 && faceIndex < static_cast<int>(meshObject.mesh.faceCount()))
                        {
                            EditableFace& face = meshObject.mesh.facesMutable()[(size_t)faceIndex];
                            if (face.indices.size() >= 3)
                            {
                                std::reverse(face.indices.begin(), face.indices.end());
                                ++invertedCount;
                            }
                        }
                    }
                    if (invertedCount > 0)
                    {
                        meshCacheValid_ = false;
                        sceneDirty_ = true;
                        sceneStatusMessage_ = (invertedCount == 1)
                            ? "Inverted selected face"
                            : ("Inverted " + std::to_string(invertedCount) + " faces");
                    }
                }
            }

            ImGui::PopID();
        }

        if (Section("CSG"))
        {
            ImGui::PushID("CSGSection");

            if (selectedMeshIndices_.size() >= 2)
            {
                const int cutterIndex = selectedMeshIndices_[0] == selectedMeshIndex_
                    ? selectedMeshIndices_[1]
                    : selectedMeshIndices_[0];
                if (cutterIndex >= 0 && cutterIndex < static_cast<int>(scene_.meshObjects().size()))
                {
                    ImGui::TextDisabled("Primary = target, secondary = cutter");

                    if (ImGui::Button("Subtract"))
                    {
                        PushUndoState();

                        std::vector<LevelMeshObject>& meshObjects = scene_.meshObjects();
                        const LevelMeshObject targetCopy = meshObjects[(size_t)selectedMeshIndex_];
                        const LevelMeshObject cutterCopy = meshObjects[(size_t)cutterIndex];

                        const glm::mat4 cutterWorld = meshObjectModelMatrix(cutterCopy);
                        const glm::mat4 targetInverse = glm::inverse(meshObjectModelMatrix(targetCopy));
                        const EditableMesh cutterLocal = transformedEditableMesh(cutterCopy.mesh, targetInverse * cutterWorld);

                        const std::vector<EditableMesh> pieces = subtractEditableMeshesConvex(targetCopy.mesh, cutterLocal);

                        if (!pieces.empty())
                        {
                            meshObjects[(size_t)selectedMeshIndex_].mesh = pieces[0];

                            for (std::size_t i = 1; i < pieces.size(); ++i)
                            {
                                LevelMeshObject pieceObject = targetCopy;
                                pieceObject.name = targetCopy.name + " part " + std::to_string(i + 1);
                                pieceObject.mesh = pieces[i];
                                meshObjects.insert(meshObjects.begin() + selectedMeshIndex_ + static_cast<int>(i), pieceObject);
                            }

                            int removeCutterIdx = cutterIndex;
                            if (cutterIndex > selectedMeshIndex_)
                                removeCutterIdx += static_cast<int>(pieces.size()) - 1;
                            meshObjects.erase(meshObjects.begin() + removeCutterIdx);

                            if (cutterIndex < selectedMeshIndex_)
                                selectedMeshIndex_--;

                            selectedFaceIndex_ = -1;
                            selectedFaceIndices_.clear();
                            selectedVertexIndices_.clear();
                            sceneDirty_ = true;
                            SyncSelectedMeshes();
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Intersect"))
                    {
                        PushUndoState();

                        std::vector<LevelMeshObject>& meshObjects = scene_.meshObjects();
                        const LevelMeshObject targetCopy = meshObjects[(size_t)selectedMeshIndex_];
                        const LevelMeshObject cutterCopy = meshObjects[(size_t)cutterIndex];

                        const glm::mat4 cutterWorld = meshObjectModelMatrix(cutterCopy);
                        const glm::mat4 targetInverse = glm::inverse(meshObjectModelMatrix(targetCopy));
                        const EditableMesh cutterLocal = transformedEditableMesh(cutterCopy.mesh, targetInverse * cutterWorld);

                        const EditableMesh result = intersectEditableMeshesConvex(targetCopy.mesh, cutterLocal);

                        if (result.faceCount() > 0)
                        {
                            meshObjects[(size_t)selectedMeshIndex_].mesh = result;

                            meshObjects.erase(meshObjects.begin() + cutterIndex);
                            if (cutterIndex < selectedMeshIndex_)
                                selectedMeshIndex_--;

                            selectedFaceIndex_ = -1;
                            selectedFaceIndices_.clear();
                            selectedVertexIndices_.clear();
                            sceneDirty_ = true;
                            SyncSelectedMeshes();
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Add"))
                    {
                        PushUndoState();

                        std::vector<LevelMeshObject>& meshObjects = scene_.meshObjects();
                        const LevelMeshObject targetCopy = meshObjects[(size_t)selectedMeshIndex_];
                        const LevelMeshObject cutterCopy = meshObjects[(size_t)cutterIndex];

                        const glm::mat4 cutterWorld = meshObjectModelMatrix(cutterCopy);
                        const glm::mat4 targetInverse = glm::inverse(meshObjectModelMatrix(targetCopy));
                        const EditableMesh cutterLocal = transformedEditableMesh(cutterCopy.mesh, targetInverse * cutterWorld);

                        const std::vector<EditableMesh> outsidePieces = subtractEditableMeshesConvex(targetCopy.mesh, cutterLocal);

                        std::vector<EditableVertex> mergedVerts;
                        std::vector<EditableFace> mergedFaces;

                        for (const EditableMesh& piece : outsidePieces)
                        {
                            const int vertOffset = static_cast<int>(mergedVerts.size());
                            for (const EditableVertex& v : piece.vertices())
                                mergedVerts.push_back(v);
                            for (const EditableFace& f : piece.faces())
                            {
                                EditableFace nf;
                                nf.materialName = f.materialName;
                                for (int idx : f.indices)
                                    nf.indices.push_back(idx + vertOffset);
                                mergedFaces.push_back(nf);
                            }
                        }

                        {
                            const int vertOffset = static_cast<int>(mergedVerts.size());
                            for (const EditableVertex& v : cutterLocal.vertices())
                                mergedVerts.push_back(v);
                            for (const EditableFace& f : cutterLocal.faces())
                            {
                                EditableFace nf;
                                nf.materialName = f.materialName;
                                for (int idx : f.indices)
                                    nf.indices.push_back(idx + vertOffset);
                                mergedFaces.push_back(nf);
                            }
                        }

                        if (!mergedFaces.empty())
                        {
                            meshObjects[(size_t)selectedMeshIndex_].mesh.setData(mergedVerts, mergedFaces);

                            int removeCutterIdx = cutterIndex;
                            if (cutterIndex > selectedMeshIndex_)
                                removeCutterIdx = cutterIndex;
                            meshObjects.erase(meshObjects.begin() + removeCutterIdx);
                            if (cutterIndex < selectedMeshIndex_)
                                selectedMeshIndex_--;

                            selectedFaceIndex_ = -1;
                            selectedFaceIndices_.clear();
                            selectedVertexIndices_.clear();
                            sceneDirty_ = true;
                            SyncSelectedMeshes();
                        }
                    }

                    if (Section("Experimental Mesh CSG", false))
                    {
                        if (ImGui::Button("Union Root CSG"))
                        {
                            PushUndoState();

                            LevelMeshObject& targetObject = scene_.meshObjects()[selectedMeshIndex_];
                            const LevelMeshObject& cutterObject = scene_.meshObjects()[cutterIndex];
                            Mesh targetMesh;
                            Mesh cutterMesh;
                            fillRenderMeshFromEditable(targetObject.mesh, targetMesh);
                            fillRenderMeshFromEditable(cutterObject.mesh, cutterMesh);
                            Mesh* resultMesh = CSG::makeUnion(targetMesh, cutterMesh,
                                                              meshObjectModelMatrix(targetObject),
                                                              meshObjectModelMatrix(cutterObject));

                            if (resultMesh)
                            {
                                const glm::mat4 inverseTarget = glm::inverse(meshObjectModelMatrix(targetObject));
                                targetObject.mesh = makeEditableFromRenderMesh(*resultMesh, inverseTarget);
                                delete resultMesh;
                                selectedFaceIndex_ = -1;
                                selectedFaceIndices_.clear();
                                selectedVertexIndices_.clear();
                                sceneDirty_ = true;
                            }
                        }

                        ImGui::SameLine();
                        if (ImGui::Button("Difference Root CSG"))
                        {
                            PushUndoState();

                            LevelMeshObject& targetObject = scene_.meshObjects()[selectedMeshIndex_];
                            const LevelMeshObject& cutterObject = scene_.meshObjects()[cutterIndex];
                            Mesh targetMesh;
                            Mesh cutterMesh;
                            fillRenderMeshFromEditable(targetObject.mesh, targetMesh);
                            fillRenderMeshFromEditable(cutterObject.mesh, cutterMesh);
                            Mesh* resultMesh = CSG::makeDifference(targetMesh, cutterMesh,
                                                                   meshObjectModelMatrix(targetObject),
                                                                   meshObjectModelMatrix(cutterObject));

                            if (resultMesh)
                            {
                                const glm::mat4 inverseTarget = glm::inverse(meshObjectModelMatrix(targetObject));
                                targetObject.mesh = makeEditableFromRenderMesh(*resultMesh, inverseTarget);
                                delete resultMesh;
                                selectedFaceIndex_ = -1;
                                selectedFaceIndices_.clear();
                                selectedVertexIndices_.clear();
                                sceneDirty_ = true;
                            }
                        }

                        ImGui::SameLine();
                        if (ImGui::Button("Intersect Root CSG"))
                        {
                            PushUndoState();

                            LevelMeshObject& targetObject = scene_.meshObjects()[selectedMeshIndex_];
                            const LevelMeshObject& cutterObject = scene_.meshObjects()[cutterIndex];
                            Mesh targetMesh;
                            Mesh cutterMesh;
                            fillRenderMeshFromEditable(targetObject.mesh, targetMesh);
                            fillRenderMeshFromEditable(cutterObject.mesh, cutterMesh);
                            Mesh* resultMesh = CSG::makeIntersection(targetMesh, cutterMesh,
                                                                     meshObjectModelMatrix(targetObject),
                                                                     meshObjectModelMatrix(cutterObject));

                            if (resultMesh)
                            {
                                const glm::mat4 inverseTarget = glm::inverse(meshObjectModelMatrix(targetObject));
                                targetObject.mesh = makeEditableFromRenderMesh(*resultMesh, inverseTarget);
                                delete resultMesh;
                                selectedFaceIndex_ = -1;
                                selectedFaceIndices_.clear();
                                selectedVertexIndices_.clear();
                                sceneDirty_ = true;
                            }
                        }
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("Select 2+ meshes for CSG");
            }

            if (Section("Plane Clip", false))
            {
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::BeginCombo("Clip Axis##CSGClipAxis", csgAxisName(csgClipAxis_)))
                {
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const bool selectedAxis = axis == csgClipAxis_;
                        if (ImGui::Selectable(csgAxisName(axis), selectedAxis))
                            csgClipAxis_ = axis;
                        if (selectedAxis)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::DragFloat("Clip Offset##CSGClipOffset", &csgClipOffset_, 0.5f, -4096.0f, 4096.0f);
                ImGui::Checkbox("Keep Front##CSGKeepFront", &csgClipKeepFront_);
                if (ImGui::Button("Apply Plane Clip"))
                {
                    glm::vec3 normal(0.0f);
                    normal[(size_t)glm::clamp(csgClipAxis_, 0, 2)] = 1.0f;
                    glm::vec3 point(0.0f);
                    point[(size_t)glm::clamp(csgClipAxis_, 0, 2)] = csgClipOffset_;

                    const EditableMesh clipped = clipEditableMeshAgainstPlane(
                        meshObject.mesh,
                        MeshPlane::FromPointNormal(point, normal),
                        csgClipKeepFront_);

                    if (clipped.faceCount() > 0)
                    {
                        PushUndoState();
                        meshObject.mesh = clipped;
                        selectedFaceIndex_ = -1;
                        selectedFaceIndices_.clear();
                        selectedVertexIndices_.clear();
                        sceneDirty_ = true;
                    }
                }
            }

            ImGui::PopID();
        }
    }

    // Reference Image Planes
    if (Section("Reference Planes", false))
    {
        static const char* refAxisNames[] = {"Front", "Back", "Left", "Right", "Top", "Bottom"};

        for (int rp = 0; rp < static_cast<int>(referencePlanes_.size()); ++rp)
        {
            auto& plane = referencePlanes_[(size_t)rp];
            ImGui::PushID(rp + 5000);
            const std::string label = std::string(refAxisNames[static_cast<int>(plane.axis)]) + " ##refplane";
            bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            if (ImGui::SmallButton("X"))
            {
                if (!plane.textureName.empty())
                    TextureManager::instance().unload(plane.textureName);
                referencePlanes_.erase(referencePlanes_.begin() + rp);
                if (open) ImGui::TreePop();
                ImGui::PopID();
                --rp;
                continue;
            }
            if (open)
            {
                ImGui::Checkbox("Visible", &plane.visible);
                ImGui::SetNextItemWidth(80.0f);
                int axisIdx = static_cast<int>(plane.axis);
                if (ImGui::Combo("Axis", &axisIdx, refAxisNames, 6))
                    plane.axis = static_cast<RefPlaneAxis>(axisIdx);
                ImGui::DragFloat("Offset", &plane.offset, 1.0f, -4096.0f, 4096.0f);
                ImGui::DragFloat("Scale", &plane.scale, 1.0f, 1.0f, 8192.0f);
                ImGui::SliderFloat("Opacity", &plane.opacity, 0.0f, 1.0f);
                if (!plane.imagePath.empty())
                    ImGui::TextWrapped("Image: %s", plane.imagePath.c_str());
                if (ImGui::Button("Load Image"))
                {
                    refPlaneDialogTarget_ = rp;
                    const std::filesystem::path startDir = assetRoot_.empty()
                        ? std::filesystem::current_path()
                        : std::filesystem::path(assetRoot_);
                    refPlaneImageDialog_.Open(ImGuiFileDialog::Mode::OpenFile, startDir, "image");
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (ImGui::Button("Add Front")) { ReferencePlane p; p.axis = RefPlaneAxis::Front; referencePlanes_.push_back(p); }
        ImGui::SameLine();
        if (ImGui::Button("Add Back")) { ReferencePlane p; p.axis = RefPlaneAxis::Back; referencePlanes_.push_back(p); }
        ImGui::SameLine();
        if (ImGui::Button("Add Left")) { ReferencePlane p; p.axis = RefPlaneAxis::Left; referencePlanes_.push_back(p); }
        if (ImGui::Button("Add Right")) { ReferencePlane p; p.axis = RefPlaneAxis::Right; referencePlanes_.push_back(p); }
        ImGui::SameLine();
        if (ImGui::Button("Add Top")) { ReferencePlane p; p.axis = RefPlaneAxis::Top; referencePlanes_.push_back(p); }
        ImGui::SameLine();
        if (ImGui::Button("Add Bottom")) { ReferencePlane p; p.axis = RefPlaneAxis::Bottom; referencePlanes_.push_back(p); }
    }

    ImGui::End();
}

void LevelEditorApp::ShowCenterPanel()
{
    ImGui::SetNextWindowPos(centerPanelPos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(centerPanelSize_, ImGuiCond_Always);
    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Viewport", nullptr, panelFlags))
    {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Level Views");
    DrawViewportToolbar();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::BeginCombo("Layout##ViewLayout", (viewLayout_ == ViewLayout::One) ? "1 View" : (viewLayout_ == ViewLayout::Two) ? "2 Views" : (viewLayout_ == ViewLayout::Three) ? "3 Views" : "4 Views"))
    {
        if (ImGui::Selectable("1 View", viewLayout_ == ViewLayout::One))
            viewLayout_ = ViewLayout::One;
        if (ImGui::Selectable("2 Views", viewLayout_ == ViewLayout::Two))
            viewLayout_ = ViewLayout::Two;
        if (ImGui::Selectable("3 Views", viewLayout_ == ViewLayout::Three))
            viewLayout_ = ViewLayout::Three;
        if (ImGui::Selectable("4 Views", viewLayout_ == ViewLayout::Four))
            viewLayout_ = ViewLayout::Four;
        ImGui::EndCombo();
    }

    const ViewType allTypes[] = {
        ViewType::Top, ViewType::Bottom, ViewType::Front, ViewType::Back,
        ViewType::Left, ViewType::Right, ViewType::Perspective
    };
    for (int i = 0; i < activeViewCount_; ++i)
    {
        ImGui::SameLine();
        ImGui::PushID(i + 1000);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::BeginCombo("##ViewTypeCombo", viewTypeName(views_[i].type)))
        {
            for (ViewType type : allTypes)
            {
                const bool selected = views_[i].type == type;
                if (ImGui::Selectable(viewTypeName(type), selected))
                {
                    views_[i].type = type;
                    views_[i].label = viewTypeName(type);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 canvasSize(available.x, available.y - 8.0f);
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(canvasSize);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 viewportMin = canvasPos;
    const ImVec2 viewportMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    const bool viewportHovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::IsMouseHoveringRect(viewportMin, viewportMax);
    drawList->PushClipRect(viewportMin, viewportMax, true);

    LayoutViews(canvasPos, canvasSize);
    UpdateViewCameras();

    for (int i = 0; i < activeViewCount_; ++i)
        DrawViewTile(views_[i], drawList);
    DrawTransformGizmo();
    HandleViewportInput(viewportHovered);
    DrawViewportContextMenu();
    drawList->PopClipRect();
    ImGui::End();
}

void LevelEditorApp::ShowRightPanel()
{
    ImGui::SetNextWindowPos(rightPanelPos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(rightPanelSize_, ImGuiCond_Always);
    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Inspector", nullptr, panelFlags))
    {
        ImGui::End();
        return;
    }

    if (Section("Level Stats"))
    {
        std::size_t totalVertices = 0;
        std::size_t totalFaces = 0;
        for (const LevelMeshObject& object : scene_.meshObjects())
        {
            totalVertices += object.mesh.vertexCount();
            totalFaces += object.mesh.faceCount();
        }
        ImGui::Text("Mesh Objects: %d", static_cast<int>(scene_.meshObjects().size()));
        ImGui::Text("Selected Meshes: %d", static_cast<int>(selectedMeshIndices_.size()));
        ImGui::Text("Entities: %d", static_cast<int>(scene_.entities().size()));
        ImGui::Text("Total Vertices: %d", static_cast<int>(totalVertices));
        ImGui::Text("Total Faces: %d", static_cast<int>(totalFaces));
    }

    if (Section("Lightmap"))
    {
        const bool isBaking = bakeRunning_;
        if (isBaking) ImGui::BeginDisabled();
        ImGui::DragInt("Resolution", &lightmapSettings_.resolution, 16, 64, 8192);
        ImGui::DragInt("Samples/Texel", &lightmapSettings_.samplesPerTexel, 1, 1, 16);
        ImGui::DragFloat("Bias", &lightmapSettings_.bias, 0.01f, 0.001f, 10.0f);
        ImGui::DragFloat("Ambient", &lightmapSettings_.ambient, 0.01f, 0.0f, 1.0f);
        ImGui::Checkbox("Use Lightmap", &useLightmap_);
        if (isBaking) ImGui::EndDisabled();

        if (!isBaking)
        {
            if (ImGui::Button("Bake Lightmaps"))
                StartBakeAsync();
        }
        else
        {
            // Show progress bar
            const float prog = bakeProgress_.load();
            ImGui::ProgressBar(prog, ImVec2(-1, 0), "Baking...");
        }

        if (lightmapTexture_)
        {
            ImGui::Text("Lightmap: %dx%d", lightmapResult_.width, lightmapResult_.height);
            ImGui::Image((ImTextureID)(intptr_t)lightmapTexture_, ImVec2(128, 128));
        }
    }

    if (Section("View Settings"))
    {
        ImGui::Checkbox("Show Grid", &showGrid_);
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &snapEnabled_);

        // 2D grid/snap size presets
        ImGui::Text("2D Grid / Snap:");
        ImGui::SameLine();
        static const float gridPresets[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};
        for (int i = 0; i < 7; ++i)
        {
            if (i > 0) ImGui::SameLine();
            char label[16];
            snprintf(label, sizeof(label), "%g", gridPresets[i]);
            bool selected = (std::fabs(gridSize_ - gridPresets[i]) < 0.01f);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(label))
                gridSize_ = gridPresets[i];
            if (selected) ImGui::PopStyleColor();
        }
        ImGui::TextUnformatted("3D Grid Size");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##PerspGridSlider", &perspGridSize_, 1.0f, 128.0f, "%.1f");

        ImGui::Checkbox("Transparency", &useTransparency_);
        if (useTransparency_)
        {
            ImGui::TextUnformatted("Transparency Alpha");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##TransAlpha", &transparency_, 0.0f, 1.0f, "%.2f");
        }
        static const char* cullModeLabels[] = {"Off", "Front", "Back"};
        int cullModeIndex = static_cast<int>(cullMode_);
        ImGui::TextUnformatted("Cull Mode");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##CullMode", &cullModeIndex, cullModeLabels, IM_ARRAYSIZE(cullModeLabels)))
            cullMode_ = static_cast<CullMode>(cullModeIndex);
        ImGui::TextUnformatted("3D Zoom Min");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##PerspectiveMinDistance", &perspectiveMinDistance_, 1.0f, 0.01f, 1024.0f, "%.2f"))
            perspectiveMinDistance_ = std::max(0.01f, perspectiveMinDistance_);
        for (LevelEditorView& v : views_)
            if (v.type == ViewType::Perspective)
                v.perspectiveDistance = std::max(v.perspectiveDistance, perspectiveMinDistance_);
        ImGui::TextUnformatted("Perspective Near");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##PerspectiveNear", &perspectiveNearPlane_, 0.01f, 0.001f, 100.0f, "%.3f"))
            perspectiveNearPlane_ = std::max(0.001f, perspectiveNearPlane_);
        perspectiveFarPlane_ = std::max(perspectiveNearPlane_ + 1.0f, perspectiveFarPlane_);
        ImGui::TextUnformatted("Perspective Far");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::DragFloat("##PerspectiveFar", &perspectiveFarPlane_, 10.0f, perspectiveNearPlane_ + 1.0f, 200000.0f, "%.1f");
        perspectiveFarPlane_ = std::max(perspectiveNearPlane_ + 1.0f, perspectiveFarPlane_);

        ImGui::Checkbox("Face Fill Overlay", &faceHighlightFillEnabled_);
        ImGui::BeginDisabled(!faceHighlightFillEnabled_);
        ImGui::TextUnformatted("Face Fill Alpha");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##FaceFillAlpha", &faceHighlightFillAlpha_, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Per-View Background");
        for (int i = 0; i < 4; ++i)
        {
            ImGui::PushID(i);
            ImGui::ColorEdit3(views_[i].label, &views_[i].clearColor.r, ImGuiColorEditFlags_NoInputs);
            ImGui::PopID();
          //  if (i % 2 == 0) 
          ImGui::SameLine();
        }
    }
    
    ImGui::Separator();
    if (Section("Debug", false))
    {
        ImGui::Checkbox("Draw Normals", &debugDrawNormals_);
        ImGui::SameLine();
        ImGui::Checkbox("Draw Tangents", &debugDrawTangents_);
        if (debugDrawNormals_ || debugDrawTangents_)
            ImGui::DragFloat("Line Length", &debugNormalLength_, 0.5f, 1.0f, 100.0f);
    }

    if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        Section("Selected Mesh"))
    {
        ImGui::PushID("SelectedMeshSection");
        LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];
        if (selectedMeshIndices_.size() > 1)
            ImGui::Text("Primary selection: %d of %d", selectedMeshIndex_ + 1, static_cast<int>(selectedMeshIndices_.size()));
        std::string editedMeshName = meshObject.name;
        if (ImGui::InputText("Name##MeshName", &editedMeshName) && editedMeshName != meshObject.name)
        {
            PushUndoState();
            meshObject.name = editedMeshName;
        }
        glm::vec3 editedMeshPosition = meshObject.position;
        if (ImGui::DragFloat3("Position##MeshPosition", &editedMeshPosition.x, 1.0f) && editedMeshPosition != meshObject.position)
        {
            PushUndoState();
            meshObject.position = editedMeshPosition;
        }
        glm::vec3 editedMeshRotation = meshObject.rotationEuler;
        if (ImGui::DragFloat3("Rotation##MeshRotation", &editedMeshRotation.x, 0.5f, -360.0f, 360.0f) && editedMeshRotation != meshObject.rotationEuler)
        {
            PushUndoState();
            meshObject.rotationEuler = editedMeshRotation;
        }
        glm::vec3 editedMeshScale = meshObject.scale;
        if (ImGui::DragFloat3("Scale##MeshScale", &editedMeshScale.x, 0.01f, 0.01f, 128.0f) && editedMeshScale != meshObject.scale)
        {
            PushUndoState();
            meshObject.scale = editedMeshScale;
        }
        glm::vec3 editedMeshPivot = meshObject.pivot;
        if (ImGui::DragFloat3("Pivot##MeshPivot", &editedMeshPivot.x, 0.5f) && editedMeshPivot != meshObject.pivot)
        {
            PushUndoState();
            setMeshPivotPreserveWorld(meshObject, editedMeshPivot);
        }
        if (ImGui::Button("Pivot To Bounds Center"))
        {
            const glm::vec3 pivotCenter = editableMeshBoundsCenter(meshObject.mesh);
            if (pivotCenter != meshObject.pivot)
            {
                PushUndoState();
                setMeshPivotPreserveWorld(meshObject, pivotCenter);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Pivot To Bottom"))
        {
            const glm::vec3 pivotBottom = editableMeshBoundsBottomCenter(meshObject.mesh);
            if (pivotBottom != meshObject.pivot)
            {
                PushUndoState();
                setMeshPivotPreserveWorld(meshObject, pivotBottom);
            }
        }
        if (ImGui::Button("Reset Pivot"))
        {
            if (meshObject.pivot != glm::vec3(0.0f))
            {
                PushUndoState();
                setMeshPivotPreserveWorld(meshObject, glm::vec3(0.0f));
            }
        }
        if (ImGui::Button("Bake Pivot Into Vertices"))
        {
            if (meshObject.pivot != glm::vec3(0.0f))
            {
                PushUndoState();
                bakeMeshPivotIntoVertices(meshObject);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Bake Rotation/Scale"))
        {
            const glm::vec3 normalizedRotation = normalizeEulerDegrees(meshObject.rotationEuler);
            if (!nearlyEqualVec3(normalizedRotation, glm::vec3(0.0f)) || !nearlyEqualVec3(meshObject.scale, glm::vec3(1.0f)))
            {
                PushUndoState();
                bakeMeshRotationScaleIntoVertices(meshObject);
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Transform Vertices");
        ImGui::DragFloat3("Move##VertexBakeMove", &vertexBakeTranslate_.x, 0.5f);
        ImGui::DragFloat3("Rotate##VertexBakeRotate", &vertexBakeRotate_.x, 0.5f, -360.0f, 360.0f);
        ImGui::DragFloat3("Scale##VertexBakeScale", &vertexBakeScale_.x, 0.01f, 0.01f, 128.0f);
        if (ImGui::Button("Apply To Vertices"))
        {
            const glm::vec3 normalizedBakeRotation = normalizeEulerDegrees(vertexBakeRotate_);
            if (!nearlyEqualVec3(vertexBakeTranslate_, glm::vec3(0.0f)) ||
                !nearlyEqualVec3(normalizedBakeRotation, glm::vec3(0.0f)) ||
                !nearlyEqualVec3(vertexBakeScale_, glm::vec3(1.0f)))
            {
                PushUndoState();
                bakeMeshVertexTransform(meshObject, vertexBakeTranslate_, vertexBakeRotate_, vertexBakeScale_);
                vertexBakeTranslate_ = glm::vec3(0.0f);
                vertexBakeRotate_ = glm::vec3(0.0f);
                vertexBakeScale_ = glm::vec3(1.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Fields"))
        {
            vertexBakeTranslate_ = glm::vec3(0.0f);
            vertexBakeRotate_ = glm::vec3(0.0f);
            vertexBakeScale_ = glm::vec3(1.0f);
        }
        //ImGui::TextDisabled("Pivot agora desloca sem mexer no visual. Este painel transforma a mesh local nos vertices.");

        int terrainCols = 0;
        int terrainRows = 0;
        const bool isTerrainMesh = SelectedMeshIsTerrain(&terrainCols, &terrainRows);
        if (isTerrainMesh)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Terrain Sculpt");
            ImGui::Checkbox("Enable Sculpt##Terrain", &terrainSculptEnabled_);
            static const char* sculptModeLabels[] = {"Raise", "Lower", "Smooth", "Flatten"};
            int sculptModeIndex = static_cast<int>(terrainSculptMode_);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::Combo("##TerrainSculptMode", &sculptModeIndex, sculptModeLabels, IM_ARRAYSIZE(sculptModeLabels)))
                terrainSculptMode_ = static_cast<TerrainSculptMode>(sculptModeIndex);
            ImGui::TextUnformatted("Brush Radius");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::DragFloat("##TerrainBrushRadius", &terrainBrushRadius_, 1.0f, 1.0f, 1024.0f, "%.1f");
            terrainBrushRadius_ = std::max(1.0f, terrainBrushRadius_);
            ImGui::TextUnformatted("Brush Strength");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::DragFloat("##TerrainBrushStrength", &terrainBrushStrength_, 0.25f, 0.1f, 128.0f, "%.2f");
            terrainBrushStrength_ = std::max(0.1f, terrainBrushStrength_);
            if (terrainSculptMode_ == TerrainSculptMode::Flatten)
            {
                ImGui::TextUnformatted("Target Height");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::DragFloat("##TerrainFlattenHeight", &terrainFlattenHeight_, 0.5f, -4096.0f, 4096.0f, "%.2f");
            }
            ImGui::TextDisabled("Use in Perspective or Top view.");
            ImGui::TextDisabled("Grid: %d x %d", terrainCols, terrainRows);
        }

        std::string meshPrimitiveLabel = meshPrimitiveName(meshObject.primitive);
        ImGui::TextUnformatted("Primitive Type");
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##MeshPrimitiveType", &meshPrimitiveLabel, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();

        ImGui::Text("Vertices: %d  Faces: %d",
            static_cast<int>(meshObject.mesh.vertexCount()),
            static_cast<int>(meshObject.mesh.faceCount()));

        if (Section("Faces", false))
        {
            const float faceListHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.4f, 160.0f, 320.0f);
            if (ImGui::BeginChild("FacesListPanel##Mesh", ImVec2(0.0f, faceListHeight), true, ImGuiWindowFlags_None))
            {
                if (ImGui::BeginTable("FacesTable##Mesh", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
                {
                    ImGui::TableSetupColumn("Face");
                    ImGui::TableSetupColumn("Material");
                    ImGui::TableHeadersRow();
                    const auto& faces = meshObject.mesh.faces();
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(faces.size()));
                    while (clipper.Step())
                    {
                        for (int rowFaceIndex = clipper.DisplayStart; rowFaceIndex < clipper.DisplayEnd; ++rowFaceIndex)
                        {
                            const EditableFace& face = faces[(size_t)rowFaceIndex];
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            const bool rowSelected = IsFaceSelected(rowFaceIndex);
                            if (ImGui::Selectable(("Face " + std::to_string(rowFaceIndex)).c_str(), rowSelected, ImGuiSelectableFlags_SpanAllColumns))
                            {
                                if (ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl)
                                {
                                    toggleIndexSelection(selectedFaceIndices_, rowFaceIndex);
                                    selectedFaceIndex_ = selectedFaceIndices_.empty() ? -1 : rowFaceIndex;
                                    SyncSelectedFaces();
                                }
                                else
                                {
                                    SetSingleSelectedFace(rowFaceIndex);
                                }
                                selectionMode_ = SelectionMode::Face;
                            }
                            ImGui::TableSetColumnIndex(1);
                            const std::string faceMaterialLabel = face.materialName.empty() ? "(none)" : PathFilename(face.materialName);
                            ImGui::TextUnformatted(faceMaterialLabel.c_str());
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }
        }
        ImGui::PopID();
    }

    if (selectedEntityIndex_ >= 0 && selectedEntityIndex_ < static_cast<int>(scene_.entities().size()) &&
        Section("Selected Entity"))
    {
        ImGui::PushID("SelectedEntitySection");
        LevelEntityObject& entity = scene_.entities()[selectedEntityIndex_];
        std::string editedEntityName = entity.name;
        if (ImGui::InputText("Name##EntityName", &editedEntityName) && editedEntityName != entity.name)
        {
            PushUndoState();
            entity.name = editedEntityName;
        }
        ImGui::Text("Type: %s", entityTypeName(entity.type));
        glm::vec3 editedPosition = entity.position;
        if (ImGui::DragFloat3("Position##EntityPosition", &editedPosition.x, 1.0f) && editedPosition != entity.position)
        {
            PushUndoState();
            entity.position = editedPosition;
        }

        // Light-specific properties
        if (entity.type == LevelEntityType::Light)
        {
            // Light type combo
            static const char* lightTypeNames[] = { "Point", "Directional", "Spot" };
            int lt = static_cast<int>(entity.lightType);
            if (ImGui::Combo("Light Type", &lt, lightTypeNames, 3))
            {
                PushUndoState();
                entity.lightType = static_cast<LightType>(lt);
            }

            glm::vec3 editedColor = entity.color;
            if (ImGui::ColorEdit3("Color##LightColor", &editedColor.x) && editedColor != entity.color)
            {
                PushUndoState();
                entity.color = editedColor;
            }
            float editedIntensity = entity.intensity;
            if (ImGui::DragFloat("Intensity##LightIntensity", &editedIntensity, 0.1f, 0.0f, 100.0f) && editedIntensity != entity.intensity)
            {
                PushUndoState();
                entity.intensity = editedIntensity;
            }

            // Point/Spot have radius
            if (entity.lightType != LightType::Directional)
            {
                float editedRadius = entity.radius;
                if (ImGui::DragFloat("Radius##LightRadius", &editedRadius, 1.0f, 0.0f, 10000.0f) && editedRadius != entity.radius)
                {
                    PushUndoState();
                    entity.radius = editedRadius;
                }
            }

            // Directional/Spot have direction
            if (entity.lightType == LightType::Directional || entity.lightType == LightType::Spot)
            {
                glm::vec3 dir = entity.direction;
                if (ImGui::DragFloat3("Direction##LightDir", &dir.x, 0.01f, -1.0f, 1.0f))
                {
                    if (glm::length(dir) > 1e-4f)
                    {
                        PushUndoState();
                        entity.direction = glm::normalize(dir);
                    }
                }
            }

            // Spot-specific
            if (entity.lightType == LightType::Spot)
            {
                float angle = entity.spotAngle;
                if (ImGui::DragFloat("Cone Angle##SpotAngle", &angle, 0.5f, 1.0f, 89.0f) && angle != entity.spotAngle)
                {
                    PushUndoState();
                    entity.spotAngle = angle;
                }
                float soft = entity.spotSoftness;
                if (ImGui::DragFloat("Softness##SpotSoft", &soft, 0.01f, 0.0f, 1.0f) && soft != entity.spotSoftness)
                {
                    PushUndoState();
                    entity.spotSoftness = soft;
                }
            }
        }

        // PlayerStart direction
        if (entity.type == LevelEntityType::PlayerStart)
        {
            glm::vec3 dir = entity.direction;
            if (ImGui::DragFloat3("Direction##PlayerDir", &dir.x, 0.01f, -1.0f, 1.0f))
            {
                if (glm::length(dir) > 1e-4f)
                {
                    PushUndoState();
                    entity.direction = glm::normalize(dir);
                }
            }
        }

        // Door properties
        if (entity.type == LevelEntityType::Door)
        {
            static const char* doorTypeNames[] = {"Slide", "Turn", "Shutter"};
            int dt = static_cast<int>(entity.doorType);
            if (ImGui::Combo("Door Type", &dt, doorTypeNames, 3))
            {
                PushUndoState();
                entity.doorType = static_cast<DoorType>(dt);
            }

            if (entity.doorType == DoorType::Slide || entity.doorType == DoorType::Shutter)
            {
                glm::vec3 dir = entity.direction;
                if (ImGui::DragFloat3("Slide Direction", &dir.x, 0.01f, -1.0f, 1.0f))
                {
                    if (glm::length(dir) > 1e-4f)
                    {
                        PushUndoState();
                        entity.direction = glm::normalize(dir);
                    }
                }
                // Quick direction presets
                if (ImGui::Button("Left")) { PushUndoState(); entity.direction = glm::vec3(-1, 0, 0); }
                ImGui::SameLine();
                if (ImGui::Button("Right")) { PushUndoState(); entity.direction = glm::vec3(1, 0, 0); }
                ImGui::SameLine();
                if (ImGui::Button("Up")) { PushUndoState(); entity.direction = glm::vec3(0, 1, 0); }
                ImGui::SameLine();
                if (ImGui::Button("Down")) { PushUndoState(); entity.direction = glm::vec3(0, -1, 0); }
            }

            const char* distLabel = (entity.doorType == DoorType::Turn) ? "Angle (degrees)" : "Distance";
            float dist = entity.doorDistance;
            if (ImGui::DragFloat(distLabel, &dist, 1.0f, 0.0f, 1000.0f) && dist != entity.doorDistance)
            {
                PushUndoState();
                entity.doorDistance = dist;
            }

            float spd = entity.doorSpeed;
            if (ImGui::DragFloat("Speed##Door", &spd, 1.0f, 0.1f, 1000.0f) && spd != entity.doorSpeed)
            {
                PushUndoState();
                entity.doorSpeed = spd;
            }

            bool open = entity.doorStartOpen;
            if (ImGui::Checkbox("Start Open", &open))
            {
                PushUndoState();
                entity.doorStartOpen = open;
            }

            // Link to mesh
            const char* meshPreview = (entity.linkedMeshIndex >= 0 &&
                entity.linkedMeshIndex < static_cast<int>(scene_.meshObjects().size()))
                ? scene_.meshObjects()[(size_t)entity.linkedMeshIndex].name.c_str()
                : "(none)";
            if (ImGui::BeginCombo("Linked Mesh", meshPreview))
            {
                if (ImGui::Selectable("(none)", entity.linkedMeshIndex < 0))
                {
                    PushUndoState();
                    entity.linkedMeshIndex = -1;
                }
                for (int mi = 0; mi < static_cast<int>(scene_.meshObjects().size()); ++mi)
                {
                    const bool sel = (mi == entity.linkedMeshIndex);
                    if (ImGui::Selectable(scene_.meshObjects()[(size_t)mi].name.c_str(), sel))
                    {
                        PushUndoState();
                        entity.linkedMeshIndex = mi;
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Elevator properties
        if (entity.type == LevelEntityType::Elevator)
        {
            glm::vec3 endPos = entity.endPosition;
            if (ImGui::DragFloat3("End Position", &endPos.x, 1.0f) && endPos != entity.endPosition)
            {
                PushUndoState();
                entity.endPosition = endPos;
            }

            float spd = entity.moveSpeed;
            if (ImGui::DragFloat("Speed##Elev", &spd, 1.0f, 0.1f, 1000.0f) && spd != entity.moveSpeed)
            {
                PushUndoState();
                entity.moveSpeed = spd;
            }

            float wt = entity.waitTime;
            if (ImGui::DragFloat("Wait Time (s)", &wt, 0.1f, 0.0f, 60.0f) && wt != entity.waitTime)
            {
                PushUndoState();
                entity.waitTime = wt;
            }

            // Link to mesh
            const char* meshPreview = (entity.linkedMeshIndex >= 0 &&
                entity.linkedMeshIndex < static_cast<int>(scene_.meshObjects().size()))
                ? scene_.meshObjects()[(size_t)entity.linkedMeshIndex].name.c_str()
                : "(none)";
            if (ImGui::BeginCombo("Linked Mesh##Elev", meshPreview))
            {
                if (ImGui::Selectable("(none)##Elev", entity.linkedMeshIndex < 0))
                {
                    PushUndoState();
                    entity.linkedMeshIndex = -1;
                }
                for (int mi = 0; mi < static_cast<int>(scene_.meshObjects().size()); ++mi)
                {
                    const bool sel = (mi == entity.linkedMeshIndex);
                    if (ImGui::Selectable(scene_.meshObjects()[(size_t)mi].name.c_str(), sel))
                    {
                        PushUndoState();
                        entity.linkedMeshIndex = mi;
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Platform properties
        if (entity.type == LevelEntityType::Platform)
        {
            glm::vec3 endPos = entity.endPosition;
            if (ImGui::DragFloat3("End Position##Plat", &endPos.x, 1.0f) && endPos != entity.endPosition)
            {
                PushUndoState();
                entity.endPosition = endPos;
            }

            float spd = entity.moveSpeed;
            if (ImGui::DragFloat("Speed##Plat", &spd, 1.0f, 0.1f, 1000.0f) && spd != entity.moveSpeed)
            {
                PushUndoState();
                entity.moveSpeed = spd;
            }

            float wt = entity.waitTime;
            if (ImGui::DragFloat("Wait Time##Plat", &wt, 0.1f, 0.0f, 60.0f) && wt != entity.waitTime)
            {
                PushUndoState();
                entity.waitTime = wt;
            }

            // Link to mesh
            const char* meshPreview = (entity.linkedMeshIndex >= 0 &&
                entity.linkedMeshIndex < static_cast<int>(scene_.meshObjects().size()))
                ? scene_.meshObjects()[(size_t)entity.linkedMeshIndex].name.c_str()
                : "(none)";
            if (ImGui::BeginCombo("Linked Mesh##Plat", meshPreview))
            {
                if (ImGui::Selectable("(none)##Plat", entity.linkedMeshIndex < 0))
                {
                    PushUndoState();
                    entity.linkedMeshIndex = -1;
                }
                for (int mi = 0; mi < static_cast<int>(scene_.meshObjects().size()); ++mi)
                {
                    const bool sel = (mi == entity.linkedMeshIndex);
                    if (ImGui::Selectable(scene_.meshObjects()[(size_t)mi].name.c_str(), sel))
                    {
                        PushUndoState();
                        entity.linkedMeshIndex = mi;
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Placement properties
        if (entity.type == LevelEntityType::Placement)
        {
            int it = entity.itemType;
            if (ImGui::InputInt("Item Type", &it) && it != entity.itemType)
            {
                PushUndoState();
                entity.itemType = it;
            }

            float ry = entity.rotationY;
            if (ImGui::DragFloat("Rotation Y", &ry, 1.0f, -360.0f, 360.0f) && ry != entity.rotationY)
            {
                PushUndoState();
                entity.rotationY = ry;
            }
        }

        // Preview button for Door / Elevator / Platform
        if ((entity.type == LevelEntityType::Door ||
             entity.type == LevelEntityType::Elevator ||
             entity.type == LevelEntityType::Platform) &&
            entity.linkedMeshIndex >= 0 &&
            entity.linkedMeshIndex < static_cast<int>(scene_.meshObjects().size()))
        {
            ImGui::Separator();
            const bool thisPreview = (entityPreviewActive_ && entityPreviewIndex_ == selectedEntityIndex_);
            if (thisPreview)
            {
                if (ImGui::Button("Stop Preview"))
                {
                    // Restore original mesh position/rotation
                    LevelMeshObject& mesh = scene_.meshObjects()[(size_t)entity.linkedMeshIndex];
                    mesh.position = entityPreviewOrigPos_;
                    mesh.rotationEuler = entityPreviewOrigRot_;
                    entityPreviewActive_ = false;
                    entityPreviewIndex_ = -1;
                    entityPreviewTime_ = 0.0f;
                    sceneDirty_ = true;
                }
                // Show progress bar
                ImGui::SameLine();
                ImGui::ProgressBar(entityPreviewTime_, ImVec2(100, 0));
            }
            else
            {
                if (ImGui::Button("Preview"))
                {
                    // Stop any existing preview first
                    if (entityPreviewActive_ && entityPreviewIndex_ >= 0 &&
                        entityPreviewIndex_ < static_cast<int>(scene_.entities().size()))
                    {
                        const auto& prevEnt = scene_.entities()[(size_t)entityPreviewIndex_];
                        if (prevEnt.linkedMeshIndex >= 0 && prevEnt.linkedMeshIndex < static_cast<int>(scene_.meshObjects().size()))
                        {
                            scene_.meshObjects()[(size_t)prevEnt.linkedMeshIndex].position = entityPreviewOrigPos_;
                            scene_.meshObjects()[(size_t)prevEnt.linkedMeshIndex].rotationEuler = entityPreviewOrigRot_;
                        }
                    }
                    // Start new preview
                    entityPreviewActive_ = true;
                    entityPreviewIndex_ = selectedEntityIndex_;
                    entityPreviewTime_ = 0.0f;
                    entityPreviewForward_ = true;
                    entityPreviewOrigPos_ = scene_.meshObjects()[(size_t)entity.linkedMeshIndex].position;
                    entityPreviewOrigRot_ = scene_.meshObjects()[(size_t)entity.linkedMeshIndex].rotationEuler;
                }
            }
        }

        ImGui::PopID();
    }

    if (Section("Texture"))
    {
        TextureManager& textureManager = TextureManager::instance();
        Texture* white = textureManager.getWhite();
        Texture* texture = white;
        if (!currentTexturePath_.empty())
        {
            texture = textureManager.get(currentTexturePath_);
            if (!texture)
            {
                const std::string texName = "level_current_texture::" + currentTexturePath_;
                texture = textureManager.get(texName);
                if (!texture && failedTextureLoads_.find(texName) == failedTextureLoads_.end())
                {
                    const std::string baseDir = assetRoot_.empty() ? "assets" : assetRoot_;
                    std::string resolved = ResolveTexturePath(baseDir, currentTexturePath_);
                    if (resolved.empty())
                        resolved = ResolveTexturePath(baseDir, PathFilename(currentTexturePath_));
                    if (!resolved.empty())
                        texture = textureManager.load(texName, resolved);
                    if (!texture)
                        failedTextureLoads_.insert(texName);
                }
            }
            if (!texture)
                texture = white;
        }

        ImGui::Text("Selected:");
        ImGui::SameLine();
        const std::string selectedTextureLabel = currentTexturePath_.empty() ? "(none)" : PathFilename(currentTexturePath_);
        ImGui::TextUnformatted(selectedTextureLabel.c_str());
        ImGui::Image((ImTextureID)(intptr_t)texture->id, ImVec2(140.0f, 140.0f));

        // Apply texture to selected face / object
        if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()))
        {
            LevelMeshObject& obj = scene_.meshObjects()[(size_t)selectedMeshIndex_];
            const bool hasFace = selectionMode_ == SelectionMode::Face &&
                                 selectedFaceIndex_ >= 0 &&
                                 selectedFaceIndex_ < static_cast<int>(obj.mesh.faceCount());

            ImGui::BeginDisabled(currentTexturePath_.empty());
            if (hasFace)
            {
                if (ImGui::Button("Apply to Face"))
                {
                    PushUndoState();
                    for (int faceIndex : selectedFaceIndices_)
                    {
                        if (faceIndex >= 0 && faceIndex < static_cast<int>(obj.mesh.faceCount()))
                            obj.mesh.facesMutable()[(size_t)faceIndex].materialName = currentTexturePath_;
                    }
                    failedTextureLoads_.clear();
                    meshCacheValid_ = false;
                    sceneDirty_ = true;
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Apply to Object"))
            {
                PushUndoState();
                for (EditableFace& f : obj.mesh.facesMutable())
                    f.materialName = currentTexturePath_;
                failedTextureLoads_.clear();
                meshCacheValid_ = false;
                sceneDirty_ = true;
            }
            ImGui::EndDisabled();

            // Live UV editing — directly modify the selected face's UV params
            ImGui::Separator();
            if (hasFace)
            {
                EditableFace& face = obj.mesh.facesMutable()[(size_t)selectedFaceIndex_];
                const ImGuiIO& io = ImGui::GetIO();
                const float uvMoveStep = io.KeyShift ? 0.001f : (io.KeyCtrl ? 0.1f : 0.01f);
                const float uvScaleStep = io.KeyShift ? 0.001f : (io.KeyCtrl ? 0.1f : 0.01f);
                const float uvRotateStep = io.KeyShift ? 0.1f : (io.KeyCtrl ? 15.0f : 1.0f);
                auto applyToSelectedFaces = [&](const auto& fn)
                {
                    for (int faceIndex : selectedFaceIndices_)
                    {
                        if (faceIndex >= 0 && faceIndex < static_cast<int>(obj.mesh.faceCount()))
                            fn(obj.mesh.facesMutable()[(size_t)faceIndex]);
                    }
                };
                auto pushUndoForActivatedItem = [&]()
                {
                    if (ImGui::IsItemActivated())
                        PushUndoState();
                };
                auto applyUvStateToSelectedFaces = [&](const glm::vec2& offset,
                                                       const glm::vec2& scale,
                                                       float rotation)
                {
                    applyToSelectedFaces([&](EditableFace& selectedFace) {
                        selectedFace.uvOffset = offset;
                        selectedFace.uvScale = scale;
                        selectedFace.uvRotation = rotation;
                    });
                    meshCacheValid_ = false;
                    sceneDirty_ = true;
                };
                auto applyPrimaryUvStateToSelectedFaces = [&]()
                {
                    applyUvStateToSelectedFaces(face.uvOffset, face.uvScale, face.uvRotation);
                };
                ImGui::TextDisabled("UV fine tune: Shift=fine  Ctrl=coarse");
                if (ImGui::Button("Copy UV"))
                {
                    faceUvClipboard_.hasData = true;
                    faceUvClipboard_.uvOffset = face.uvOffset;
                    faceUvClipboard_.uvScale = face.uvScale;
                    faceUvClipboard_.uvRotation = face.uvRotation;
                    faceUvClipboard_.uvProjection = face.uvProjection;
                    sceneStatusMessage_ = "Copied face UV";
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!faceUvClipboard_.hasData);
                if (ImGui::Button("Paste UV"))
                {
                    PushUndoState();
                    applyToSelectedFaces([&](EditableFace& selectedFace) {
                        selectedFace.uvOffset = faceUvClipboard_.uvOffset;
                        selectedFace.uvScale = faceUvClipboard_.uvScale;
                        selectedFace.uvRotation = faceUvClipboard_.uvRotation;
                        selectedFace.uvProjection = faceUvClipboard_.uvProjection;
                    });
                    meshCacheValid_ = false;
                    sceneDirty_ = true;
                    face = obj.mesh.facesMutable()[(size_t)selectedFaceIndex_];
                    sceneStatusMessage_ = "Pasted UV to selected faces";
                }
                ImGui::EndDisabled();
                //ImGui::SameLine();
                //ImGui::TextDisabled("Ctrl+C / Ctrl+V");
                ImGui::SameLine();
                if (ImGui::Button("Mapping"))
                    showUvMappingWindow_ = true;
                if (ImGui::DragFloat2("Move UV", &face.uvOffset.x, uvMoveStep, -1000.0f, 1000.0f, "%.3f"))
                {
                    pushUndoForActivatedItem();
                    applyPrimaryUvStateToSelectedFaces();
                }
                if (ImGui::InputFloat2("Move UV##Input", &face.uvOffset.x, "%.4f"))
                {
                    PushUndoState();
                    applyPrimaryUvStateToSelectedFaces();
                }
                if (ImGui::DragFloat2("Tile UV", &face.uvScale.x, uvScaleStep, 0.01f, 128.0f, "%.3f"))
                {
                    pushUndoForActivatedItem();
                    face.uvScale = glm::max(face.uvScale, glm::vec2(0.01f));
                    applyPrimaryUvStateToSelectedFaces();
                }
                if (ImGui::InputFloat2("Tile UV##Input", &face.uvScale.x, "%.4f"))
                {
                    PushUndoState();
                    face.uvScale = glm::max(face.uvScale, glm::vec2(0.01f));
                    applyPrimaryUvStateToSelectedFaces();
                }
                if (ImGui::DragFloat("Rotate UV", &face.uvRotation, uvRotateStep, -360.0f, 360.0f, "%.2f"))
                {
                    pushUndoForActivatedItem();
                    applyPrimaryUvStateToSelectedFaces();
                }
                if (ImGui::InputFloat("Rotate UV##Input", &face.uvRotation, 0.0f, 0.0f, "%.3f"))
                {
                    PushUndoState();
                    applyPrimaryUvStateToSelectedFaces();
                }

                if (ImGui::Button("U-"))
                {
                    PushUndoState();
                    face.uvOffset.x -= uvMoveStep;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("U+"))
                {
                    PushUndoState();
                    face.uvOffset.x += uvMoveStep;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("V-"))
                {
                    PushUndoState();
                    face.uvOffset.y -= uvMoveStep;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("V+"))
                {
                    PushUndoState();
                    face.uvOffset.y += uvMoveStep;
                    applyPrimaryUvStateToSelectedFaces();
                }

                if (ImGui::Button("Tile-"))
                {
                    PushUndoState();
                    face.uvScale = glm::max(face.uvScale - glm::vec2(uvScaleStep), glm::vec2(0.01f));
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("Tile+"))
                {
                    PushUndoState();
                    face.uvScale += glm::vec2(uvScaleStep);
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("Rot-"))
                {
                    PushUndoState();
                    face.uvRotation -= uvRotateStep;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("Rot+"))
                {
                    PushUndoState();
                    face.uvRotation += uvRotateStep;
                    applyPrimaryUvStateToSelectedFaces();
                }

                if (ImGui::Button("Fit 1x1"))
                {
                    PushUndoState();
                    face.uvOffset = glm::vec2(0.0f);
                    face.uvScale = glm::vec2(1.0f);
                    face.uvRotation = 0.0f;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("Flip U"))
                {
                    PushUndoState();
                    face.uvScale.x = -face.uvScale.x;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("Flip V"))
                {
                    PushUndoState();
                    face.uvScale.y = -face.uvScale.y;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("Rot 90"))
                {
                    PushUndoState();
                    face.uvRotation += 90.0f;
                    applyPrimaryUvStateToSelectedFaces();
                }

                if (ImGui::Button("Reset UV"))
                {
                    PushUndoState();
                    face.uvOffset = glm::vec2(0.0f);
                    face.uvScale = glm::vec2(1.0f);
                    face.uvRotation = 0.0f;
                    applyPrimaryUvStateToSelectedFaces();
                }
                ImGui::SameLine();
                if (ImGui::Button("Copy to Object"))
                {
                    PushUndoState();
                    for (EditableFace& f : obj.mesh.facesMutable())
                    {
                        f.uvOffset = face.uvOffset;
                        f.uvScale = face.uvScale;
                        f.uvRotation = face.uvRotation;
                    }
                    meshCacheValid_ = false;
                    sceneDirty_ = true;
                }
            }
            else
            {
                ImGui::TextDisabled("Select a face to edit UVs");
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Reload Textures"))
        {
            failedTextureLoads_.clear();
            TextureManager& texMgr = TextureManager::instance();
            const std::string baseDir = assetRoot_.empty() ? "assets" : assetRoot_;
            const int preloadedLegacyMaterials = preloadLegacyImportedMaterials(scene_, assetRoot_, lastImportDir_);
            int reloaded = 0;
            for (LevelMeshObject& obj : scene_.meshObjects())
            {
                for (const EditableFace& face : obj.mesh.faces())
                {
                    if (face.materialName.empty() || face.materialName == "default")
                        continue;
                    Texture* existing = resolveTextureForMaterialRef(
                        texMgr, face.materialName, "level_face_tex::",
                        &failedTextureLoads_, assetRoot_, lastImportDir_);
                    if (existing && existing->id != 0)
                        ++reloaded;
                }
            }
            meshCacheValid_ = false;
            SyncCurrentTextureFromSelection();
            sceneStatusMessage_ = "Reloaded " + std::to_string(reloaded) + " textures from: " + baseDir;
            if (preloadedLegacyMaterials > 0)
                sceneStatusMessage_ += " (loaded " + std::to_string(preloadedLegacyMaterials) + " legacy texture sets)";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Uses Assets root");
    }

    if (Section("History"))
    {
        ImGui::Text("Undo states: %d", static_cast<int>(undoStack_.size()));
        ImGui::Text("Redo states: %d", static_cast<int>(redoStack_.size()));
        const float halfWidth = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
        ImGui::BeginDisabled(undoStack_.empty());
        if (ImGui::Button("Undo##panel", ImVec2(halfWidth, 0.0f)))
            PerformUndo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(redoStack_.empty());
        if (ImGui::Button("Redo##panel", ImVec2(halfWidth, 0.0f)))
            PerformRedo();
        ImGui::EndDisabled();
    }

    if (Section("Roadmap"))
    {
        ImGui::BulletText("Create box -> render real mesh");
        ImGui::BulletText("Object / Face / Vertex selection");
        ImGui::BulletText("Extrude, split, clip, bevel");
        ImGui::BulletText("Entities with editable properties");
        ImGui::BulletText("CSG as a higher-level operation");
    }

    ImGui::End();
}

void LevelEditorApp::ShowUvMappingWindow()
{
    static bool uvWindowWasOpen = false;

    if (!showUvMappingWindow_)
    {
        uvWindowWasOpen = false;
        return;
    }

    const bool hasFace = selectionMode_ == SelectionMode::Face &&
                         selectedMeshIndex_ >= 0 &&
                         selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
                         selectedFaceIndex_ >= 0 &&
                         selectedFaceIndex_ < static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faceCount());
    if (!hasFace)
    {
        showUvMappingWindow_ = false;
        return;
    }

    LevelMeshObject& obj = scene_.meshObjects()[(size_t)selectedMeshIndex_];
    EditableFace& face = obj.mesh.facesMutable()[(size_t)selectedFaceIndex_];
    TextureManager& textureManager = TextureManager::instance();
    Texture* white = textureManager.getWhite();
    Texture* texture = white;
    if (!face.materialName.empty() && face.materialName != "default")
    {
        texture = textureManager.get(face.materialName);
        if (!texture)
        {
            const std::string texName = "level_face_tex::" + face.materialName;
            texture = textureManager.get(texName);
        }
        if (!texture)
            texture = white;
    }

    if (!uvWindowWasOpen)
    {
        if (ImGuiViewport* viewport = ImGui::GetMainViewport())
        {
            const ImVec2 pos(viewport->Pos.x + std::max(24.0f, viewport->Size.x * 0.5f - 260.0f),
                             viewport->Pos.y + 40.0f);
            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        }
        ImGui::SetNextWindowFocus();
        uvWindowWasOpen = true;
    }

    if (!ImGui::Begin("UV Mapping", &showUvMappingWindow_))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Face %d", selectedFaceIndex_);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", face.materialName.empty() ? "(none)" : PathFilename(face.materialName).c_str());

    auto applyPrimaryUvStateToSelectedFaces = [&]()
    {
        for (int faceIndex : selectedFaceIndices_)
        {
            if (faceIndex >= 0 && faceIndex < static_cast<int>(obj.mesh.faceCount()))
            {
                EditableFace& selectedFace = obj.mesh.facesMutable()[(size_t)faceIndex];
                selectedFace.uvOffset = face.uvOffset;
                selectedFace.uvScale = face.uvScale;
                selectedFace.uvRotation = face.uvRotation;
            }
        }
        meshCacheValid_ = false;
        sceneDirty_ = true;
    };

    const auto& verts = obj.mesh.vertices();
    glm::vec3 meshCenter(0.0f);
    if (!verts.empty())
    {
        for (const auto& v : verts) meshCenter += v.position;
        meshCenter /= static_cast<float>(verts.size());
    }

    glm::vec3 faceN(0.0f, 1.0f, 0.0f);
    if (face.indices.size() >= 3)
    {
        const glm::vec3& p0 = verts[(size_t)face.indices[0]].position;
        const glm::vec3& p1 = verts[(size_t)face.indices[1]].position;
        const glm::vec3& p2 = verts[(size_t)face.indices[2]].position;
        const glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
        if (glm::length(cr) > 1e-6f)
            faceN = glm::normalize(cr);
    }

    glm::vec3 planarTangent = glm::cross(faceN, glm::vec3(0, 1, 0));
    if (glm::length(planarTangent) < 0.01f)
        planarTangent = glm::cross(faceN, glm::vec3(1, 0, 0));
    planarTangent = glm::normalize(planarTangent);
    const glm::vec3 planarBitangent = glm::normalize(glm::cross(faceN, planarTangent));
    const glm::vec3 absN = glm::abs(faceN);

    auto baseProjectedUv = [&](const EditableVertex& ev) -> glm::vec2
    {
        glm::vec2 uv(0.0f);
        switch (face.uvProjection)
        {
        case UvProjection::Planar:
            uv = glm::vec2(glm::dot(ev.position, planarTangent), glm::dot(ev.position, planarBitangent));
            break;
        case UvProjection::Cylindrical:
        {
            const glm::vec3 rel = ev.position - meshCenter;
            uv.x = std::atan2(rel.x, rel.z) / 6.2831853f + 0.5f;
            uv.y = rel.y * 0.01f;
            break;
        }
        case UvProjection::Spherical:
        {
            const glm::vec3 rel = ev.position - meshCenter;
            const float len = glm::length(rel);
            const glm::vec3 d = len > 1e-6f ? rel / len : glm::vec3(0, 1, 0);
            uv.x = std::atan2(d.x, d.z) / 6.2831853f + 0.5f;
            uv.y = std::asin(glm::clamp(d.y, -1.0f, 1.0f)) / 3.1415926f + 0.5f;
            break;
        }
        case UvProjection::Mesh:
            uv = ev.uv;
            return uv;
        case UvProjection::Box:
        default:
            if (absN.y >= absN.x && absN.y >= absN.z)
                uv = glm::vec2(ev.position.x, ev.position.z);
            else if (absN.x >= absN.y && absN.x >= absN.z)
                uv = glm::vec2(ev.position.z, ev.position.y);
            else
                uv = glm::vec2(ev.position.x, ev.position.y);
            break;
        }
        return uv * 0.01f;
    };

    std::vector<glm::vec2> overlayBaseUvs;
    overlayBaseUvs.reserve(face.indices.size());
    for (int idx : face.indices)
    {
        if (idx < 0 || idx >= static_cast<int>(verts.size()))
            continue;
        overlayBaseUvs.push_back(baseProjectedUv(verts[(size_t)idx]));
    }

    auto transformedUv = [&](const glm::vec2& baseUv) -> glm::vec2
    {
        const float angle = glm::radians(face.uvRotation);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const glm::vec2 scaled = baseUv * face.uvScale;
        const glm::vec2 rotated(
            scaled.x * c - scaled.y * s,
            scaled.x * s + scaled.y * c);
        return rotated + face.uvOffset;
    };

    auto fitFaceUvToUnit = [&]()
    {
        if (overlayBaseUvs.empty())
            return;

        constexpr float fitMargin = 0.04f;
        glm::vec2 uvMin = overlayBaseUvs[0];
        glm::vec2 uvMax = overlayBaseUvs[0];
        for (const glm::vec2& uv : overlayBaseUvs)
        {
            uvMin = glm::min(uvMin, uv);
            uvMax = glm::max(uvMax, uv);
        }

        const glm::vec2 uvSpan = glm::max(uvMax - uvMin, glm::vec2(1e-4f));
        face.uvRotation = 0.0f;
        const glm::vec2 targetSpan(1.0f - fitMargin * 2.0f);
        face.uvScale = glm::vec2(targetSpan.x / uvSpan.x, targetSpan.y / uvSpan.y);
        face.uvOffset = glm::vec2(fitMargin) - uvMin * face.uvScale;
    };

    std::vector<glm::vec2> overlayUvs;
    overlayUvs.reserve(overlayBaseUvs.size());
    for (const glm::vec2& baseUv : overlayBaseUvs)
        overlayUvs.push_back(transformedUv(baseUv));

    static float uvPreviewZoom = 1.0f;
    static bool requestUvPreviewFrame = true;
    static int lastUvPreviewMeshIndex = -1;
    static int lastUvPreviewFaceIndex = -1;
    if (lastUvPreviewMeshIndex != selectedMeshIndex_ || lastUvPreviewFaceIndex != selectedFaceIndex_)
    {
        requestUvPreviewFrame = true;
        lastUvPreviewMeshIndex = selectedMeshIndex_;
        lastUvPreviewFaceIndex = selectedFaceIndex_;
    }

    ImGui::TextDisabled("Zoom");
    ImGui::SameLine();
    if (ImGui::SmallButton("-##UvZoom"))
        uvPreviewZoom = std::max(0.25f, uvPreviewZoom / 1.25f);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##UvZoom"))
        uvPreviewZoom = std::min(8.0f, uvPreviewZoom * 1.25f);
    ImGui::SameLine();
    if (ImGui::SmallButton("1:1##UvZoom"))
        uvPreviewZoom = 1.0f;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##UvZoomValue", &uvPreviewZoom, 0.25f, 8.0f, "%.2fx");

    const ImVec2 previewAvail = ImGui::GetContentRegionAvail();
    const float controlsReserve = ImGui::GetFrameHeightWithSpacing() * 8.0f;
    const float previewRegionWidth = std::max(previewAvail.x, 256.0f);
    const float previewRegionHeight = std::max(std::min(previewAvail.y - controlsReserve, previewRegionWidth), 220.0f);
    const ImVec2 previewRegionExtent(previewRegionWidth, previewRegionHeight);
    ImGui::BeginChild("##UvPreviewRegion", previewRegionExtent, true, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 previewBaseExtent(
        std::max(240.0f, previewRegionWidth - 16.0f),
        std::max(180.0f, previewRegionHeight - 16.0f));

    if (requestUvPreviewFrame && !overlayUvs.empty())
    {
        glm::vec2 uvMin = overlayUvs[0];
        glm::vec2 uvMax = overlayUvs[0];
        for (const glm::vec2& uv : overlayUvs)
        {
            uvMin = glm::min(uvMin, uv);
            uvMax = glm::max(uvMax, uv);
        }

        const glm::vec2 uvSpan = glm::max(uvMax - uvMin, glm::vec2(0.001f));
        const ImVec2 fitPixels(
            std::max(previewRegionWidth - 48.0f, 120.0f),
            std::max(previewRegionHeight - 48.0f, 120.0f));
        const float fitZoomX = fitPixels.x / (uvSpan.x * std::max(previewBaseExtent.x, 1.0f));
        const float fitZoomY = fitPixels.y / (uvSpan.y * std::max(previewBaseExtent.y, 1.0f));
        uvPreviewZoom = std::clamp(std::min(fitZoomX, fitZoomY), 0.25f, 8.0f);
    }

    const ImVec2 previewExtent(previewBaseExtent.x * uvPreviewZoom, previewBaseExtent.y * uvPreviewZoom);
    const ImVec2 previewMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##UvPreview", previewExtent, ImGuiButtonFlags_MouseButtonLeft);
    const ImVec2 previewMax(previewMin.x + previewExtent.x, previewMin.y + previewExtent.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddImage((ImTextureID)(intptr_t)texture->id, previewMin, previewMax, ImVec2(0, 1), ImVec2(1, 0));
    drawList->AddRect(previewMin, previewMax, IM_COL32(180, 190, 205, 255), 0.0f, 0, 1.2f);

    for (int i = 1; i < 4; ++i)
    {
        const float t = static_cast<float>(i) / 4.0f;
        const float x = previewMin.x + previewExtent.x * t;
        const float y = previewMin.y + previewExtent.y * t;
        drawList->AddLine(ImVec2(x, previewMin.y), ImVec2(x, previewMax.y), IM_COL32(255, 255, 255, 28), 1.0f);
        drawList->AddLine(ImVec2(previewMin.x, y), ImVec2(previewMax.x, y), IM_COL32(255, 255, 255, 28), 1.0f);
    }

    auto uvToScreen = [&](const glm::vec2& uv) -> ImVec2
    {
        return ImVec2(previewMin.x + uv.x * previewExtent.x, previewMin.y + (1.0f - uv.y) * previewExtent.y);
    };
    auto screenToUv = [&](const ImVec2& point) -> glm::vec2
    {
        return glm::vec2(
            (point.x - previewMin.x) / std::max(previewExtent.x, 1.0f),
            1.0f - ((point.y - previewMin.y) / std::max(previewExtent.y, 1.0f)));
    };

    std::vector<ImVec2> overlayPoints;
    overlayPoints.reserve(overlayUvs.size());
    for (const glm::vec2& uv : overlayUvs)
        overlayPoints.push_back(uvToScreen(uv));

    if (requestUvPreviewFrame && !overlayUvs.empty())
    {
        glm::vec2 uvMin = overlayUvs[0];
        glm::vec2 uvMax = overlayUvs[0];
        for (const glm::vec2& uv : overlayUvs)
        {
            uvMin = glm::min(uvMin, uv);
            uvMax = glm::max(uvMax, uv);
        }

        const ImVec2 uvMinScreen = uvToScreen(uvMin);
        const ImVec2 uvMaxScreen = uvToScreen(uvMax);
        const float boundsMinX = std::min(uvMinScreen.x, uvMaxScreen.x) - previewMin.x;
        const float boundsMaxX = std::max(uvMinScreen.x, uvMaxScreen.x) - previewMin.x;
        const float boundsMinY = std::min(uvMinScreen.y, uvMaxScreen.y) - previewMin.y;
        const float boundsMaxY = std::max(uvMinScreen.y, uvMaxScreen.y) - previewMin.y;
        const float targetScrollX = std::max(0.0f, (boundsMinX + boundsMaxX) * 0.5f - previewRegionWidth * 0.5f);
        const float targetScrollY = std::max(0.0f, (boundsMinY + boundsMaxY) * 0.5f - previewRegionHeight * 0.5f);
        ImGui::SetScrollX(targetScrollX);
        ImGui::SetScrollY(targetScrollY);
        requestUvPreviewFrame = false;
    }

    for (std::size_t i = 0; i < overlayPoints.size(); ++i)
    {
        const ImVec2 a = overlayPoints[i];
        const ImVec2 b = overlayPoints[(i + 1) % overlayPoints.size()];
        drawList->AddLine(a, b, IM_COL32(255, 210, 90, 255), 2.0f);
        drawList->AddCircleFilled(a, 7.0f, IM_COL32(255, 245, 180, 255), 12);
        drawList->AddCircle(a, 10.0f, IM_COL32(255, 255, 230, 255), 12, 1.4f);
    }

    auto cross2 = [](const ImVec2& a, const ImVec2& b, const ImVec2& p) -> float
    {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    };
    auto pointInPolygon = [&](const ImVec2& p) -> bool
    {
        if (overlayPoints.size() < 3)
            return false;
        bool hasPos = false;
        bool hasNeg = false;
        for (std::size_t i = 0; i < overlayPoints.size(); ++i)
        {
            const float c = cross2(overlayPoints[i], overlayPoints[(i + 1) % overlayPoints.size()], p);
            hasPos |= c > 0.0f;
            hasNeg |= c < 0.0f;
        }
        return !(hasPos && hasNeg);
    };

    static int activeUvCorner = -1;
    static bool draggingUvRect = false;

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mousePos = io.MousePos;
    int hoveredCorner = -1;
    for (std::size_t i = 0; i < overlayPoints.size(); ++i)
    {
        const float dx = mousePos.x - overlayPoints[i].x;
        const float dy = mousePos.y - overlayPoints[i].y;
        if ((dx * dx + dy * dy) <= (16.0f * 16.0f))
        {
            hoveredCorner = static_cast<int>(i);
            break;
        }
    }

    const bool previewHovered = ImGui::IsItemHovered();
    if (previewHovered && io.MouseWheel != 0.0f)
        uvPreviewZoom = std::clamp(uvPreviewZoom * (io.MouseWheel > 0.0f ? 1.1f : 0.9f), 0.25f, 8.0f);

    const bool canResizeUvPolygon = overlayBaseUvs.size() == 4;
    const bool clickedOnUvHandle = hoveredCorner >= 0;
    const bool clickedOnUvRect = pointInPolygon(mousePos);
    if (previewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (clickedOnUvHandle || clickedOnUvRect))
    {
        PushUndoState();
        activeUvCorner = canResizeUvPolygon ? hoveredCorner : -1;
        draggingUvRect = (hoveredCorner < 0) && clickedOnUvRect;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        activeUvCorner = -1;
        draggingUvRect = false;
    }

    if (canResizeUvPolygon && activeUvCorner >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const int oppositeCorner = (activeUvCorner + 2) % 4;
        const glm::vec2 draggedUv = screenToUv(mousePos);
        const glm::vec2 fixedUv = overlayUvs[oppositeCorner];
        const glm::vec2 worldDiff = draggedUv - fixedUv;
        const float angle = glm::radians(face.uvRotation);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const glm::vec2 localDiff(
            worldDiff.x * c + worldDiff.y * s,
            -worldDiff.x * s + worldDiff.y * c);

        const glm::vec2 baseDiff = overlayBaseUvs[activeUvCorner] - overlayBaseUvs[oppositeCorner];
        if (std::abs(baseDiff.x) > 1e-6f)
            face.uvScale.x = localDiff.x / baseDiff.x;
        if (std::abs(baseDiff.y) > 1e-6f)
            face.uvScale.y = localDiff.y / baseDiff.y;

        const glm::vec2 fixedBaseUv = overlayBaseUvs[oppositeCorner];
        const glm::vec2 fixedScaled = fixedBaseUv * face.uvScale;
        const glm::vec2 fixedRotated(
            fixedScaled.x * c - fixedScaled.y * s,
            fixedScaled.x * s + fixedScaled.y * c);
        face.uvOffset = fixedUv - fixedRotated;
        applyPrimaryUvStateToSelectedFaces();
    }
    else if (draggingUvRect && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const glm::vec2 uvDelta = screenToUv(ImVec2(previewMin.x + io.MouseDelta.x, previewMin.y + io.MouseDelta.y)) -
                                  screenToUv(previewMin);
        face.uvOffset += uvDelta;
        applyPrimaryUvStateToSelectedFaces();
    }

    ImGui::EndChild();

    ImGui::TextDisabled("Drag rect to move UV, drag corners to resize");
    if (ImGui::DragFloat2("Offset##UvWindow", &face.uvOffset.x, 0.01f, -1000.0f, 1000.0f, "%.3f"))
    {
        PushUndoState();
        applyPrimaryUvStateToSelectedFaces();
    }
    if (ImGui::DragFloat2("Scale##UvWindow", &face.uvScale.x, 0.01f, -128.0f, 128.0f, "%.3f"))
    {
        PushUndoState();
        applyPrimaryUvStateToSelectedFaces();
    }
    if (ImGui::DragFloat("Rotation##UvWindow", &face.uvRotation, 1.0f, -360.0f, 360.0f, "%.2f"))
    {
        PushUndoState();
        applyPrimaryUvStateToSelectedFaces();
    }
    if (ImGui::Button("Mirror X##UvWindow"))
    {
        PushUndoState();
        face.uvScale.x = -face.uvScale.x;
        applyPrimaryUvStateToSelectedFaces();
    }
    ImGui::SameLine();
    if (ImGui::Button("Mirror Y##UvWindow"))
    {
        PushUndoState();
        face.uvScale.y = -face.uvScale.y;
        applyPrimaryUvStateToSelectedFaces();
    }
    ImGui::SameLine();
    if (ImGui::Button("Center##UvWindow"))
    {
        PushUndoState();
        face.uvOffset = glm::vec2(0.0f);
        applyPrimaryUvStateToSelectedFaces();
    }
    if (ImGui::Button("Rotate 90##UvWindow"))
    {
        PushUndoState();
        face.uvRotation = std::remainder(face.uvRotation + 90.0f, 360.0f);
        applyPrimaryUvStateToSelectedFaces();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotate 180##UvWindow"))
    {
        PushUndoState();
        face.uvRotation = std::remainder(face.uvRotation + 180.0f, 360.0f);
        applyPrimaryUvStateToSelectedFaces();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotate -90##UvWindow"))
    {
        PushUndoState();
        face.uvRotation = std::remainder(face.uvRotation - 90.0f, 360.0f);
        applyPrimaryUvStateToSelectedFaces();
    }
    if (ImGui::Button("Fit 1x1##UvWindow"))
    {
        PushUndoState();
        fitFaceUvToUnit();
        requestUvPreviewFrame = true;
        applyPrimaryUvStateToSelectedFaces();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##UvWindow"))
    {
        PushUndoState();
        face.uvOffset = glm::vec2(0.0f);
        face.uvScale = glm::vec2(1.0f);
        face.uvRotation = 0.0f;
        applyPrimaryUvStateToSelectedFaces();
    }

    ImGui::End();
}

void LevelEditorApp::ShowAssetsPanel()
{
    ImGui::SetNextWindowPos(assetPanelPos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(assetPanelSize_, ImGuiCond_Always);
    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Assets", nullptr, panelFlags))
    {
        ImGui::End();
        return;
    }

    if (ImGui::InputText("Root", &assetRoot_, ImGuiInputTextFlags_EnterReturnsTrue))
        RescanAssets();
    ImGui::SameLine();
    if (ImGui::Button("Folder..."))
    {
        std::filesystem::path rootPath = assetRoot_.empty() ? std::filesystem::current_path() : std::filesystem::path(assetRoot_);
        if (rootPath.is_relative())
            rootPath = std::filesystem::current_path() / rootPath;
        assetFolderDialog_.Open(ImGuiFileDialog::Mode::ChooseFolder, rootPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Use assets"))
    {
        assetRoot_ = "assets";
        RescanAssets();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan"))
        RescanAssets();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::BeginCombo("View", assetViewModeName(assetViewMode_)))
    {
        const AssetViewMode modes[] = {AssetViewMode::List, AssetViewMode::Details, AssetViewMode::Grid};
        for (AssetViewMode mode : modes)
        {
            const bool selected = mode == assetViewMode_;
            if (ImGui::Selectable(assetViewModeName(mode), selected))
                assetViewMode_ = mode;
        }
        ImGui::EndCombo();
    }

    ImGui::InputText("Filter", &assetFilter_);
    ImGui::TextDisabled("Found: %d", static_cast<int>(assets_.size()));
    if (!currentTexturePath_.empty())
        ImGui::Text("Current: %s", PathFilename(currentTexturePath_).c_str());

    ImGui::Separator();
    ImGui::BeginChild("##asset_list", ImVec2(-1.0f, -1.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::vector<const AssetEntry*> filtered;
    filtered.reserve(assets_.size());
    for (const AssetEntry& asset : assets_)
    {
        if (!containsInsensitive(asset.path, assetFilter_) &&
            !containsInsensitive(asset.name, assetFilter_))
        {
            continue;
        }
        filtered.push_back(&asset);
    }

    if (assetViewMode_ == AssetViewMode::Grid)
    {
        TextureManager& textureManager = TextureManager::instance();
        Texture* white = textureManager.getWhite();
        const float thumbnailSize = 64.0f;
        const float padding = 6.0f;
        const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x - padding) / (thumbnailSize + padding)));
        int itemIndex = 0;
        for (const AssetEntry* asset : filtered)
        {
            if (itemIndex > 0 && (itemIndex % perRow) != 0)
                ImGui::SameLine();

            ImGui::PushID(itemIndex);
            const std::string texName = "level_asset_thumb::" + asset->path;
            Texture* tex = textureManager.get(texName);
            if (!tex)
                tex = textureManager.load(texName, asset->path);

            const bool selected = asset->path == selectedAssetPath_;
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.33f, 0.53f, 0.93f, 1.0f));
            ImTextureID texId = (ImTextureID)(intptr_t)((tex && tex->id != 0) ? tex->id : white->id);
            if (ImGui::ImageButton("##thumb", texId, ImVec2(thumbnailSize, thumbnailSize)))
            {
                selectedAssetPath_ = asset->path;
                currentTexturePath_ = asset->path;
            }
            if (selected)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(asset->name.c_str());
                ImGui::TextUnformatted(asset->path.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
            itemIndex++;
        }
    }
    else if (assetViewMode_ == AssetViewMode::Details)
    {
        if (ImGui::BeginTable("AssetsDetails", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("Path");
            ImGui::TableHeadersRow();
            for (const AssetEntry* asset : filtered)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool selected = asset->path == selectedAssetPath_;
                if (ImGui::Selectable(asset->name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    selectedAssetPath_ = asset->path;
                    currentTexturePath_ = asset->path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(asset->path.c_str());
            }
            ImGui::EndTable();
        }
    }
    else
    {
        for (const AssetEntry* asset : filtered)
        {
            const bool selected = asset->path == selectedAssetPath_;
            if (ImGui::Selectable(asset->path.c_str(), selected))
            {
                selectedAssetPath_ = asset->path;
                currentTexturePath_ = asset->path;
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void LevelEditorApp::ShowStatusBar(float deltaTime)
{
    Device& device = Device::Instance();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 28.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 28.0f), ImGuiCond_Always);
    if (ImGui::Begin("LevelStatusBar", nullptr, flags))
    {
        ImGui::Text("Tool: %s", toolName(currentTool_));
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::Text("Mode: %s", selectionModeName(selectionMode_));
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::Text("Grid: %.0f", gridSize_);
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::Text("Undo: %d  Redo: %d", static_cast<int>(undoStack_.size()), static_cast<int>(redoStack_.size()));
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::Text("Scene: %s%s", scenePath_.empty() ? "(unsaved)" : scenePath_.c_str(), sceneDirty_ ? "*" : "");
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        if (device.IsGifRecording())
        {
            ImGui::Text("REC %d fps  frames:%d", device.GetGifRecordingFPS(), device.GetGifRecordingFrameCount());
        }
        else if (device.IsFrameSequenceRecording())
        {
            ImGui::Text("REC %s %d fps frames:%d",
                        device.GetFrameSequenceExtension().c_str(),
                        device.GetFrameSequenceFPS(),
                        device.GetFrameSequenceFrameCount());
        }
        if (device.IsGifRecording() || device.IsFrameSequenceRecording())
        {
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
        }
        ImGui::Text("%.1f FPS (%.2f ms)", deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f, deltaTime * 1000.0f);
        if (!sceneStatusMessage_.empty())
        {
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            ImGui::TextUnformatted(sceneStatusMessage_.c_str());
        }
    }
    ImGui::End();
}
