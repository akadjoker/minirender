#include "MeshCSG.hpp"

#include <algorithm>

namespace
{
struct ClippedPolygonVertex
{
    glm::vec3 position {0.0f};
};

bool nearlySamePoint(const glm::vec3& a, const glm::vec3& b, float epsilon)
{
    return glm::length2(a - b) <= epsilon * epsilon;
}

glm::vec3 safeNormalized(const glm::vec3& value)
{
    const float len2 = glm::length2(value);
    if (len2 <= 1e-8f)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(value);
}

glm::vec3 lerpPoint(const glm::vec3& a, const glm::vec3& b, float t)
{
    return a + (b - a) * t;
}

std::vector<ClippedPolygonVertex> clipPolygonAgainstPlane(const std::vector<ClippedPolygonVertex>& polygon,
                                                          const MeshPlane& plane,
                                                          bool keepFront,
                                                          float epsilon)
{
    std::vector<ClippedPolygonVertex> output;
    if (polygon.size() < 3)
        return output;

    auto isInside = [&](float signedDistance) -> bool
    {
        return keepFront ? (signedDistance >= -epsilon) : (signedDistance <= epsilon);
    };

    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const ClippedPolygonVertex& current = polygon[i];
        const ClippedPolygonVertex& previous = polygon[(i + polygon.size() - 1) % polygon.size()];
        const float currentDistance = plane.signedDistanceTo(current.position);
        const float previousDistance = plane.signedDistanceTo(previous.position);
        const bool currentInside = isInside(currentDistance);
        const bool previousInside = isInside(previousDistance);

        if (currentInside != previousInside)
        {
            const float denom = previousDistance - currentDistance;
            const float t = std::fabs(denom) <= epsilon ? 0.0f : (previousDistance / denom);
            ClippedPolygonVertex intersection;
            intersection.position = lerpPoint(previous.position, current.position, glm::clamp(t, 0.0f, 1.0f));
            output.push_back(intersection);
        }

        if (currentInside)
            output.push_back(current);
    }

    if (output.size() < 3)
        return {};

    std::vector<ClippedPolygonVertex> deduped;
    deduped.reserve(output.size());
    for (const ClippedPolygonVertex& vertex : output)
    {
        if (!deduped.empty() && glm::length2(deduped.back().position - vertex.position) <= epsilon * epsilon)
            continue;
        deduped.push_back(vertex);
    }
    if (deduped.size() >= 2 &&
        glm::length2(deduped.front().position - deduped.back().position) <= epsilon * epsilon)
    {
        deduped.pop_back();
    }

    if (deduped.size() < 3)
        return {};
    return deduped;
}

void appendUniquePoint(std::vector<glm::vec3>& points, const glm::vec3& point, float epsilon)
{
    for (const glm::vec3& existing : points)
    {
        if (nearlySamePoint(existing, point, epsilon))
            return;
    }
    points.push_back(point);
}

int findOrAppendVertex(std::vector<EditableVertex>& vertices, const glm::vec3& position, float epsilon)
{
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        if (nearlySamePoint(vertices[i].position, position, epsilon))
            return static_cast<int>(i);
    }

    EditableVertex vertex;
    vertex.position = position;
    vertices.push_back(vertex);
    return static_cast<int>(vertices.size()) - 1;
}

EditableFace buildCapFace(const std::vector<glm::vec3>& cutPoints,
                          std::vector<EditableVertex>& outVertices,
                          const MeshPlane& plane,
                          bool keepFront,
                          float epsilon)
{
    EditableFace face;
    if (cutPoints.size() < 3)
        return face;

    glm::vec3 center(0.0f);
    for (const glm::vec3& point : cutPoints)
        center += point;
    center /= static_cast<float>(cutPoints.size());

    const glm::vec3 planeNormal = keepFront ? -plane.normal : plane.normal;
    const glm::vec3 tangent = safeNormalized(std::abs(planeNormal.x) < 0.9f
        ? glm::cross(planeNormal, glm::vec3(1.0f, 0.0f, 0.0f))
        : glm::cross(planeNormal, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 bitangent = safeNormalized(glm::cross(planeNormal, tangent));

    struct OrderedPoint
    {
        glm::vec3 position {0.0f};
        float angle = 0.0f;
    };

    std::vector<OrderedPoint> ordered;
    ordered.reserve(cutPoints.size());
    for (const glm::vec3& point : cutPoints)
    {
        const glm::vec3 offset = point - center;
        const float x = glm::dot(offset, tangent);
        const float y = glm::dot(offset, bitangent);
        if (std::fabs(x) <= epsilon && std::fabs(y) <= epsilon)
            continue;
        ordered.push_back({point, std::atan2(y, x)});
    }

    std::sort(ordered.begin(), ordered.end(), [](const OrderedPoint& a, const OrderedPoint& b)
    {
        return a.angle < b.angle;
    });

    // Flipping the cap normal changes the tangent basis, so the angle sort
    // naturally produces the opposite winding for the kept half-space.
    for (const OrderedPoint& point : ordered)
    {
        const int vertexIndex = findOrAppendVertex(outVertices, point.position, epsilon);
        if (!face.indices.empty() && face.indices.back() == vertexIndex)
            continue;
        face.indices.push_back(vertexIndex);
    }

    if (face.indices.size() >= 2 && face.indices.front() == face.indices.back())
        face.indices.pop_back();
    if (face.indices.size() < 3)
        face.indices.clear();
    face.materialName = "csg_cap";
    return face;
}
}

MeshPlane MeshPlane::FromPointNormal(const glm::vec3& point, const glm::vec3& normal)
{
    MeshPlane plane;
    plane.normal = safeNormalized(normal);
    plane.distance = -glm::dot(plane.normal, point);
    return plane;
}

float MeshPlane::signedDistanceTo(const glm::vec3& point) const
{
    return glm::dot(normal, point) + distance;
}

EditableMesh clipEditableMeshAgainstPlane(const EditableMesh& mesh,
                                          const MeshPlane& plane,
                                          bool keepFront,
                                          bool addCap,
                                          float epsilon)
{
    std::vector<EditableVertex> vertices;
    std::vector<EditableFace> faces;
    std::vector<glm::vec3> cutPoints;

    for (const EditableFace& face : mesh.faces())
    {
        if (face.indices.size() < 3)
            continue;

        std::vector<ClippedPolygonVertex> polygon;
        polygon.reserve(face.indices.size());
        bool validFace = true;
        for (int index : face.indices)
        {
            if (index < 0 || index >= static_cast<int>(mesh.vertices().size()))
            {
                validFace = false;
                break;
            }

            ClippedPolygonVertex vertex;
            vertex.position = mesh.vertices()[(size_t)index].position;
            polygon.push_back(vertex);
        }
        if (!validFace)
            continue;

        for (std::size_t i = 0; i < polygon.size(); ++i)
        {
            const glm::vec3& current = polygon[i].position;
            const glm::vec3& next = polygon[(i + 1) % polygon.size()].position;
            const float currentDistance = plane.signedDistanceTo(current);
            const float nextDistance = plane.signedDistanceTo(next);
            const bool currentOn = std::fabs(currentDistance) <= epsilon;
            const bool nextOn = std::fabs(nextDistance) <= epsilon;

            if (currentOn)
                appendUniquePoint(cutPoints, current, epsilon);
            if (nextOn)
                appendUniquePoint(cutPoints, next, epsilon);

            const bool crosses = (currentDistance > epsilon && nextDistance < -epsilon) ||
                                 (currentDistance < -epsilon && nextDistance > epsilon);
            if (crosses || (currentOn && !nextOn) || (!currentOn && nextOn))
            {
                const float denom = currentDistance - nextDistance;
                const float t = std::fabs(denom) <= epsilon ? 0.0f : (currentDistance / denom);
                appendUniquePoint(cutPoints, lerpPoint(current, next, glm::clamp(t, 0.0f, 1.0f)), epsilon);
            }
        }

        const std::vector<ClippedPolygonVertex> clipped = clipPolygonAgainstPlane(polygon, plane, keepFront, epsilon);
        if (clipped.size() < 3)
            continue;

        EditableFace outFace;
        outFace.materialName = face.materialName;
        outFace.indices.reserve(clipped.size());
        for (const ClippedPolygonVertex& clippedVertex : clipped)
        {
            const int vertexIndex = findOrAppendVertex(vertices, clippedVertex.position, epsilon);
            if (!outFace.indices.empty() && outFace.indices.back() == vertexIndex)
                continue;
            outFace.indices.push_back(vertexIndex);
        }
        if (outFace.indices.size() >= 2 && outFace.indices.front() == outFace.indices.back())
            outFace.indices.pop_back();
        if (outFace.indices.size() >= 3)
            faces.push_back(std::move(outFace));
    }

    if (addCap)
    {
        EditableFace capFace = buildCapFace(cutPoints, vertices, plane, keepFront, epsilon);
        if (!capFace.indices.empty())
            faces.push_back(capFace);
    }

    return EditableMesh::FromData(vertices, faces);
}
