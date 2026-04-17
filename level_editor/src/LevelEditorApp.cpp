#include "LevelEditorApp.hpp"

#include "Input.hpp"
#include "LevelEditorSceneIO.hpp"
#include "Manager.hpp"
#include "CSG.hpp"
#include "MeshCSG.hpp"
#include "Batch.hpp"
#include "RenderTarget.hpp"
#include "RenderState.hpp"
#include "Opengl.hpp"
#include "Material.hpp"
#include "Utils.hpp"
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

    // Weld vertices by position to reduce duplicates
    std::map<std::tuple<int,int,int>, int> positionMap;
    auto getOrAddVertex = [&](const glm::vec3& pos) -> int
    {
        const float scale = 1000.0f;
        auto key = std::make_tuple(
            static_cast<int>(std::round(pos.x * scale)),
            static_cast<int>(std::round(pos.y * scale)),
            static_cast<int>(std::round(pos.z * scale)));
        auto it = positionMap.find(key);
        if (it != positionMap.end())
            return it->second;
        int idx = static_cast<int>(vertices.size());
        EditableVertex v;
        v.position = pos;
        // normal and uv set by caller after getOrAddVertex
        vertices.push_back(v);
        positionMap[key] = idx;
        return idx;
    };

    auto setVertexAttribs = [&](int idx, const glm::vec3& normal, const glm::vec2& uv)
    {
        if (idx >= 0 && idx < static_cast<int>(vertices.size()))
        {
            vertices[(size_t)idx].normal = normal;
            vertices[(size_t)idx].uv = uv;
        }
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
                // Prefer the albedo texture name as materialName — the TextureManager
                // keeps it cached, so the renderer can find it by this key.
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
                bool valid = true;
                for (int k = 0; k < 3; ++k)
                {
                    const uint32_t index = mesh.buffer.indices[i + k];
                    if (index >= mesh.buffer.vertices.size())
                    { valid = false; break; }
                    const Vertex& sv = mesh.buffer.vertices[index];
                    const glm::vec3 pos = glm::vec3(transform * glm::vec4(sv.position, 1.0f));
                    const int vi = getOrAddVertex(pos);
                    setVertexAttribs(vi, glm::normalize(glm::mat3(transform) * sv.normal), sv.uv);
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
            bool valid = true;
            for (int k = 0; k < 3; ++k)
            {
                const uint32_t index = mesh.buffer.indices[i + k];
                if (index >= mesh.buffer.vertices.size())
                { valid = false; break; }
                const Vertex& sv = mesh.buffer.vertices[index];
                const glm::vec3 pos = glm::vec3(transform * glm::vec4(sv.position, 1.0f));
                const int vi = getOrAddVertex(pos);
                setVertexAttribs(vi, glm::normalize(glm::mat3(transform) * sv.normal), sv.uv);
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
                if (absN.y >= absN.x && absN.y >= absN.z)
                    uv = glm::vec2(ev.position.x, ev.position.z);
                else if (absN.x >= absN.y && absN.x >= absN.z)
                    uv = glm::vec2(ev.position.z, ev.position.y);
                else
                    uv = glm::vec2(ev.position.x, ev.position.y);

                uv *= face.uvScale * 0.01f;
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

static const char* kSettingsPath = "level_editor_settings.json";

void LevelEditorApp::SaveEditorSettings()
{
    nlohmann::json j;
    j["assetRoot"] = assetRoot_;
    j["scenePath"] = scenePath_;
    j["viewLayout"] = static_cast<int>(viewLayout_);
    j["currentTexturePath"] = currentTexturePath_;
    j["lastImportDir"] = lastImportDir_;
    j["assetViewMode"] = static_cast<int>(assetViewMode_);
    j["theme"] = static_cast<int>(theme_);
    j["showGrid"] = showGrid_;
    j["snapEnabled"] = snapEnabled_;
    j["gridSize"] = gridSize_;
    j["perspGridSize"] = perspGridSize_;
    j["useTransparency"] = useTransparency_;
    j["transparency"] = transparency_;
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
    if (j.contains("assetViewMode"))
    {
        const int av = j["assetViewMode"].get<int>();
        if (av >= 0 && av <= 2) assetViewMode_ = static_cast<AssetViewMode>(av);
    }
    if (j.contains("theme"))
    {
        const int th = j["theme"].get<int>();
        if (th >= 0 && th <= 1) { theme_ = static_cast<LevelEditorTheme>(th); applyLevelEditorTheme(theme_); }
    }
    if (j.contains("showGrid")) showGrid_ = j["showGrid"].get<bool>();
    if (j.contains("snapEnabled")) snapEnabled_ = j["snapEnabled"].get<bool>();
    if (j.contains("gridSize")) gridSize_ = j["gridSize"].get<float>();
    if (j.contains("perspGridSize")) perspGridSize_ = j["perspGridSize"].get<float>();
    if (j.contains("useTransparency")) useTransparency_ = j["useTransparency"].get<bool>();
    if (j.contains("transparency")) transparency_ = j["transparency"].get<float>();
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
}

LevelEditorApp::LevelEditorApp()
{
    applyLevelEditorTheme(theme_);
    InitializeViews();
    scenePath_ = "bin/level_scene.mred";
    LoadEditorSettings();

    // Auto-load last scene if path exists
    if (!scenePath_.empty() && std::filesystem::exists(scenePath_))
    {
        std::string error;
        if (loadLevelEditorScene(scenePath_, scene_, error))
            sceneStatusMessage_ = "Loaded: " + scenePath_;
        else
            sceneStatusMessage_ = "Failed to load: " + error;
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

    if (sceneDirty_)
        meshCacheValid_ = false;

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
                object.mesh = editable;
                scene_.meshObjects().push_back(object);
                SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
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
            selectedVertexIndices_.clear();
            selectedFaceIndex_ = -1;
            sceneDirty_ = true;
        }
        else if (selectionMode_ == SelectionMode::Face &&
                 selectedFaceIndex_ >= 0 &&
                 selectedFaceIndex_ < static_cast<int>(scene_.meshObjects()[(size_t)selectedMeshIndex_].mesh.faceCount()))
        {
            PushUndoState();
            LevelMeshObject& obj = scene_.meshObjects()[(size_t)selectedMeshIndex_];
            auto& faces = obj.mesh.facesMutable();
            faces.erase(faces.begin() + selectedFaceIndex_);

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
            selectedFaceIndex_ = -1;
            selectedVertexIndices_.clear();
            sceneDirty_ = true;
        }
        else if (selectionMode_ == SelectionMode::Object)
        {
            PushUndoState();
            std::vector<LevelMeshObject>& meshObjects = scene_.meshObjects();
            // Sort indices descending so erasing from the back doesn't shift earlier ones
            std::vector<int> toDelete = selectedMeshIndices_;
            if (toDelete.empty())
                toDelete.push_back(selectedMeshIndex_);
            std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());
            // Remove duplicates
            toDelete.erase(std::unique(toDelete.begin(), toDelete.end()), toDelete.end());
            for (int idx : toDelete)
            {
                if (idx >= 0 && idx < static_cast<int>(meshObjects.size()))
                    meshObjects.erase(meshObjects.begin() + idx);
            }
            selectedMeshIndex_ = -1;
            selectedMeshIndices_.clear();
            selectedFaceIndex_ = -1;
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
                glm::vec3 newLocal = glm::vec3(inverseModel * glm::vec4(movedWorld, 1.0f));
                if (snapEnabled_)
                {
                    newLocal.x = std::round(newLocal.x / gridSize_) * gridSize_;
                    newLocal.y = std::round(newLocal.y / gridSize_) * gridSize_;
                    newLocal.z = std::round(newLocal.z / gridSize_) * gridSize_;
                }
                object.mesh.verticesMutable()[(size_t)vertexIndex].position = newLocal;
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
                glm::vec3 newLocal = glm::vec3(inverseModel * glm::vec4(movedWorld, 1.0f));
                if (snapEnabled_)
                {
                    newLocal.x = std::round(newLocal.x / gridSize_) * gridSize_;
                    newLocal.y = std::round(newLocal.y / gridSize_) * gridSize_;
                    newLocal.z = std::round(newLocal.z / gridSize_) * gridSize_;
                }
                object.mesh.verticesMutable()[(size_t)vertexIndex].position = newLocal;
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
                glm::vec3 newPos = dragStartObjectPosition_ + objectDelta;
                if (snapEnabled_)
                {
                    newPos.x = std::round(newPos.x / gridSize_) * gridSize_;
                    newPos.y = std::round(newPos.y / gridSize_) * gridSize_;
                    newPos.z = std::round(newPos.z / gridSize_) * gridSize_;
                }
                object.position = newPos;
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
        hovered->type != ViewType::Perspective &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // Place vertex mode: click in ortho view = add vertex
        if (placeVertexMode_ && selectionMode_ == SelectionMode::Vertex &&
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

    // Build gizmo matrix from the selected object
    glm::mat4 gizmoMatrix(1.0f);
    if (hasMesh)
    {
        LevelMeshObject& meshObject = scene_.meshObjects()[selectedMeshIndex_];
        gizmoMatrix = meshObjectPivotFrameMatrix(meshObject);
    }
    else
    {
        LevelEntityObject& ent = scene_.entities()[selectedEntityIndex_];
        gizmoMatrix = glm::translate(glm::mat4(1.0f), ent.position);
    }

    // Entities only support Move (no rotate/scale)
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (hasMesh)
    {
        if (currentTool_ == Tool::Rotate)
            operation = ImGuizmo::ROTATE;
        else if (currentTool_ == Tool::Scale)
            operation = ImGuizmo::SCALE;
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
        PushUndoState();

    if (usingNow)
    {
        float t[3] = {0.0f, 0.0f, 0.0f};
        float r[3] = {0.0f, 0.0f, 0.0f};
        float s[3] = {1.0f, 1.0f, 1.0f};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);

        if (hasEntity)
        {
            // Entity: just update position
            LevelEntityObject& ent = scene_.entities()[selectedEntityIndex_];
            const glm::vec3 newPos(t[0], t[1], t[2]);
            if (newPos != ent.position)
            {
                ent.position = newPos;
                sceneDirty_ = true;
            }
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

    gizmoWasUsing_ = usingNow;
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

        // Lightmap setup
        solidShader_->setInt("u_useLightmap", (useLightmap_ && lightmapTexture_) ? 1 : 0);
        solidShader_->setInt("u_lightmap", 1); // texture unit 1
        if (useLightmap_ && lightmapTexture_)
            rs.bindTexture(1, GL_TEXTURE_2D, lightmapTexture_);

        const bool wireMode = view.renderMode == RenderMode::Wireframe;
        const bool texturedMode = view.renderMode == RenderMode::Textured;

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

            // Always try textured rendering (Solid mode uses textures when available)
            if (!wireMode)
            {
                TextureManager& texMgr = TextureManager::instance();
                bool anyTextured = false;

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
                                faceTex = texMgr.load(texName, range.materialName);
                                if (!faceTex)
                                    failedTextureLoads_.insert(texName);
                            }
                        }
                    }

                    if (faceTex && faceTex->id != 0)
                    {
                        solidShader_->setInt("u_useTexture", 1);
                        solidShader_->setInt("u_albedo", 0);
                        rs.bindTexture(0, GL_TEXTURE_2D, faceTex->id);
                        anyTextured = true;
                    }
                    else if (texturedMode)
                    {
                        solidShader_->setInt("u_useTexture", 1);
                        solidShader_->setInt("u_albedo", 0);
                        rs.bindTexture(0, GL_TEXTURE_2D, texMgr.getPattern()->id);
                    }
                    else
                    {
                        // Solid mode fallback: object hash color
                        solidShader_->setInt("u_useTexture", 0);
                        const std::size_t hv = std::hash<std::string>{}(object.name) ^ (objectIndex * 2654435761u);
                        const float r = (80 + static_cast<int>((hv >> 0) & 0x7F)) / 255.0f;
                        const float g = (90 + static_cast<int>((hv >> 8) & 0x7F)) / 255.0f;
                        const float b = (100 + static_cast<int>((hv >> 16) & 0x7F)) / 255.0f;
                        solidShader_->setVec4("u_color", glm::vec4(r, g, b, 1.0f));
                    }

                    cached.buffer.drawRange(range.indexStart, range.indexCount);
                }

                // Reset u_color after textured draws so subsequent code doesn't inherit stale alpha
                if (anyTextured)
                    solidShader_->setVec4("u_color", glm::vec4(1.0f));
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
            selectedFaceIndex_ >= 0 &&
            selectedFaceIndex_ < static_cast<int>(object.mesh.faces().size()))
        {
            const EditableFace& selFace = object.mesh.faces()[(size_t)selectedFaceIndex_];
            const auto& verts = object.mesh.vertices();

            // Selected face edges
            viewBatch_->SetColor(255, 235, 90, 255);
            for (std::size_t e = 0; e < selFace.indices.size(); ++e)
            {
                const int ci = selFace.indices[e];
                const int ni = selFace.indices[(e + 1) % selFace.indices.size()];
                if (ci >= 0 && ci < static_cast<int>(verts.size()) &&
                    ni >= 0 && ni < static_cast<int>(verts.size()))
                    viewBatch_->Line3D(verts[(size_t)ci].position, verts[(size_t)ni].position);
            }

            // Selected face fill
            if (selFace.indices.size() >= 3)
            {
                viewBatch_->SetColor(255, 186, 79, 153);
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

        viewBatch_->EndTransform();
        viewBatch_->Render();
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
        ImGui::Separator();
        if (ImGui::MenuItem("Import Mesh..."))
        {
            const std::filesystem::path startDir = lastImportDir_.empty()
                ? std::filesystem::current_path()
                : std::filesystem::path(lastImportDir_);
            importMeshDialog_.Open(ImGuiFileDialog::Mode::OpenFile, startDir, "mesh");
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
        ImGui::MenuItem("Duplicate", "Ctrl+D", false, false);
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
        ImGui::SetNextItemWidth(140.0f);
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

    if (ImGui::CollapsingHeader("Mesh Objects", ImGuiTreeNodeFlags_DefaultOpen))
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
                    SetSingleSelectedMesh(i);
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

        if (ImGui::Button("Add Box"))
        {
            PushUndoState();
            LevelMeshObject object;
            object.name = "Brush " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
            scene_.meshObjects().push_back(object);
            SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Empty"))
        {
            PushUndoState();
            LevelMeshObject object;
            object.name = "Mesh " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
            object.mesh = EditableMesh::FromData({}, {});
            scene_.meshObjects().push_back(object);
            SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
            selectionMode_ = SelectionMode::Vertex;
            placeVertexMode_ = true;
            sceneDirty_ = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Create Primitive");
        {
            const char* primNames[] = {"Box", "Cylinder", "Sphere", "Plane", "Wedge", "Stairs", "Spiral Stairs", "Text"};
            int primIdx = static_cast<int>(primitiveType_);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Type##Primitive", &primIdx, primNames, 8))
                primitiveType_ = static_cast<PrimitiveType>(primIdx);

            switch (primitiveType_)
            {
            case PrimitiveType::Box:
                ImGui::DragFloat3("Size##PrimBox", &primSize_.x, 1.0f, 1.0f, 4096.0f);
                break;
            case PrimitiveType::Cylinder:
                ImGui::DragFloat("Radius##PrimCyl", &primRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragFloat("Height##PrimCyl", &primHeight_, 1.0f, 1.0f, 4096.0f);
                ImGui::DragInt("Segments##PrimCyl", &primSegments_, 1, 3, 128);
                break;
            case PrimitiveType::Sphere:
                ImGui::DragFloat("Radius##PrimSph", &primRadius_, 1.0f, 1.0f, 2048.0f);
                ImGui::DragInt("Rings##PrimSph", &primRings_, 1, 2, 64);
                ImGui::DragInt("Segments##PrimSph", &primSegments_, 1, 3, 128);
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

                switch (primitiveType_)
                {
                case PrimitiveType::Box:
                    object.name = "Box " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeBox(-half, half);
                    break;
                case PrimitiveType::Cylinder:
                    object.name = "Cylinder " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeCylinder(glm::vec3(0.0f), primRadius_, primHeight_, primSegments_);
                    break;
                case PrimitiveType::Sphere:
                    object.name = "Sphere " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeSphere(glm::vec3(0.0f), primRadius_, primRings_, primSegments_);
                    break;
                case PrimitiveType::Plane:
                {
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
                    object.name = "Wedge " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeWedge(-half, half);
                    break;
                case PrimitiveType::Stairs:
                    object.name = "Stairs " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeStairs(-half, half, primStairSteps_);
                    break;
                case PrimitiveType::SpiralStairs:
                    object.name = "Spiral " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeSpiralStairs(glm::vec3(0.0f), primInnerRadius_, primOuterRadius_, primHeight_, primStairSteps_, primSpiralAngle_);
                    break;
                case PrimitiveType::Text:
                    object.name = "Text " + std::to_string(static_cast<int>(scene_.meshObjects().size()) + 1);
                    object.mesh = EditableMesh::MakeText(primText_, primFontPath_, primTextSize_, primTextExtrude_, primTextCurveQuality_);
                    break;
                }

                scene_.meshObjects().push_back(object);
                SetSingleSelectedMesh(static_cast<int>(scene_.meshObjects().size()) - 1);
                sceneDirty_ = true;
            }
        }
    }

    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& entities = scene_.entities();
        for (int i = 0; i < static_cast<int>(entities.size()); ++i)
        {
            const bool selected = i == selectedEntityIndex_;
            ImGui::PushID(i);
            if (ImGui::Selectable(entities[i].name.c_str(), selected))
                selectedEntityIndex_ = i;
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

        if (ImGui::CollapsingHeader("Mesh Edit", ImGuiTreeNodeFlags_DefaultOpen))
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
                        selectedFaceIndex_ = (selectedFaceIndex_ - 1 + faceCount) % faceCount;
                    ImGui::SameLine();
                    if (ImGui::Button(">##NextFace") && faceCount > 0)
                        selectedFaceIndex_ = (selectedFaceIndex_ + 1) % faceCount;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::SliderInt("##FaceSlider", &selectedFaceIndex_, 0, std::max(0, faceCount - 1), "Face %d");
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

            // UV operations — per-face if face selected, otherwise whole object
            ImGui::Separator();
            ImGui::TextUnformatted("UV Projection");
            const bool uvPerFace = (selectionMode_ == SelectionMode::Face && selectedFaceIndex_ >= 0
                && selectedFaceIndex_ < static_cast<int>(meshObject.mesh.faceCount()));
            if (uvPerFace)
                ImGui::TextDisabled("(applying to selected face)");
            else
                ImGui::TextDisabled("(applying to all faces)");

            if (ImGui::Button("Box Project UV"))
            {
                PushUndoState();
                auto applyBox = [](EditableFace& f) {
                    f.uvOffset = glm::vec2(0.0f);
                    f.uvScale = glm::vec2(1.0f);
                    f.uvRotation = 0.0f;
                };
                if (uvPerFace)
                    applyBox(meshObject.mesh.facesMutable()[(size_t)selectedFaceIndex_]);
                else
                    for (auto& f : meshObject.mesh.facesMutable()) applyBox(f);
                sceneDirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset UV"))
            {
                PushUndoState();
                auto applyReset = [](EditableFace& f) {
                    f.uvOffset = glm::vec2(0.0f);
                    f.uvScale = glm::vec2(1.0f);
                    f.uvRotation = 0.0f;
                };
                if (uvPerFace)
                    applyReset(meshObject.mesh.facesMutable()[(size_t)selectedFaceIndex_]);
                else
                    for (auto& f : meshObject.mesh.facesMutable()) applyReset(f);
                sceneDirty_ = true;
            }

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("CSG", ImGuiTreeNodeFlags_DefaultOpen))
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
                            selectedVertexIndices_.clear();
                            sceneDirty_ = true;
                            SyncSelectedMeshes();
                        }
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
                ImGui::TextDisabled("Select 2+ meshes for CSG");
            }

            if (ImGui::CollapsingHeader("Plane Clip", ImGuiTreeNodeFlags_None))
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
                        selectedVertexIndices_.clear();
                        sceneDirty_ = true;
                    }
                }
            }

            ImGui::PopID();
        }
    }

    // Reference Image Planes
    if (ImGui::CollapsingHeader("Reference Planes", ImGuiTreeNodeFlags_None))
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

    if (ImGui::CollapsingHeader("Lightmap", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (ImGui::CollapsingHeader("View Settings", ImGuiTreeNodeFlags_DefaultOpen))
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
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##PerspGridSlider", &perspGridSize_, 1.0f, 128.0f, "3D Grid: %.1f");

        ImGui::Checkbox("Transparency", &useTransparency_);
        if (useTransparency_)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##TransAlpha", &transparency_, 0.0f, 1.0f, "%.2f");
        }

        ImGui::Separator();
        ImGui::Text("Per-View Background");
        for (int i = 0; i < 4; ++i)
        {
            ImGui::PushID(i);
            ImGui::ColorEdit3(views_[i].label, &views_[i].clearColor.r, ImGuiColorEditFlags_NoInputs);
            ImGui::PopID();
            if (i % 2 == 0) ImGui::SameLine();
        }
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_None))
    {
        ImGui::Checkbox("Draw Normals", &debugDrawNormals_);
        ImGui::SameLine();
        ImGui::Checkbox("Draw Tangents", &debugDrawTangents_);
        if (debugDrawNormals_ || debugDrawTangents_)
            ImGui::DragFloat("Line Length", &debugNormalLength_, 0.5f, 1.0f, 100.0f);
    }

    if (selectedMeshIndex_ >= 0 && selectedMeshIndex_ < static_cast<int>(scene_.meshObjects().size()) &&
        ImGui::CollapsingHeader("Selected Mesh", ImGuiTreeNodeFlags_DefaultOpen))
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
        ImGui::TextDisabled("Pivot agora desloca sem mexer no visual. Este painel transforma a mesh local nos vertices.");

        ImGui::Text("Vertices: %d  Faces: %d",
            static_cast<int>(meshObject.mesh.vertexCount()),
            static_cast<int>(meshObject.mesh.faceCount()));

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
                {
                    selectedFaceIndex_ = rowFaceIndex;
                    selectionMode_ = SelectionMode::Face;
                }
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
                    obj.mesh.facesMutable()[(size_t)selectedFaceIndex_].materialName = currentTexturePath_;
                    failedTextureLoads_.clear();
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
                sceneDirty_ = true;
            }
            ImGui::EndDisabled();

            // Live UV editing — directly modify the selected face's UV params
            ImGui::Separator();
            if (hasFace)
            {
                EditableFace& face = obj.mesh.facesMutable()[(size_t)selectedFaceIndex_];
                bool uvChanged = false;
                uvChanged |= ImGui::DragFloat2("Move UV", &face.uvOffset.x, 0.01f, -1000.0f, 1000.0f, "%.2f");
                uvChanged |= ImGui::DragFloat2("Tile UV", &face.uvScale.x, 0.01f, 0.01f, 128.0f, "%.2f");
                uvChanged |= ImGui::DragFloat("Rotate UV", &face.uvRotation, 0.5f, -360.0f, 360.0f, "%.1f");
                if (ImGui::Button("Reset UV"))
                {
                    face.uvOffset = glm::vec2(0.0f);
                    face.uvScale = glm::vec2(1.0f);
                    face.uvRotation = 0.0f;
                    uvChanged = true;
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
                    uvChanged = true;
                }
                if (uvChanged)
                    meshCacheValid_ = false;
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
            int reloaded = 0;
            for (LevelMeshObject& obj : scene_.meshObjects())
            {
                for (const EditableFace& face : obj.mesh.faces())
                {
                    if (face.materialName.empty() || face.materialName == "default")
                        continue;
                    const std::string texName = "level_face_tex::" + face.materialName;
                    // Try to resolve using assetRoot
                    std::string resolved = ResolveTexturePath(baseDir, face.materialName);
                    if (resolved.empty())
                        resolved = ResolveTexturePath(baseDir, PathFilename(face.materialName));
                    if (!resolved.empty())
                    {
                        Texture* existing = texMgr.get(texName);
                        if (!existing || existing->id == 0)
                        {
                            texMgr.load(texName, resolved);
                            reloaded++;
                        }
                    }
                }
            }
            meshCacheValid_ = false;
            sceneStatusMessage_ = "Reloaded " + std::to_string(reloaded) + " textures from: " + baseDir;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Uses Assets root");
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
