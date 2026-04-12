#include "LevelEditorApp.hpp"

#include "Input.hpp"
#include "LevelEditorSceneIO.hpp"
#include "Manager.hpp"
#include "CSG.hpp"
#include "MeshCSG.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <map>
#include <limits>

#include <glm/gtx/norm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ImGuizmo.h"
#include "imgui.h"
#include "imgui_stdlib.h"

namespace
{
glm::mat4 meshLocalPivotTransform(const LevelMeshObject& object,
                                  const glm::vec3& rotationEuler,
                                  const glm::vec3& scale);

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
    }
    return "Entity";
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

EditableMesh transformedEditableMesh(const EditableMesh& mesh, const glm::mat4& transform)
{
    std::vector<EditableVertex> vertices = mesh.vertices();
    for (EditableVertex& vertex : vertices)
        vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
    return EditableMesh::FromData(vertices, mesh.faces());
}

struct LevelCSGBox
{
    glm::vec3 mins {0.0f};
    glm::vec3 maxs {0.0f};

    bool isValid() const
    {
        return (maxs.x - mins.x) > 1e-4f &&
               (maxs.y - mins.y) > 1e-4f &&
               (maxs.z - mins.z) > 1e-4f;
    }
};

LevelCSGBox editableMeshWorldBox(const LevelMeshObject& object)
{
    BoundingBox bounds;
    const glm::mat4 model = meshObjectModelMatrix(object);
    for (const EditableVertex& vertex : object.mesh.vertices())
        bounds.expand(glm::vec3(model * glm::vec4(vertex.position, 1.0f)));
    return {bounds.min, bounds.max};
}

bool csgBoxesIntersect(const LevelCSGBox& a, const LevelCSGBox& b)
{
    return !(a.maxs.x <= b.mins.x || a.mins.x >= b.maxs.x ||
             a.maxs.y <= b.mins.y || a.mins.y >= b.maxs.y ||
             a.maxs.z <= b.mins.z || a.mins.z >= b.maxs.z);
}

std::vector<LevelCSGBox> subtractCSGBoxes(const LevelCSGBox& a, const LevelCSGBox& b)
{
    std::vector<LevelCSGBox> result;
    if (!csgBoxesIntersect(a, b))
    {
        result.push_back(a);
        return result;
    }

    const float ox0 = glm::max(a.mins.x, b.mins.x);
    const float ox1 = glm::min(a.maxs.x, b.maxs.x);
    const float oy0 = glm::max(a.mins.y, b.mins.y);
    const float oy1 = glm::min(a.maxs.y, b.maxs.y);

    auto addPiece = [&](const glm::vec3& mins, const glm::vec3& maxs)
    {
        LevelCSGBox piece {mins, maxs};
        if (piece.isValid())
            result.push_back(piece);
    };

    addPiece(a.mins, glm::vec3(glm::min(b.mins.x, a.maxs.x), a.maxs.y, a.maxs.z));
    addPiece(glm::vec3(glm::max(b.maxs.x, a.mins.x), a.mins.y, a.mins.z), a.maxs);
    addPiece(glm::vec3(ox0, a.mins.y, a.mins.z), glm::vec3(ox1, glm::min(b.mins.y, a.maxs.y), a.maxs.z));
    addPiece(glm::vec3(ox0, glm::max(b.maxs.y, a.mins.y), a.mins.z), glm::vec3(ox1, a.maxs.y, a.maxs.z));
    addPiece(glm::vec3(ox0, oy0, a.mins.z), glm::vec3(ox1, oy1, glm::min(b.mins.z, a.maxs.z)));
    addPiece(glm::vec3(ox0, oy0, glm::max(b.maxs.z, a.mins.z)), glm::vec3(ox1, oy1, a.maxs.z));

    return result;
}

std::vector<LevelCSGBox> intersectCSGBoxes(const LevelCSGBox& a, const LevelCSGBox& b)
{
    std::vector<LevelCSGBox> result;
    if (!csgBoxesIntersect(a, b))
        return result;

    LevelCSGBox piece {
        glm::max(a.mins, b.mins),
        glm::min(a.maxs, b.maxs)
    };
    if (piece.isValid())
        result.push_back(piece);
    return result;
}

LevelMeshObject makeLevelObjectFromCSGBox(const LevelMeshObject& source, const LevelCSGBox& box, const std::string& name)
{
    LevelMeshObject out = source;
    out.name = name;
    out.position = glm::vec3(0.0f);
    out.rotationEuler = glm::vec3(0.0f);
    out.scale = glm::vec3(1.0f);
    out.pivot = glm::vec3(0.0f);
    out.mesh = EditableMesh::MakeBox(box.mins, box.maxs);
    return out;
}

void fillRenderMeshFromEditable(const EditableMesh& editableMesh, Mesh& mesh)
{
    for (const EditableFace& face : editableMesh.faces())
    {
        if (face.indices.size() < 3)
            continue;

        const uint32_t baseIndex = static_cast<uint32_t>(mesh.buffer.vertices.size());
        for (int vertexIndex : face.indices)
        {
            if (vertexIndex < 0 || vertexIndex >= static_cast<int>(editableMesh.vertices().size()))
                continue;

            Vertex vertex{};
            vertex.position = editableMesh.vertices()[(size_t)vertexIndex].position;
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            vertex.uv = glm::vec2(0.0f);
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
    vertices.reserve(mesh.buffer.vertices.size());
    faces.reserve(mesh.buffer.indices.size() / 3);

    for (std::size_t i = 0; i + 2 < mesh.buffer.indices.size(); i += 3)
    {
        EditableFace face;
        face.materialName = "csg";
        for (int k = 0; k < 3; ++k)
        {
            const uint32_t index = mesh.buffer.indices[i + k];
            if (index >= mesh.buffer.vertices.size())
                continue;

            EditableVertex vertex;
            vertex.position = glm::vec3(transform * glm::vec4(mesh.buffer.vertices[index].position, 1.0f));
            vertices.push_back(vertex);
            face.indices.push_back(static_cast<int>(vertices.size()) - 1);
        }
        if (face.indices.size() == 3)
            faces.push_back(face);
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

LevelEditorApp::LevelEditorApp()
{
    applyLevelEditorTheme(theme_);
    InitializeViews();
    scenePath_ = "bin/level_scene.mred";
    RescanAssets();
    SyncSelectedMeshes();
}

void LevelEditorApp::RenderFrame(float deltaTime)
{
    ImGuizmo::BeginFrame();
    HandleUndoRedoShortcuts();
    HandleToolShortcuts();
    ShowMenuBar();
    UpdatePanelLayout();
    ShowLeftPanel();
    ShowCenterPanel();
    ShowAssetsPanel();
    ShowRightPanel();
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

void LevelEditorApp::SetSingleSelectedMesh(int index)
{
    const int meshCount = static_cast<int>(scene_.meshObjects().size());
    if (meshCount <= 0)
    {
        selectedMeshIndex_ = -1;
        selectedMeshIndices_.clear();
        return;
    }

    selectedMeshIndex_ = std::clamp(index, 0, meshCount - 1);
    selectedMeshIndices_.assign(1, selectedMeshIndex_);
    selectedVertexIndices_.clear();
    selectedFaceIndex_ = -1;
}

void LevelEditorApp::SyncSelectedMeshes()
{
    const int meshCount = static_cast<int>(scene_.meshObjects().size());
    if (meshCount <= 0)
    {
        selectedMeshIndex_ = -1;
        selectedMeshIndices_.clear();
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
    }
}

bool LevelEditorApp::LoadSceneFromPath(const std::string& path)
{
    std::string error;
    LevelEditorScene loaded;
    if (!loadLevelEditorScene(std::filesystem::path(path), loaded, error))
    {
        sceneStatusMessage_ = "Load failed: " + error;
        return false;
    }

    scene_ = loaded;
    scenePath_ = path;
    sceneDirty_ = false;
    undoStack_.clear();
    redoStack_.clear();
    selectedMeshIndex_ = std::clamp(selectedMeshIndex_, 0, std::max(0, static_cast<int>(scene_.meshObjects().size()) - 1));
    SyncSelectedMeshes();
    selectedEntityIndex_ = std::clamp(selectedEntityIndex_, 0, std::max(0, static_cast<int>(scene_.entities().size()) - 1));
    sceneStatusMessage_ = "Loaded: " + scenePath_;
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
    assetFolderDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());

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
    sceneDialog_.Render(std::filesystem::current_path(), std::filesystem::current_path(), std::filesystem::current_path());
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
    selectedEntityIndex_ = std::clamp(selectedEntityIndex_, 0, std::max(0, static_cast<int>(scene_.entities().size()) - 1));
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
    selectedEntityIndex_ = std::clamp(selectedEntityIndex_, 0, std::max(0, static_cast<int>(scene_.entities().size()) - 1));
    return true;
}

void LevelEditorApp::HandleUndoRedoShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;

    const bool ctrlDown = Input::IsKeyDown(KEY_LEFT_CONTROL) || Input::IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shiftDown = Input::IsKeyDown(KEY_LEFT_SHIFT) || Input::IsKeyDown(KEY_RIGHT_SHIFT);
    if (!ctrlDown)
        return;

    if (Input::IsKeyPressed(KEY_Z))
    {
        if (shiftDown)
            PerformRedo();
        else
            PerformUndo();
    }
    else if (Input::IsKeyPressed(KEY_Y))
    {
        PerformRedo();
    }
    else if (Input::IsKeyPressed(KEY_O))
    {
        sceneDialog_.Open(ImGuiFileDialog::Mode::OpenFile, std::filesystem::current_path(), "scene.mred");
    }
    else if (Input::IsKeyPressed(KEY_S))
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

    const bool ctrlDown = Input::IsKeyDown(KEY_LEFT_CONTROL) || Input::IsKeyDown(KEY_RIGHT_CONTROL);
    if (!ctrlDown)
    {
        if (Input::IsKeyPressed(KEY_M)) currentTool_ = Tool::Move;
        if (Input::IsKeyPressed(KEY_R)) currentTool_ = Tool::Rotate;
        if (Input::IsKeyPressed(KEY_S)) currentTool_ = Tool::Scale;
    }

    if (Input::IsKeyPressed(KEY_ONE)) currentTool_ = Tool::Select;
    if (Input::IsKeyPressed(KEY_TWO)) currentTool_ = Tool::Move;
    if (Input::IsKeyPressed(KEY_THREE)) currentTool_ = Tool::Scale;
    if (Input::IsKeyPressed(KEY_FOUR)) currentTool_ = Tool::Rotate;
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

    for (int i = 0; i < static_cast<int>(views_.size()); ++i)
    {
        LevelEditorView& view = views_[i];
        if (i >= activeViewCount_ || view.rect.w <= 0 || view.rect.h <= 0)
            continue;

        view.camera.setViewport(0, 0, view.rect.w, view.rect.h);
        view.camera.setViewPlanes(0.1f, 8192.0f);

        if (view.type == ViewType::Perspective)
        {
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

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
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

    const ImGuiIO& io = ImGui::GetIO();
    const bool ctrlDown = io.KeyCtrl;
    const bool shiftDown = io.KeyShift;
    const glm::vec2 mouseScreen(io.MousePos.x, io.MousePos.y);
    const glm::vec2 delta(io.MouseDelta.x, io.MouseDelta.y);
    const bool leftDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
    const glm::vec2 dragMouseDelta = mouseScreen - dragStartMouse_;

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
                    selectedFaceIndex_ = picked.empty() ? -1 : picked.front();
                    selectedVertexIndices_.clear();
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
                object.mesh.verticesMutable()[(size_t)vertexIndex].position = glm::vec3(inverseModel * glm::vec4(movedWorld, 1.0f));
            }
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
                object.mesh.verticesMutable()[(size_t)vertexIndex].position = glm::vec3(inverseModel * glm::vec4(movedWorld, 1.0f));
            }
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
            LevelMeshObject& object = scene_.meshObjects()[selectedMeshIndex_];
            if (dragTool_ == DragTool::Move)
            {
                const glm::vec3 hoveredWorld = OrthoPointFromScreen(*hovered, hovered->focus, mouseScreen);
                const glm::vec3 objectDelta = ApplyViewDelta(hoveredWorld - dragStartWorld_, dragViewType_);
                object.position = dragStartObjectPosition_ + objectDelta;
                sceneDirty_ = true;
            }
            else if (dragTool_ == DragTool::Scale)
            {
                glm::vec3 newScale = dragStartObjectScale_;
                const float scaleSpeed = 0.01f;
                switch (dragViewType_)
                {
                case ViewType::Top:
                case ViewType::Bottom:
                    newScale.x = std::max(0.01f, dragStartObjectScale_.x + dragMouseDelta.x * scaleSpeed);
                    newScale.z = std::max(0.01f, dragStartObjectScale_.z - dragMouseDelta.y * scaleSpeed);
                    break;
                case ViewType::Front:
                case ViewType::Back:
                    newScale.x = std::max(0.01f, dragStartObjectScale_.x + dragMouseDelta.x * scaleSpeed);
                    newScale.y = std::max(0.01f, dragStartObjectScale_.y - dragMouseDelta.y * scaleSpeed);
                    break;
                case ViewType::Left:
                case ViewType::Right:
                    newScale.z = std::max(0.01f, dragStartObjectScale_.z + dragMouseDelta.x * scaleSpeed);
                    newScale.y = std::max(0.01f, dragStartObjectScale_.y - dragMouseDelta.y * scaleSpeed);
                    break;
                case ViewType::Perspective:
                    break;
                }
                object.scale = newScale;
                sceneDirty_ = true;
            }
            else if (dragTool_ == DragTool::Rotate)
            {
                glm::vec3 newRotation = dragStartObjectRotation_;
                const float rotationSpeed = 0.5f;
                switch (dragViewType_)
                {
                case ViewType::Top:
                case ViewType::Bottom:
                    newRotation.y = dragStartObjectRotation_.y + dragMouseDelta.x * rotationSpeed;
                    break;
                case ViewType::Front:
                case ViewType::Back:
                    newRotation.z = dragStartObjectRotation_.z + dragMouseDelta.x * rotationSpeed;
                    break;
                case ViewType::Left:
                case ViewType::Right:
                    newRotation.x = dragStartObjectRotation_.x + dragMouseDelta.x * rotationSpeed;
                    break;
                case ViewType::Perspective:
                    break;
                }
                object.rotationEuler = newRotation;
                sceneDirty_ = true;
            }
        }
        else
        {
            draggingObjectInView_ = false;
            dragTool_ = DragTool::None;
            dragUndoPushed_ = false;
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing() &&
        hovered->type == ViewType::Perspective)
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
            }
            else if (shiftDown)
            {
                if (!IsMeshSelected(picked))
                    selectedMeshIndices_.push_back(picked);
                std::sort(selectedMeshIndices_.begin(), selectedMeshIndices_.end());
                selectedMeshIndex_ = picked;
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
        }
    }

    if (!boxSelecting_ &&
        currentTool_ == Tool::Select &&
        hovered->type != ViewType::Perspective &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
        !selectedVertexIndices_.empty() &&
        !ctrlDown &&
        !shiftDown &&
        leftDragging)
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
            hovered->perspectiveDistance = std::clamp(hovered->perspectiveDistance - wheel * 32.0f, 64.0f, 4096.0f);
        else
            hovered->orthoSize = std::clamp(hovered->orthoSize - wheel * (hovered->orthoSize * 0.1f), 8.0f, 4096.0f);
    }

    if (hovered->type == ViewType::Perspective &&
        shiftDown &&
        leftDragging &&
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
        leftDragging &&
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
        if (ImGui::MenuItem("Show Grid", nullptr, showGrid_))
            showGrid_ = !showGrid_;
        if (ImGui::MenuItem("Snap", nullptr, snapEnabled_))
            snapEnabled_ = !snapEnabled_;
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
    if (selectedMeshIndex_ < 0 || selectedMeshIndex_ >= static_cast<int>(scene_.meshObjects().size()))
    {
        gizmoWasUsing_ = false;
        return;
    }

    int perspectiveIndex = -1;
    if (activeViewIndex_ >= 0 &&
        activeViewIndex_ < activeViewCount_ &&
        views_[activeViewIndex_].type == ViewType::Perspective)
    {
        perspectiveIndex = activeViewIndex_;
    }
    else for (int i = 0; i < activeViewCount_; ++i)
    {
        if (views_[i].type == ViewType::Perspective)
        {
            perspectiveIndex = i;
            break;
        }
    }
    if (perspectiveIndex < 0)
    {
        gizmoWasUsing_ = false;
        return;
    }

    LevelEditorView& view = views_[perspectiveIndex];
    if (view.rect.w <= 0 || view.rect.h <= 0)
    {
        gizmoWasUsing_ = false;
        return;
    }

    LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];
    glm::mat4 gizmoMatrix = meshObjectPivotFrameMatrix(meshObject);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (currentTool_ == Tool::Rotate)
        operation = ImGuizmo::ROTATE;
    else if (currentTool_ == Tool::Scale)
        operation = ImGuizmo::SCALE;

    ImGuizmo::SetOrthographic(false);
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
        PushUndoState();

    if (usingNow)
    {
        float t[3] = {0.0f, 0.0f, 0.0f};
        float r[3] = {0.0f, 0.0f, 0.0f};
        float s[3] = {1.0f, 1.0f, 1.0f};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);
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

    gizmoWasUsing_ = usingNow;
}

void LevelEditorApp::DrawViewTile(const LevelEditorView& view, ImDrawList* drawList) const
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
    drawList->AddText(ImVec2(minPos.x + 10.0f, minPos.y + 6.0f), IM_COL32(235, 240, 245, 255), view.label);

    // Content clip excludes header to prevent bleed into title bar.
    const ImVec2 contentMin(minPos.x, minPos.y + headerHeight);
    const ImVec2 contentMax(maxPos.x, maxPos.y);
    drawList->PushClipRect(contentMin, contentMax, true);

    if (showGrid_)
    {
        if (view.type == ViewType::Perspective)
        {
            auto projectGridPoint = [&](const glm::vec3& world, ImVec2& outPoint) -> bool
            {
                const glm::vec4 clip = view.camera.viewProjection * glm::vec4(world, 1.0f);
                if (std::fabs(clip.w) <= 1e-6f)
                    return false;
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (ndc.z < -1.0f || ndc.z > 1.0f)
                    return false;
                const float nx = (ndc.x * 0.5f) + 0.5f;
                const float ny = 1.0f - ((ndc.y * 0.5f) + 0.5f);
                outPoint.x = minPos.x + nx * (maxPos.x - minPos.x);
                outPoint.y = (minPos.y + headerHeight) + ny * (maxPos.y - (minPos.y + headerHeight));
                return std::isfinite(outPoint.x) && std::isfinite(outPoint.y);
            };

            const float step = std::max(1.0f, gridSize_);
            const float extent = std::max(256.0f, view.perspectiveDistance * 1.6f);
            const float centerX = std::round(view.focus.x / step) * step;
            const float centerZ = std::round(view.focus.z / step) * step;
            const int lineCount = std::max(8, static_cast<int>(extent / step));

            for (int i = -lineCount; i <= lineCount; ++i)
            {
                const float offset = static_cast<float>(i) * step;
                const bool major = (i % 4) == 0;
                const ImU32 color = major ? IM_COL32(52, 62, 78, 220) : IM_COL32(40, 48, 60, 190);

                ImVec2 a0, a1;
                const glm::vec3 p0(centerX + offset, 0.0f, centerZ - extent);
                const glm::vec3 p1(centerX + offset, 0.0f, centerZ + extent);
                if (projectGridPoint(p0, a0) && projectGridPoint(p1, a1))
                    drawList->AddLine(a0, a1, color, 1.0f);

                ImVec2 b0, b1;
                const glm::vec3 q0(centerX - extent, 0.0f, centerZ + offset);
                const glm::vec3 q1(centerX + extent, 0.0f, centerZ + offset);
                if (projectGridPoint(q0, b0) && projectGridPoint(q1, b1))
                    drawList->AddLine(b0, b1, color, 1.0f);
            }

            // World axes at origin on the ground plane.
            ImVec2 x0, x1, z0, z1;
            if (projectGridPoint(glm::vec3(-extent, 0.0f, 0.0f), x0) &&
                projectGridPoint(glm::vec3(extent, 0.0f, 0.0f), x1))
            {
                drawList->AddLine(x0, x1, IM_COL32(220, 90, 90, 210), 1.6f);
            }
            if (projectGridPoint(glm::vec3(0.0f, 0.0f, -extent), z0) &&
                projectGridPoint(glm::vec3(0.0f, 0.0f, extent), z1))
            {
                drawList->AddLine(z0, z1, IM_COL32(80, 120, 240, 210), 1.6f);
            }
        }
        else
        {
            const float viewportW = maxPos.x - minPos.x;
            const float viewportH = maxPos.y - (minPos.y + headerHeight);
            const float aspect = viewportH > 0.0f ? (viewportW / viewportH) : 1.0f;
            const float halfH = view.orthoSize;
            const float halfW = halfH * aspect;
            const float pixelsPerWorld = viewportH / (2.0f * halfH);
            const float worldStep = niceGridStep(24.0f / std::max(1e-4f, pixelsPerWorld));
            const float majorWorldStep = worldStep * 4.0f;

            auto toScreen = [&](float worldA, float worldB) -> ImVec2
            {
                switch (view.type)
                {
                case ViewType::Top:
                    return ImVec2(
                        minPos.x + ((worldA - (view.focus.x - halfW)) / (2.0f * halfW)) * viewportW,
                        minPos.y + headerHeight + ((view.focus.z + halfH - worldB) / (2.0f * halfH)) * viewportH);
                case ViewType::Bottom:
                    return ImVec2(
                        minPos.x + ((worldA - (view.focus.x - halfW)) / (2.0f * halfW)) * viewportW,
                        minPos.y + headerHeight + ((worldB - (view.focus.z - halfH)) / (2.0f * halfH)) * viewportH);
                case ViewType::Front:
                    return ImVec2(
                        minPos.x + ((worldA - (view.focus.x - halfW)) / (2.0f * halfW)) * viewportW,
                        minPos.y + headerHeight + ((view.focus.y + halfH - worldB) / (2.0f * halfH)) * viewportH);
                case ViewType::Back:
                    return ImVec2(
                        minPos.x + (((view.focus.x + halfW) - worldA) / (2.0f * halfW)) * viewportW,
                        minPos.y + headerHeight + ((view.focus.y + halfH - worldB) / (2.0f * halfH)) * viewportH);
                case ViewType::Left:
                    return ImVec2(
                        minPos.x + (((view.focus.z + halfW) - worldA) / (2.0f * halfW)) * viewportW,
                        minPos.y + headerHeight + ((view.focus.y + halfH - worldB) / (2.0f * halfH)) * viewportH);
                case ViewType::Right:
                    return ImVec2(
                        minPos.x + ((worldA - (view.focus.z - halfW)) / (2.0f * halfW)) * viewportW,
                        minPos.y + headerHeight + ((view.focus.y + halfH - worldB) / (2.0f * halfH)) * viewportH);
                case ViewType::Perspective:
                    break;
                }
                return ImVec2(minPos.x, minPos.y + headerHeight);
            };

            const bool xIsA = (view.type == ViewType::Top || view.type == ViewType::Bottom ||
                               view.type == ViewType::Front || view.type == ViewType::Back);
            const bool zIsB = (view.type == ViewType::Top || view.type == ViewType::Bottom);
            const float minA = xIsA ? (view.focus.x - halfW) : (view.focus.z - halfW);
            const float maxA = xIsA ? (view.focus.x + halfW) : (view.focus.z + halfW);
            const float minB = zIsB ? (view.focus.z - halfH) : (view.focus.y - halfH);
            const float maxB = zIsB ? (view.focus.z + halfH) : (view.focus.y + halfH);

            const float startA = std::floor(minA / worldStep) * worldStep;
            const float startB = std::floor(minB / worldStep) * worldStep;
            for (float a = startA; a <= maxA + worldStep; a += worldStep)
            {
                const bool major = std::fmod(std::fabs(a), majorWorldStep) < 0.001f;
                const ImU32 color = major ? IM_COL32(52, 62, 78, 255) : IM_COL32(40, 48, 60, 255);
                ImVec2 p0 = toScreen(a, minB);
                ImVec2 p1 = toScreen(a, maxB);
                drawList->AddLine(p0, p1, color);
            }
            for (float b = startB; b <= maxB + worldStep; b += worldStep)
            {
                const bool major = std::fmod(std::fabs(b), majorWorldStep) < 0.001f;
                const ImU32 color = major ? IM_COL32(52, 62, 78, 255) : IM_COL32(40, 48, 60, 255);
                ImVec2 p0 = toScreen(minA, b);
                ImVec2 p1 = toScreen(maxA, b);
                drawList->AddLine(p0, p1, color);
            }

            ImVec2 axisOrigin = toScreen(0.0f, 0.0f);
            drawList->AddLine(ImVec2(minPos.x, axisOrigin.y), ImVec2(maxPos.x, axisOrigin.y), IM_COL32(80, 120, 240, 180), 1.5f);
            drawList->AddLine(ImVec2(axisOrigin.x, minPos.y + headerHeight), ImVec2(axisOrigin.x, maxPos.y), IM_COL32(220, 90, 90, 180), 1.5f);
        }
    }

    struct Tri2D
    {
        ImVec2 a;
        ImVec2 b;
        ImVec2 c;
        float depth = 0.0f;
        ImU32 color = IM_COL32(120, 200, 255, 120);
    };
    std::vector<Tri2D> triangles;
    triangles.reserve(256);

    auto projectWorld = [&](const glm::vec3& world, ImVec2& outPoint, float& outDepth) -> bool
    {
        return ProjectWorldToView(view, world, outPoint, outDepth);
    };

    for (std::size_t objectIndex = 0; objectIndex < scene_.meshObjects().size(); ++objectIndex)
    {
        const LevelMeshObject& object = scene_.meshObjects()[objectIndex];
        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
        const bool objectSelected = IsMeshSelected(static_cast<int>(objectIndex));
        const ImU32 edgeColor = objectSelected ? IM_COL32(220, 235, 255, 255) : IM_COL32(120, 210, 255, 220);

        for (std::size_t faceIndex = 0; faceIndex < object.mesh.faces().size(); ++faceIndex)
        {
            const EditableFace& face = object.mesh.faces()[faceIndex];
            if (face.indices.size() < 3)
                continue;

            std::vector<ImVec2> poly;
            std::vector<float> depths;
            poly.reserve(face.indices.size());
            depths.reserve(face.indices.size());

            bool validFace = true;
            for (int index : face.indices)
            {
                if (index < 0 || index >= static_cast<int>(object.mesh.vertices().size()))
                {
                    validFace = false;
                    break;
                }

                ImVec2 p;
                float depth = 0.0f;
                const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[index].position, 1.0f));
                if (!projectWorld(world, p, depth))
                {
                    validFace = false;
                    break;
                }
                if (p.y < minPos.y + headerHeight - 1.0f || p.y > maxPos.y + 1.0f)
                {
                    validFace = false;
                    break;
                }
                poly.push_back(p);
                depths.push_back(depth);
            }

            if (!validFace || poly.size() < 3)
                continue;

            const std::size_t h = std::hash<std::string>{}(face.materialName);
            const int r = 80 + static_cast<int>((h >> 0) & 0x7F);
            const int g = 90 + static_cast<int>((h >> 8) & 0x7F);
            const int b = 100 + static_cast<int>((h >> 16) & 0x7F);
            const int alpha = (view.type == ViewType::Perspective && useTransparency_)
                ? static_cast<int>(glm::clamp(transparency_ * 255.0f, 0.0f, 255.0f))
                : 120;
            const bool faceSelected = selectionMode_ == SelectionMode::Face &&
                                      static_cast<int>(objectIndex) == selectedMeshIndex_ &&
                                      static_cast<int>(faceIndex) == selectedFaceIndex_;
            const ImU32 fillColor = faceSelected ? IM_COL32(255, 185, 80, std::max(alpha, 180)) : IM_COL32(r, g, b, alpha);

            for (std::size_t i = 1; i + 1 < poly.size(); ++i)
            {
                Tri2D tri;
                tri.a = poly[0];
                tri.b = poly[i];
                tri.c = poly[i + 1];
                tri.depth = (depths[0] + depths[i] + depths[i + 1]) / 3.0f;
                tri.color = fillColor;
                triangles.push_back(tri);
            }

            (void)edgeColor;
        }
    }

    if (!triangles.empty())
    {
        std::sort(triangles.begin(), triangles.end(), [](const Tri2D& a, const Tri2D& b)
        {
            return a.depth > b.depth;
        });

        for (const Tri2D& tri : triangles)
        {
            drawList->AddTriangleFilled(tri.a, tri.b, tri.c, tri.color);
            const ImVec2 l1a(tri.a.x + (tri.b.x - tri.a.x) * 0.35f, tri.a.y + (tri.b.y - tri.a.y) * 0.35f);
            const ImVec2 l1b(tri.a.x + (tri.c.x - tri.a.x) * 0.35f, tri.a.y + (tri.c.y - tri.a.y) * 0.35f);
            drawList->AddLine(l1a, l1b, IM_COL32(235, 240, 245, 36), 1.0f);
        }
    }

    for (std::size_t objectIndex = 0; objectIndex < scene_.meshObjects().size(); ++objectIndex)
    {
        const LevelMeshObject& object = scene_.meshObjects()[objectIndex];
        const glm::mat4 modelMatrix = meshObjectModelMatrix(object);
        const bool objectSelected = IsMeshSelected(static_cast<int>(objectIndex));
        for (std::size_t faceIndex = 0; faceIndex < object.mesh.faces().size(); ++faceIndex)
        {
            const EditableFace& face = object.mesh.faces()[faceIndex];
            if (face.indices.size() < 2)
                continue;

            const bool faceSelected = selectionMode_ == SelectionMode::Face &&
                                      static_cast<int>(objectIndex) == selectedMeshIndex_ &&
                                      static_cast<int>(faceIndex) == selectedFaceIndex_;
            const ImU32 edgeColor = faceSelected ? IM_COL32(255, 235, 90, 255) :
                                   objectSelected ? IM_COL32(220, 235, 255, 255) :
                                                    IM_COL32(120, 210, 255, 220);
            const float edgeThickness = faceSelected ? 2.2f : 1.2f;

            std::vector<ImVec2> poly;
            poly.reserve(face.indices.size());
            bool validFace = true;
            for (int index : face.indices)
            {
                if (index < 0 || index >= static_cast<int>(object.mesh.vertices().size()))
                {
                    validFace = false;
                    break;
                }
                ImVec2 p;
                float depth = 0.0f;
                const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[index].position, 1.0f));
                if (!projectWorld(world, p, depth))
                {
                    validFace = false;
                    break;
                }
                poly.push_back(p);
            }
            if (!validFace || poly.size() < 2)
                continue;

            for (std::size_t i = 0; i < poly.size(); ++i)
            {
                const std::size_t next = (i + 1) % poly.size();
                drawList->AddLine(poly[i], poly[next], edgeColor, edgeThickness);
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
        struct DrawVertex
        {
            int index = -1;
            ImVec2 point = ImVec2(0.0f, 0.0f);
            float depth = 0.0f;
        };
        std::vector<DrawVertex> drawVertices;
        drawVertices.reserve(object.mesh.vertices().size());
        for (int vertexIndex = 0; vertexIndex < static_cast<int>(object.mesh.vertices().size()); ++vertexIndex)
        {
            ImVec2 point;
            float depth = 0.0f;
            const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(object.mesh.vertices()[(size_t)vertexIndex].position, 1.0f));
            if (!projectWorld(world, point, depth))
                continue;
            drawVertices.push_back({vertexIndex, point, depth});
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
        drawList->AddText(ImVec2(minPos.x + 10.0f, maxPos.y - 22.0f), IM_COL32(140, 150, 162, 255), "Shift+drag orbit  Ctrl+drag pan  Wheel zoom");
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
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Level"))
        {
            PushUndoState();
            scene_.reset();
            SyncSelectedMeshes();
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
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack_.empty()))
            PerformUndo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack_.empty()))
            PerformRedo();
        ImGui::Separator();
        ImGui::MenuItem("Duplicate", "Ctrl+D", false, false);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Show Grid", nullptr, &showGrid_);
        ImGui::MenuItem("Snap Enabled", nullptr, &snapEnabled_);
        ImGui::Separator();
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
        bool dark = theme_ == LevelEditorTheme::Dark;
        if (ImGui::MenuItem("Dark", nullptr, &dark))
        {
            theme_ = LevelEditorTheme::Dark;
            applyLevelEditorTheme(theme_);
        }

        bool studio = theme_ == LevelEditorTheme::Studio;
        if (ImGui::MenuItem("Studio", nullptr, &studio))
        {
            theme_ = LevelEditorTheme::Studio;
            applyLevelEditorTheme(theme_);
        }
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

    if (ImGui::CollapsingHeader("Tools", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginCombo("Selection Mode", selectionModeName(selectionMode_)))
        {
            const SelectionMode modes[] = {
                SelectionMode::Object,
                SelectionMode::Face,
                SelectionMode::Edge,
                SelectionMode::Vertex
            };
            for (SelectionMode mode : modes)
            {
                bool selected = mode == selectionMode_;
                if (ImGui::Selectable(selectionModeName(mode), selected))
                    selectionMode_ = mode;
            }
            ImGui::EndCombo();
        }

        ImGui::Checkbox("Show Grid", &showGrid_);
        ImGui::Checkbox("Snap", &snapEnabled_);
        ImGui::Checkbox("Transparency", &useTransparency_);
        ImGui::SliderFloat("Opacity", &transparency_, 0.0f, 1.0f, "%.2f");
        if (selectionMode_ == SelectionMode::Vertex)
            ImGui::Checkbox("Front Vertices Only", &vertexFrontOnly_);
        ImGui::DragFloat("Grid Size", &gridSize_, 1.0f, 1.0f, 256.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Mesh Objects", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& meshObjects = scene_.meshObjects();
        for (int i = 0; i < static_cast<int>(meshObjects.size()); ++i)
        {
            const bool selected = IsMeshSelected(i);
            if (ImGui::Selectable(meshObjects[i].name.c_str(), selected))
                SetSingleSelectedMesh(i);
        }
        if (ImGui::Button("Add Box"))
        {
            PushUndoState();
            LevelMeshObject object;
            object.name = "Brush " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
            scene_.meshObjects().push_back(object);
            SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
        }
    }

    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& entities = scene_.entities();
        for (int i = 0; i < static_cast<int>(entities.size()); ++i)
        {
            const bool selected = i == selectedEntityIndex_;
            if (ImGui::Selectable(entities[i].name.c_str(), selected))
                selectedEntityIndex_ = i;
        }

        if (ImGui::Button("Add Light"))
        {
            PushUndoState();
            LevelEntityObject entity;
            entity.name = "Light";
            entity.type = LevelEntityType::Light;
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Door"))
        {
            PushUndoState();
            LevelEntityObject entity;
            entity.name = "Door";
            entity.type = LevelEntityType::Door;
            scene_.entities().push_back(entity);
            selectedEntityIndex_ = static_cast<int>(scene_.entities().size()) - 1;
        }
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
    if (ImGui::BeginCombo("Layout##ViewLayout", (viewLayout_ == ViewLayout::Two) ? "2 Views" : (viewLayout_ == ViewLayout::Three) ? "3 Views" : "4 Views"))
    {
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
    const bool viewportHovered = ImGui::IsMouseHoveringRect(viewportMin, viewportMax);
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

    if (ImGui::CollapsingHeader("Level Stats", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        ImGui::CollapsingHeader("Selected Mesh", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("SelectedMeshSection");
        bool closeInspectorAfterRebuild = false;
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
        ImGui::TextDisabled("Pivot agora desloca sem mexer no visual. Este painel transforma a mesh local nos vertices.");
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
                selectedVertexIndices_.clear();
                sceneDirty_ = true;
            }
        }
        ImGui::TextDisabled("Cria uma casca fechada para ver a box por dentro e por fora.");
        ImGui::Text("Vertices: %d", static_cast<int>(meshObject.mesh.vertexCount()));
        ImGui::Text("Selected Vertices: %d", static_cast<int>(selectedVertexIndices_.size()));
        ImGui::Text("Faces: %d", static_cast<int>(meshObject.mesh.faceCount()));

        if (selectedFaceIndex_ >= 0 && selectedFaceIndex_ < static_cast<int>(meshObject.mesh.faceCount()))
        {
            ImGui::Separator();
            ImGui::Text("Selected Face: %d", selectedFaceIndex_);
            ImGui::DragFloat("Extrude Distance##FaceExtrudeDistance", &faceExtrudeDistance_, 0.5f, -1024.0f, 1024.0f);
            if (ImGui::Button("Extrude Face"))
            {
                if (std::fabs(faceExtrudeDistance_) > 1e-4f)
                {
                    PushUndoState();
                    if (extrudeMeshFace(meshObject, selectedFaceIndex_, faceExtrudeDistance_))
                        sceneDirty_ = true;
                }
            }
            ImGui::TextDisabled("Extrude empurra a face ao longo da normal local e cria as faces laterais.");
        }

        if (ImGui::CollapsingHeader("CSG", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (selectedMeshIndices_.size() >= 2)
            {
                const int cutterIndex = selectedMeshIndices_[0] == selectedMeshIndex_
                    ? selectedMeshIndices_[1]
                    : selectedMeshIndices_[0];
                if (cutterIndex >= 0 && cutterIndex < static_cast<int>(scene_.meshObjects().size()))
                {
                    ImGui::Text("CSG Between Meshes");
                    ImGui::TextDisabled("Primary = target, secondary = cutter. Usa box CSG estavel para blockout.");

                    if (ImGui::Button("Subtract"))
                    {
                        PushUndoState();

                        std::vector<LevelMeshObject>& meshObjects = scene_.meshObjects();
                        LevelMeshObject targetTemplate = meshObjects[(size_t)selectedMeshIndex_];
                        const LevelMeshObject cutterObject = meshObjects[(size_t)cutterIndex];
                        const LevelCSGBox targetBox = editableMeshWorldBox(targetTemplate);
                        const LevelCSGBox cutterBox = editableMeshWorldBox(cutterObject);
                        const std::vector<LevelCSGBox> pieces = subtractCSGBoxes(targetBox, cutterBox);

                        if (!pieces.empty())
                        {
                            meshObjects[(size_t)selectedMeshIndex_] = makeLevelObjectFromCSGBox(targetTemplate, pieces[0], targetTemplate.name);

                            for (std::size_t i = 1; i < pieces.size(); ++i)
                            {
                                LevelMeshObject pieceObject = makeLevelObjectFromCSGBox(
                                    targetTemplate,
                                    pieces[i],
                                    targetTemplate.name + " part " + std::to_string(i + 1));
                                meshObjects.insert(meshObjects.begin() + selectedMeshIndex_ + static_cast<int>(i), pieceObject);
                            }

                            selectedFaceIndex_ = -1;
                            selectedVertexIndices_.clear();
                            sceneDirty_ = true;
                            SyncSelectedMeshes();
                            closeInspectorAfterRebuild = true;
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Intersect"))
                    {
                        PushUndoState();

                        LevelMeshObject& targetObject = scene_.meshObjects()[selectedMeshIndex_];
                        const LevelMeshObject& cutterObject = scene_.meshObjects()[cutterIndex];
                        const std::vector<LevelCSGBox> pieces = intersectCSGBoxes(
                            editableMeshWorldBox(targetObject),
                            editableMeshWorldBox(cutterObject));

                        if (!pieces.empty())
                        {
                            targetObject = makeLevelObjectFromCSGBox(targetObject, pieces[0], targetObject.name);
                            selectedFaceIndex_ = -1;
                            selectedVertexIndices_.clear();
                            sceneDirty_ = true;
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Add"))
                    {
                        PushUndoState();

                        const LevelMeshObject duplicate = scene_.meshObjects()[cutterIndex];
                        scene_.meshObjects().push_back(duplicate);
                        sceneDirty_ = true;
                        SyncSelectedMeshes();
                        closeInspectorAfterRebuild = true;
                    }

                    if (ImGui::CollapsingHeader("Experimental Mesh CSG", ImGuiTreeNodeFlags_None))
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
                                selectedVertexIndices_.clear();
                                sceneDirty_ = true;
                            }
                        }
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("Seleciona pelo menos 2 meshes para usar Union / Difference / Intersection.");
            }

            if (ImGui::CollapsingHeader("Plane Clip", ImGuiTreeNodeFlags_None))
            {
                ImGui::TextDisabled("Opcao util para cortar uma mesh por plano local.");
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
                ImGui::Checkbox("Keep Front Half-Space##CSGKeepFront", &csgClipKeepFront_);
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
                        selectedVertexIndices_.clear();
                        sceneDirty_ = true;
                    }
                }
                ImGui::TextDisabled("Base de CSG: clip por plano local com cap simples para loops de corte principais.");
            }
        }

        if (closeInspectorAfterRebuild)
        {
            ImGui::PopID();
            ImGui::End();
            return;
        }

        if (ImGui::BeginTable("FacesTable##Mesh", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Face");
            ImGui::TableSetupColumn("Material");
            ImGui::TableHeadersRow();
            int faceIndex = 0;
            for (const EditableFace& face : meshObject.mesh.faces())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const int rowFaceIndex = faceIndex++;
                const bool rowSelected = rowFaceIndex == selectedFaceIndex_;
                if (ImGui::Selectable(("Face " + std::to_string(rowFaceIndex)).c_str(), rowSelected, ImGuiSelectableFlags_SpanAllColumns))
                    selectedFaceIndex_ = rowFaceIndex;
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(face.materialName.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }

    if (selectedEntityIndex_ >= 0 && selectedEntityIndex_ < static_cast<int>(scene_.entities().size()) &&
        ImGui::CollapsingHeader("Selected Entity", ImGuiTreeNodeFlags_DefaultOpen))
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
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen))
    {
        TextureManager& textureManager = TextureManager::instance();
        Texture* white = textureManager.getWhite();
        Texture* texture = white;
        if (!currentTexturePath_.empty())
        {
            const std::string texName = "level_current_texture::" + currentTexturePath_;
            texture = textureManager.get(texName);
            if (!texture)
                texture = textureManager.load(texName, currentTexturePath_);
            if (!texture)
                texture = white;
        }

        ImGui::Text("Selected:");
        ImGui::SameLine();
        ImGui::TextUnformatted(currentTexturePath_.empty() ? "(none)" : currentTexturePath_.c_str());
        ImGui::Image((ImTextureID)(intptr_t)texture->id, ImVec2(140.0f, 140.0f));
        ImGui::DragFloat2("Move UV", &currentUvOffset_.x, 0.01f, -1000.0f, 1000.0f, "%.2f");
        ImGui::DragFloat2("Tile UV", &currentUvScale_.x, 0.01f, 0.01f, 128.0f, "%.2f");
        ImGui::DragFloat("Rotate UV", &currentUvRotation_, 0.1f, -360.0f, 360.0f, "%.1f");
        if (ImGui::Button("Reset UV"))
        {
            currentUvOffset_ = glm::vec2(0.0f);
            currentUvScale_ = glm::vec2(1.0f);
            currentUvRotation_ = 0.0f;
        }
    }

    if (ImGui::CollapsingHeader("History", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (ImGui::CollapsingHeader("Roadmap", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BulletText("Create box -> render real mesh");
        ImGui::BulletText("Object / Face / Edge / Vertex selection");
        ImGui::BulletText("Extrude, split, clip, bevel");
        ImGui::BulletText("Entities with editable properties");
        ImGui::BulletText("CSG as a higher-level operation");
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
        ImGui::Text("Current: %s", currentTexturePath_.c_str());

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
        ImGui::Text("Frame: %.2f ms", deltaTime * 1000.0f);
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
