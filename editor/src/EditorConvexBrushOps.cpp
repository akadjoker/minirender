#include "EditorConvexBrushOps.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>

namespace {
EditorBrushFace makeFace(const glm::vec3 &a,
                         const glm::vec3 &b,
                         const glm::vec3 &c,
                         const std::string &texturePath,
                         const glm::vec2 &uvOffset = glm::vec2(0.0f),
                         const glm::vec2 &uvScale = glm::vec2(1.0f),
                         float uvRotation = 0.0f)
{
    EditorBrushFace face;
    face.planePoints = {a, b, c};
    face.texturePath = texturePath;
    face.uvOffset = uvOffset;
    face.uvScale = uvScale;
    face.uvRotation = uvRotation;
    return face;
}

std::string resolvedFaceTexture(const BrushVolume &volume, int faceIndex)
{
    if (faceIndex >= 0 && faceIndex < (int)volume.faceTextures.size() &&
        !volume.faceTextures[(size_t)faceIndex].empty())
    {
        return volume.faceTextures[(size_t)faceIndex];
    }
    return volume.texturePath;
}

EditorBrushFace makeFaceFromVolume(const BrushVolume &volume,
                                   int faceIndex,
                                   const glm::vec3 &a,
                                   const glm::vec3 &b,
                                   const glm::vec3 &c)
{
    const BrushVolume::FaceUV &faceUv = volume.faceUV[(size_t)faceIndex];
    return makeFace(
        a,
        b,
        c,
        resolvedFaceTexture(volume, faceIndex),
        volume.uvOffset + faceUv.offset,
        glm::vec2(volume.uvScale.x * faceUv.scale.x, volume.uvScale.y * faceUv.scale.y),
        volume.uvRotation + faceUv.rotation);
}

glm::vec3 faceNormal(const EditorBrushFace &face)
{
    const glm::vec3 normal = glm::cross(face.planePoints[1] - face.planePoints[0],
                                        face.planePoints[2] - face.planePoints[0]);
    if (glm::length2(normal) <= 1e-8f)
        return glm::vec3(0.0f);
    return glm::normalize(normal);
}

bool hasRenderablePolygons(const EditorBrush &brush)
{
    int polygonCount = 0;
    for (const EditorConvexFacePolygon &polygon : buildConvexFacePolygons(brush))
    {
        if (polygon.vertices.size() >= 3)
            ++polygonCount;
    }
    return polygonCount >= 4;
}
} // namespace

EditorBrush makeBoxConvexBrush(const glm::vec3 &mins,
                               const glm::vec3 &maxs,
                               const std::string &name,
                               const std::string &texturePath)
{
    EditorBrush brush;
    brush.name = name;
    brush.primitive = EditorBrushPrimitive::Box;
    brush.faces = {
        makeFace(glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
        makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(mins.x, maxs.y, maxs.z), texturePath),
        makeFace(glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(mins.x, maxs.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
        makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), texturePath),
        makeFace(glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
        makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z), texturePath),
    };
    normalizeConvexBrush(brush);
    return brush;
}

EditorBrush makeWedgeConvexBrush(const glm::vec3 &mins,
                                 const glm::vec3 &maxs,
                                 EditorRampDirection direction,
                                 const std::string &name,
                                 const std::string &texturePath)
{
    EditorBrush brush;
    brush.name = name;
    brush.primitive = EditorBrushPrimitive::Wedge;

    switch (direction)
    {
    case EditorRampDirection::PosX:
        brush.faces = {
            makeFace(glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
        };
        break;
    case EditorRampDirection::NegX:
        brush.faces = {
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, maxs.y, maxs.z), glm::vec3(mins.x, mins.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(mins.x, maxs.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(mins.x, mins.y, mins.z), texturePath),
            makeFace(glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(mins.x, maxs.y, maxs.z), texturePath),
        };
        break;
    case EditorRampDirection::PosZ:
        brush.faces = {
            makeFace(glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), texturePath),
            makeFace(glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, maxs.y, maxs.z), glm::vec3(maxs.x, mins.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(mins.x, maxs.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, maxs.y, maxs.z), texturePath),
        };
        break;
    case EditorRampDirection::NegZ:
        brush.faces = {
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), texturePath),
            makeFace(glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, mins.z), texturePath),
            makeFace(glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(mins.x, mins.y, maxs.z), texturePath),
            makeFace(glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(mins.x, mins.y, maxs.z), texturePath),
        };
        break;
    }

    normalizeConvexBrush(brush);
    return brush;
}

EditorBrush makeCylinderConvexBrush(const glm::vec3 &center,
                                    float radius,
                                    float height,
                                    int sides,
                                    const std::string &name,
                                    const std::string &texturePath)
{
    EditorBrush brush;
    brush.name = name;
    brush.primitive = EditorBrushPrimitive::Cylinder;

    const int clampedSides = glm::max(sides, 3);
    const float halfHeight = glm::max(height * 0.5f, 0.5f);
    const float safeRadius = glm::max(radius, 0.5f);
    const glm::vec3 topCenter = center + glm::vec3(0.0f, halfHeight, 0.0f);
    const glm::vec3 bottomCenter = center - glm::vec3(0.0f, halfHeight, 0.0f);

    std::vector<glm::vec3> topRing;
    std::vector<glm::vec3> bottomRing;
    topRing.reserve((size_t)clampedSides);
    bottomRing.reserve((size_t)clampedSides);

    for (int i = 0; i < clampedSides; ++i)
    {
        const float angle = glm::two_pi<float>() * ((float)i / (float)clampedSides);
        const float x = std::cos(angle) * safeRadius;
        const float z = std::sin(angle) * safeRadius;
        topRing.push_back(topCenter + glm::vec3(x, 0.0f, z));
        bottomRing.push_back(bottomCenter + glm::vec3(x, 0.0f, z));
    }

    for (int i = 0; i < clampedSides; ++i)
    {
        const int next = (i + 1) % clampedSides;
        brush.faces.push_back(makeFace(topRing[i], bottomRing[i], bottomRing[next], texturePath));
    }

    for (int i = 1; i + 1 < clampedSides; ++i)
    {
        brush.faces.push_back(makeFace(topRing[0], topRing[i], topRing[i + 1], texturePath));
        brush.faces.push_back(makeFace(bottomRing[0], bottomRing[i + 1], bottomRing[i], texturePath));
    }

    normalizeConvexBrush(brush);
    return brush;
}

EditorBrush makeConvexBrushFromVolume(const BrushVolume &volume)
{
    EditorBrush brush;
    brush.name = volume.name;
    brush.primitive = EditorBrushPrimitive::Box;
    brush.color = volume.color;
    brush.hidden = volume.hidden;
    brush.dirty = volume.dirty;

    const glm::vec3 &mins = volume.mins;
    const glm::vec3 &maxs = volume.maxs;
    brush.faces = {
        makeFaceFromVolume(volume, 0, glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z), glm::vec3(maxs.x, maxs.y, maxs.z)),
        makeFaceFromVolume(volume, 1, glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(mins.x, maxs.y, maxs.z)),
        makeFaceFromVolume(volume, 2, glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(mins.x, maxs.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z)),
        makeFaceFromVolume(volume, 3, glm::vec3(mins.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, mins.z), glm::vec3(maxs.x, mins.y, maxs.z)),
        makeFaceFromVolume(volume, 4, glm::vec3(mins.x, mins.y, maxs.z), glm::vec3(maxs.x, mins.y, maxs.z), glm::vec3(maxs.x, maxs.y, maxs.z)),
        makeFaceFromVolume(volume, 5, glm::vec3(mins.x, mins.y, mins.z), glm::vec3(mins.x, maxs.y, mins.z), glm::vec3(maxs.x, maxs.y, mins.z)),
    };

    normalizeConvexBrush(brush);
    return brush;
}

glm::vec3 convexBrushInteriorPoint(const EditorBrush &brush)
{
    glm::vec3 interior(0.0f);
    int count = 0;
    for (const EditorBrushFace &face : brush.faces)
    {
        for (const glm::vec3 &point : face.planePoints)
        {
            interior += point;
            ++count;
        }
    }
    if (count <= 0)
        return glm::vec3(0.0f);
    return interior / (float)count;
}

void normalizeConvexBrush(EditorBrush &brush)
{
    const glm::vec3 interior = convexBrushInteriorPoint(brush);
    for (EditorBrushFace &face : brush.faces)
    {
        const glm::vec3 normal = glm::cross(face.planePoints[1] - face.planePoints[0],
                                            face.planePoints[2] - face.planePoints[0]);
        if (glm::length2(normal) <= 1e-8f)
            continue;
        if (glm::dot(normal, interior - face.planePoints[0]) > 0.0f)
            std::swap(face.planePoints[1], face.planePoints[2]);
    }
}

bool clipConvexBrush(const EditorBrush &source,
                     const glm::vec3 &planePoint,
                     const glm::vec3 &planeNormal,
                     const std::string &texturePath,
                     EditorBrush &outBrush)
{
    if (!source.isValid() || glm::length2(planeNormal) <= 1e-8f)
        return false;

    glm::vec3 mins(0.0f);
    glm::vec3 maxs(0.0f);
    bool firstVertex = true;
    for (const EditorConvexFacePolygon &polygon : buildConvexFacePolygons(source))
    {
        for (const glm::vec3 &vertex : polygon.vertices)
        {
            if (firstVertex)
            {
                mins = vertex;
                maxs = vertex;
                firstVertex = false;
            }
            else
            {
                mins = glm::min(mins, vertex);
                maxs = glm::max(maxs, vertex);
            }
        }
    }
    if (firstVertex)
        return false;

    outBrush = source;
    glm::vec3 normal = glm::normalize(planeNormal);
    const glm::vec3 interior = convexBrushInteriorPoint(source);
    if (glm::dot(normal, interior - planePoint) > 0.0f)
        normal = -normal;

    glm::vec3 tangent = glm::cross(std::fabs(normal.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                               : glm::vec3(1.0f, 0.0f, 0.0f),
                                   normal);
    if (glm::length2(tangent) <= 1e-8f)
        return false;
    tangent = glm::normalize(tangent);
    const glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
    const float extent = glm::max(glm::length(maxs - mins) * 2.0f, 64.0f);
    const std::string clipTexture = texturePath.empty() && !source.faces.empty()
        ? source.faces.front().texturePath
        : texturePath;

    outBrush.faces.push_back(makeFace(planePoint + tangent * extent + bitangent * extent,
                                      planePoint - tangent * extent + bitangent * extent,
                                      planePoint - tangent * extent - bitangent * extent,
                                      clipTexture));
    normalizeConvexBrush(outBrush);
    return hasRenderablePolygons(outBrush);
}

bool moveConvexBrushFace(const EditorBrush &source,
                         int faceIndex,
                         float distance,
                         EditorBrush &outBrush)
{
    if (!source.isValid() || faceIndex < 0 || faceIndex >= (int)source.faces.size())
        return false;

    const glm::vec3 normal = faceNormal(source.faces[(size_t)faceIndex]);
    if (glm::length2(normal) <= 1e-8f)
        return false;

    outBrush = source;
    for (glm::vec3 &point : outBrush.faces[(size_t)faceIndex].planePoints)
        point += normal * distance;
    normalizeConvexBrush(outBrush);
    return hasRenderablePolygons(outBrush);
}

std::vector<EditorConvexFacePolygon> buildConvexFacePolygons(const EditorBrush &brush)
{
    struct Plane
    {
        glm::vec3 normal = glm::vec3(0.0f);
        float d = 0.0f;
    };

    std::vector<EditorConvexFacePolygon> polygons;
    if (!brush.isValid())
        return polygons;

    std::vector<Plane> planes;
    planes.reserve(brush.faces.size());
    for (const EditorBrushFace &face : brush.faces)
    {
        glm::vec3 normal = glm::cross(face.planePoints[1] - face.planePoints[0],
                                      face.planePoints[2] - face.planePoints[0]);
        if (glm::length2(normal) <= 1e-8f)
        {
            planes.push_back({});
            continue;
        }

        normal = glm::normalize(normal);
        Plane plane;
        plane.normal = normal;
        plane.d = -glm::dot(normal, face.planePoints[0]);
        planes.push_back(plane);
    }

    auto intersectThreePlanes = [](const Plane &a,
                                   const Plane &b,
                                   const Plane &c,
                                   glm::vec3 &outPoint) -> bool
    {
        const glm::vec3 bc = glm::cross(b.normal, c.normal);
        const float denom = glm::dot(a.normal, bc);
        if (std::fabs(denom) <= 1e-6f)
            return false;

        outPoint = (-a.d * bc
                  - b.d * glm::cross(c.normal, a.normal)
                  - c.d * glm::cross(a.normal, b.normal)) / denom;
        return true;
    };

    auto pointInsideBrush = [&](const glm::vec3 &point) -> bool
    {
        for (const Plane &plane : planes)
        {
            if (glm::length2(plane.normal) <= 1e-8f)
                continue;
            if (glm::dot(plane.normal, point) + plane.d > 1e-4f)
                return false;
        }
        return true;
    };

    for (int faceIndex = 0; faceIndex < (int)brush.faces.size(); ++faceIndex)
    {
        const Plane &plane = planes[(size_t)faceIndex];
        if (glm::length2(plane.normal) <= 1e-8f)
            continue;

        std::vector<glm::vec3> facePoints;
        for (int i = 0; i < (int)planes.size(); ++i)
        {
            for (int j = i + 1; j < (int)planes.size(); ++j)
            {
                glm::vec3 point(0.0f);
                if (!intersectThreePlanes(plane, planes[(size_t)i], planes[(size_t)j], point))
                    continue;
                if (std::fabs(glm::dot(plane.normal, point) + plane.d) > 1e-3f)
                    continue;
                if (!pointInsideBrush(point))
                    continue;

                bool duplicate = false;
                for (const glm::vec3 &existing : facePoints)
                {
                    if (glm::length2(existing - point) <= 1e-6f)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                    facePoints.push_back(point);
            }
        }

        if (facePoints.size() < 3)
            continue;

        glm::vec3 faceCenter(0.0f);
        for (const glm::vec3 &point : facePoints)
            faceCenter += point;
        faceCenter /= (float)facePoints.size();

        glm::vec3 tangent = facePoints[0] - faceCenter;
        if (glm::length2(tangent) <= 1e-8f)
            tangent = glm::cross(plane.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length2(tangent) <= 1e-8f)
            tangent = glm::cross(plane.normal, glm::vec3(1.0f, 0.0f, 0.0f));
        tangent = glm::normalize(tangent);
        const glm::vec3 bitangent = glm::normalize(glm::cross(plane.normal, tangent));

        std::sort(facePoints.begin(), facePoints.end(),
                  [&](const glm::vec3 &a, const glm::vec3 &b)
        {
            const glm::vec3 ra = a - faceCenter;
            const glm::vec3 rb = b - faceCenter;
            const float angleA = std::atan2(glm::dot(ra, bitangent), glm::dot(ra, tangent));
            const float angleB = std::atan2(glm::dot(rb, bitangent), glm::dot(rb, tangent));
            return angleA < angleB;
        });

        polygons.push_back({faceIndex, facePoints});
    }

    return polygons;
}
