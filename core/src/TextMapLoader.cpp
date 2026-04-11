#include "TextMapLoader.hpp"

#include "Collision.hpp"
#include "Manager.hpp"
#include "M8Texture.hpp"
#include "Pixmap.hpp"
#include "TextMap.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <memory>
#include <sys/stat.h>
#include <unordered_map>

namespace
{
struct BrushFaceData
{
    Plane plane;
    glm::vec3 points[3] = {};
    std::string textureName;
    float shiftX = 0.0f;
    float shiftY = 0.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    std::vector<glm::vec3> facePoints;
    const TextMapFace *face = nullptr;
};

struct BrushBuildData
{
    std::vector<BrushFaceData> faces;
};

struct SurfaceGroup
{
    std::string textureName;
    Texture *texture = nullptr;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct QuantizedVertexKey
{
    int px = 0, py = 0, pz = 0;
    int nx = 0, ny = 0, nz = 0;
    int u = 0, v = 0;

    bool operator==(const QuantizedVertexKey &other) const
    {
        return px == other.px && py == other.py && pz == other.pz &&
               nx == other.nx && ny == other.ny && nz == other.nz &&
               u == other.u && v == other.v;
    }
};

struct QuantizedVertexKeyHash
{
    size_t operator()(const QuantizedVertexKey &key) const
    {
        size_t h = 1469598103934665603ull;
        const auto mix = [&](int value)
        {
            h ^= static_cast<size_t>(static_cast<uint32_t>(value));
            h *= 1099511628211ull;
        };

        mix(key.px); mix(key.py); mix(key.pz);
        mix(key.nx); mix(key.ny); mix(key.nz);
        mix(key.u);  mix(key.v);
        return h;
    }
};

struct TriangleKey
{
    uint32_t a = 0, b = 0, c = 0;

    bool operator==(const TriangleKey &other) const
    {
        return a == other.a && b == other.b && c == other.c;
    }
};

struct TriangleKeyHash
{
    size_t operator()(const TriangleKey &key) const
    {
        size_t h = 1469598103934665603ull;
        h ^= key.a; h *= 1099511628211ull;
        h ^= key.b; h *= 1099511628211ull;
        h ^= key.c; h *= 1099511628211ull;
        return h;
    }
};

glm::vec3 mapPointToEngine(const glm::vec3 &point, bool remapZUpToYUp)
{
    return remapZUpToYUp ? glm::vec3(point.x, point.z, point.y) : point;
}

bool nearlySamePoint(const glm::vec3 &a, const glm::vec3 &b, float epsilon)
{
    return glm::length2(a - b) <= epsilon * epsilon;
}

bool isDirectory(const std::string &path)
{
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
        return false;
    return (info.st_mode & S_IFDIR) != 0;
}

int quantizeFloat(float value, float scale)
{
    return static_cast<int>(std::lround(value * scale));
}

QuantizedVertexKey makeQuantizedVertexKey(const Vertex &vertex)
{
    QuantizedVertexKey key;
    key.px = quantizeFloat(vertex.position.x, 256.0f);
    key.py = quantizeFloat(vertex.position.y, 256.0f);
    key.pz = quantizeFloat(vertex.position.z, 256.0f);
    key.nx = quantizeFloat(vertex.normal.x, 4096.0f);
    key.ny = quantizeFloat(vertex.normal.y, 4096.0f);
    key.nz = quantizeFloat(vertex.normal.z, 4096.0f);
    key.u  = quantizeFloat(vertex.uv.x, 4096.0f);
    key.v  = quantizeFloat(vertex.uv.y, 4096.0f);
    return key;
}

void optimizeSurfaceGroup(SurfaceGroup &group)
{
    if (group.vertices.empty() || group.indices.empty())
        return;

    std::vector<Vertex> weldedVertices;
    weldedVertices.reserve(group.vertices.size());

    std::vector<uint32_t> remap(group.vertices.size(), 0);
    std::unordered_map<QuantizedVertexKey, uint32_t, QuantizedVertexKeyHash> vertexLookup;
    vertexLookup.reserve(group.vertices.size());

    for (size_t i = 0; i < group.vertices.size(); ++i)
    {
        const QuantizedVertexKey key = makeQuantizedVertexKey(group.vertices[i]);
        const auto found = vertexLookup.find(key);
        if (found != vertexLookup.end())
        {
            remap[i] = found->second;
            continue;
        }

        const uint32_t newIndex = static_cast<uint32_t>(weldedVertices.size());
        vertexLookup.emplace(key, newIndex);
        weldedVertices.push_back(group.vertices[i]);
        remap[i] = newIndex;
    }

    std::vector<uint32_t> optimizedIndices;
    optimizedIndices.reserve(group.indices.size());
    std::unordered_map<TriangleKey, uint8_t, TriangleKeyHash> triangleLookup;
    triangleLookup.reserve(group.indices.size() / 3u);

    for (size_t i = 0; i + 2 < group.indices.size(); i += 3)
    {
        const uint32_t ia = remap[group.indices[i + 0]];
        const uint32_t ib = remap[group.indices[i + 1]];
        const uint32_t ic = remap[group.indices[i + 2]];
        if (ia == ib || ib == ic || ic == ia)
            continue;

        TriangleKey canonical{ia, ib, ic};
        if (canonical.a > canonical.b) std::swap(canonical.a, canonical.b);
        if (canonical.b > canonical.c) std::swap(canonical.b, canonical.c);
        if (canonical.a > canonical.b) std::swap(canonical.a, canonical.b);

        if (triangleLookup.find(canonical) != triangleLookup.end())
            continue;

        triangleLookup.emplace(canonical, 1);
        optimizedIndices.push_back(ia);
        optimizedIndices.push_back(ib);
        optimizedIndices.push_back(ic);
    }

    group.vertices = std::move(weldedVertices);
    group.indices = std::move(optimizedIndices);
}

bool planeFromPoints(BrushFaceData &face)
{
    const glm::vec3 t1 = face.points[1] - face.points[0];
    const glm::vec3 t2 = face.points[2] - face.points[1];
    glm::vec3 normal = glm::cross(t1, t2);
    const float length2 = glm::length2(normal);
    if (length2 <= 1e-8f)
        return false;

    normal /= std::sqrt(length2);
    face.plane = Plane(normal, glm::dot(face.points[0], normal));
    return true;
}

void calcMasterFace(BrushFaceData &face)
{
    const glm::vec3 normal = face.plane.normal;
    const float dist = face.plane.d;

    float maxAbs = -655360.0f;
    int majorAxis = -1;
    for (int i = 0; i < 3; ++i)
    {
        const float axisAbs = std::fabs(normal[i]);
        if (axisAbs > maxAbs)
        {
            majorAxis = i;
            maxAbs = axisAbs;
        }
    }

    if (majorAxis < 0)
        return;

    glm::vec3 vup(0.0f);
    switch (majorAxis)
    {
    case 0:
    case 1:
        vup.z = 1.0f;
        break;
    case 2:
        vup.x = 1.0f;
        break;
    }

    const float projection = glm::dot(vup, normal);
    vup = glm::normalize(vup - projection * normal);
    const glm::vec3 org = normal * dist;
    glm::vec3 vright = glm::cross(vup, normal);
    vup *= 65536.0f;
    vright *= 65536.0f;

    face.facePoints.resize(4);
    face.facePoints[0] = org - vright + vup;
    face.facePoints[1] = org + vright + vup;
    face.facePoints[2] = org + vright - vup;
    face.facePoints[3] = org - vright - vup;
}

bool clipToPlane(const Plane &plane,
                 const std::vector<glm::vec3> &inPoints,
                 std::vector<glm::vec3> &outPoints,
                 float epsilon)
{
    outPoints.clear();
    if (inPoints.size() < 3)
        return false;

    glm::vec3 current = inPoints[0];
    float currentDot = glm::dot(current, plane.normal);
    bool currentInside = currentDot >= (plane.d - epsilon);

    for (size_t i = 0; i < inPoints.size(); ++i)
    {
        const glm::vec3 next = inPoints[(i + 1) % inPoints.size()];
        const float nextDot = glm::dot(next, plane.normal);
        const bool nextInside = nextDot >= (plane.d - epsilon);

        if (currentInside)
            outPoints.push_back(current);

        if (currentInside != nextInside)
        {
            const float denom = nextDot - currentDot;
            if (std::fabs(denom) > 1e-8f)
            {
                const float scale = (plane.d - currentDot) / denom;
                outPoints.push_back(current + (next - current) * scale);
            }
        }

        current = next;
        currentDot = nextDot;
        currentInside = nextInside;
    }

    return outPoints.size() >= 3;
}

void clipFace(const BrushBuildData &brush,
              BrushFaceData &face,
              float epsilon)
{
    std::vector<glm::vec3> inPoints = face.facePoints;
    std::vector<glm::vec3> outPoints;

    for (const BrushFaceData &clipper : brush.faces)
    {
        if (&clipper == &face)
            continue;

        if (!clipToPlane(clipper.plane, inPoints, outPoints, epsilon))
        {
            face.facePoints.clear();
            return;
        }

        inPoints.swap(outPoints);
    }

    face.facePoints = inPoints;
}

bool collapsePoints(BrushFaceData &face, float epsilon)
{
    std::vector<glm::vec3> unique;
    unique.reserve(face.facePoints.size());

    for (const glm::vec3 &point : face.facePoints)
    {
        bool duplicate = false;
        for (const glm::vec3 &existing : unique)
        {
            if (nearlySamePoint(existing, point, epsilon))
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            unique.push_back(point);
    }

    if (unique.size() < 3)
        return false;

    face.facePoints = std::move(unique);
    return true;
}

bool createBrushFaces(const TextMapBrush &brush,
                      const TextMapLoadOptions &options,
                      BrushBuildData &outBrush)
{
    outBrush.faces.clear();
    outBrush.faces.reserve(brush.faces.size());
    for (const TextMapFace &face : brush.faces)
    {
        BrushFaceData data;
        data.points[0] = face.points[0];
        data.points[1] = face.points[1];
        data.points[2] = face.points[2];
        data.textureName = face.texture;
        data.shiftX = face.shiftX;
        data.shiftY = face.shiftY;
        data.rotation = face.rotation;
        data.scaleX = face.scaleX;
        data.scaleY = face.scaleY;
        data.face = &face;

        if (!planeFromPoints(data))
            continue;

        calcMasterFace(data);
        outBrush.faces.push_back(std::move(data));
    }

    if (outBrush.faces.size() < 4)
        return false;

    for (BrushFaceData &face : outBrush.faces)
    {
        clipFace(outBrush, face, options.planeEpsilon);
        if (!collapsePoints(face, options.vertexEpsilon))
            return false;
    }

    return true;
}

static const glm::vec3 kBaseAxis[18] = {
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),

    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),

    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),

    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),

    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),

    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
};

void textureAxisFromPlane(const Plane &plane, glm::vec3 &xAxis, glm::vec3 &yAxis)
{
    float best = 0.0f;
    int bestAxis = 0;
    for (int i = 0; i < 6; ++i)
    {
        const float dot = glm::dot(plane.normal, kBaseAxis[i * 3]);
        if (dot > best)
        {
            best = dot;
            bestAxis = i;
        }
    }

    xAxis = kBaseAxis[bestAxis * 3 + 1];
    yAxis = kBaseAxis[bestAxis * 3 + 2];
}

glm::vec2 computeFaceUv(const glm::vec3 &rawPosition,
                        const BrushFaceData &face,
                        float textureWidth,
                        float textureHeight,
                        float fallbackPixelsPerUnit)
{
    glm::vec3 xAxis(1.0f, 0.0f, 0.0f);
    glm::vec3 yAxis(0.0f, 1.0f, 0.0f);
    textureAxisFromPlane(face.plane, xAxis, yAxis);

    const float angle = face.rotation * DEG2RAD;
    const float sinv = std::sin(angle);
    const float cosv = std::cos(angle);

    float u = glm::dot(rawPosition, xAxis);
    float v = glm::dot(rawPosition, yAxis);
    const float rotatedU = cosv * u - sinv * v;
    const float rotatedV = sinv * u + cosv * v;

    const float scaleX = (std::fabs(face.scaleX) > 1e-6f) ? face.scaleX : 1.0f;
    const float scaleY = (std::fabs(face.scaleY) > 1e-6f) ? face.scaleY : 1.0f;
    const float texWidth = (textureWidth > 0.0f) ? textureWidth : fallbackPixelsPerUnit;
    const float texHeight = (textureHeight > 0.0f) ? textureHeight : fallbackPixelsPerUnit;

    u = rotatedU / scaleX + face.shiftX;
    v = rotatedV / scaleY + face.shiftY;
    return glm::vec2(u / texWidth, v / texHeight);
}

glm::vec3 computePolygonNormal(const std::vector<Vertex> &vertices)
{
    if (vertices.size() < 3)
        return glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::vec3 normal =
        glm::cross(vertices[1].position - vertices[0].position,
                   vertices[2].position - vertices[0].position);
    const float length2 = glm::length2(normal);
    if (length2 <= 1e-8f)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return normal / std::sqrt(length2);
}

void resetMesh(Mesh &mesh, const std::string &name)
{
    mesh.free();
    mesh.name = name;
    mesh.aabb = BoundingBox{};
    mesh.buffer.vertices.clear();
    mesh.buffer.indices.clear();
    mesh.buffer.aabb = BoundingBox{};
    mesh.surfaces.clear();
}

Texture *resolveMapTexture(const std::string &textureDirectory,
                           const std::string &textureName,
                           Texture *white)
{
    if (textureDirectory.empty() || textureName.empty())
        return white;

    std::vector<std::string> roots;
    roots.push_back(textureDirectory);
    const std::string texturesSubdir = PathJoin(textureDirectory, "textures");
    if (isDirectory(texturesSubdir))
        roots.push_back(texturesSubdir);

    for (const std::string &root : roots)
    {
        const std::string resolved = ResolveTexturePath(root, textureName);
        if (!resolved.empty())
        {
            const std::string cacheName = "maptex:" + LowerString(resolved);
            Texture *loaded = TextureManager::instance().load(cacheName, resolved);
            return loaded ? loaded : white;
        }

        std::string rel = textureName;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        while (!rel.empty() && rel.front() == '/')
            rel.erase(rel.begin());

        const std::string fileOnly = PathFilename(rel);
        std::vector<std::string> candidates;
        candidates.reserve(4);
        if (!rel.empty())
        {
            candidates.push_back(PathJoin(root, rel));
            candidates.push_back(PathJoin(root, rel + ".m8"));
        }
        if (!fileOnly.empty())
        {
            candidates.push_back(PathJoin(root, fileOnly));
            candidates.push_back(PathJoin(root, fileOnly + ".m8"));
        }

        for (const std::string &candidate : candidates)
        {
            if (!FileExists(candidate))
                continue;

            const std::string lowerCandidate = LowerString(candidate);
            if (lowerCandidate.size() < 3 || lowerCandidate.substr(lowerCandidate.size() - 3) != ".m8")
                continue;

            const std::string cacheName = "maptexm8:" + lowerCandidate;
            if (Texture *existing = TextureManager::instance().get(cacheName))
                return existing;

            M8Image image;
            std::string error;
            if (!image.loadFromFile(candidate, &error))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[TextMap] Failed to load M8 texture '%s': %s",
                            candidate.c_str(), error.c_str());
                continue;
            }

            std::unique_ptr<Pixmap> pixmap(image.createPixmap(0));
            if (!pixmap)
                continue;

            Texture *texture = TextureManager::instance().createFromPixmap(cacheName, *pixmap);
            if (texture)
                return texture;
        }
    }

    return white;
}
} // namespace

void TextMapLoadResult::applyToCollision(CollisionSystem &system, bool clearFirst) const
{
    if (clearFirst)
        system.clear();
    system.addTriangles(collisionTriangles);
}

bool TextMapLoader::load(const std::string &meshName,
                         const std::string &mapPath,
                         const TextMapLoadOptions &options,
                         Mesh &outMesh,
                         TextMapLoadResult *outResult,
                         std::string *error)
{
    TextMapDocument document;
    if (!TextMapParser::loadFromFile(mapPath, document, error))
        return false;
    return build(meshName, document, options, outMesh, outResult, error);
}

bool TextMapLoader::build(const std::string &meshName,
                          const TextMapDocument &document,
                          const TextMapLoadOptions &options,
                          Mesh &outMesh,
                          TextMapLoadResult *outResult,
                          std::string *error)
{
    resetMesh(outMesh, meshName);

    if (outResult)
    {
        outResult->mesh = &outMesh;
        outResult->bounds = BoundingBox{};
        outResult->collisionTriangles.clear();
    }

    std::vector<SurfaceGroup> groups;
    std::unordered_map<std::string, size_t> groupIndexByTexture;
    std::unordered_map<std::string, Texture *> textureCache;
    size_t attemptedBrushes = 0;
    size_t builtBrushes = 0;
    size_t generatedFaces = 0;

    Texture *white = TextureManager::instance().getWhite();
    auto resolveTexture = [&](const std::string &textureName) -> Texture *
    {
        auto it = textureCache.find(textureName);
        if (it != textureCache.end())
            return it->second;

        Texture *resolved = resolveMapTexture(options.textureDirectory, textureName, white);
        textureCache[textureName] = resolved ? resolved : white;
        return textureCache[textureName];
    };

    auto getGroup = [&](const std::string &textureName, Texture *texture) -> SurfaceGroup &
    {
        auto it = groupIndexByTexture.find(textureName);
        if (it != groupIndexByTexture.end())
            return groups[it->second];

        const size_t groupIndex = groups.size();
        groupIndexByTexture[textureName] = groupIndex;
        groups.push_back({});
        groups.back().textureName = textureName;
        groups.back().texture = texture ? texture : white;
        return groups.back();
    };

    for (const TextMapEntity &entity : document.entities)
    {
        for (const TextMapBrush &brush : entity.brushes)
        {
            ++attemptedBrushes;
            BrushBuildData brushData;
            if (!createBrushFaces(brush, options, brushData))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[TextMap] Discarded brush at line %d with %zu source faces",
                            brush.line, brush.faces.size());
                continue;
            }

            ++builtBrushes;

            for (const BrushFaceData &face : brushData.faces)
            {
                if (face.facePoints.size() < 3)
                    continue;

                Texture *texture = resolveTexture(face.textureName);
                SurfaceGroup &group = getGroup(face.textureName, texture);
                const uint32_t baseVertex = static_cast<uint32_t>(group.vertices.size());

                std::vector<Vertex> faceVertices;
                faceVertices.reserve(face.facePoints.size());
                for (const glm::vec3 &rawPosition : face.facePoints)
                {
                    Vertex vertex{};
                    vertex.position = mapPointToEngine(rawPosition, options.remapZUpToYUp);
                    vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    vertex.uv = computeFaceUv(rawPosition,
                                              face,
                                              texture ? (float)texture->width : 0.0f,
                                              texture ? (float)texture->height : 0.0f,
                                              options.texturePixelsPerUnit);
                    faceVertices.push_back(vertex);
                }

                if (options.remapZUpToYUp)
                    std::reverse(faceVertices.begin(), faceVertices.end());

                const glm::vec3 normal = computePolygonNormal(faceVertices);
                for (Vertex &vertex : faceVertices)
                {
                    vertex.normal = normal;
                    group.vertices.push_back(vertex);
                }

                for (size_t i = 1; i + 1 < faceVertices.size(); ++i)
                {
                    group.indices.push_back(baseVertex);
                    group.indices.push_back(baseVertex + static_cast<uint32_t>(i));
                    group.indices.push_back(baseVertex + static_cast<uint32_t>(i + 1));

                    if (outResult)
                    {
                        outResult->collisionTriangles.push_back(
                            {faceVertices[0].position,
                             faceVertices[i].position,
                             faceVertices[i + 1].position});
                    }
                }

                ++generatedFaces;
            }
        }
    }

    if (groups.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[TextMap] No render groups for '%s' (entities=%d brushes=%d attempted=%zu built=%zu faces=%zu)",
                    meshName.c_str(),
                    document.entityCount(),
                    document.brushCount(),
                    attemptedBrushes,
                    builtBrushes,
                    generatedFaces);
        if (error)
            *error = "no brush geometry generated from map";
        return false;
    }

    size_t preOptimizeVertices = 0;
    size_t preOptimizeIndices = 0;
    for (SurfaceGroup &group : groups)
    {
        preOptimizeVertices += group.vertices.size();
        preOptimizeIndices += group.indices.size();
        optimizeSurfaceGroup(group);
    }

    for (const SurfaceGroup &group : groups)
    {
        if (group.vertices.empty() || group.indices.empty())
            continue;

        const uint32_t vertexOffset = static_cast<uint32_t>(outMesh.buffer.vertices.size());
        const uint32_t surfaceStart = static_cast<uint32_t>(outMesh.buffer.indices.size());

        outMesh.buffer.vertices.insert(outMesh.buffer.vertices.end(),
                                       group.vertices.begin(),
                                       group.vertices.end());
        for (uint32_t index : group.indices)
            outMesh.buffer.indices.push_back(vertexOffset + index);

        Material *material = new Material();
        material->name = meshName + "::" + group.textureName;
        material->type = MaterialType::Textured;
        material->setCullFace(false);
        material->setVec4("u_color", glm::vec4(1.0f));
        material->setTexture("u_albedo", group.texture ? group.texture : white);

        const int materialIndex = outMesh.add_material(material);
        outMesh.add_surface(surfaceStart,
                            static_cast<uint32_t>(outMesh.buffer.indices.size()) - surfaceStart,
                            materialIndex);
    }

    if (outMesh.buffer.vertices.empty() || outMesh.buffer.indices.empty())
    {
        if (error)
            *error = "map build produced no renderable triangles";
        return false;
    }

    outMesh.compute_tangents();
    outMesh.upload();

    if (outResult)
    {
        outResult->mesh = &outMesh;
        outResult->bounds = outMesh.aabb;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[TextMap] Built '%s': verts=%zu tris=%zu surfaces=%zu collision=%zu attemptedBrushes=%zu builtBrushes=%zu generatedFaces=%zu preOptimizeVerts=%zu preOptimizeTris=%zu",
                meshName.c_str(),
                outMesh.buffer.vertices.size(),
                outMesh.buffer.indices.size() / 3u,
                outMesh.surfaces.size(),
                outResult ? outResult->collisionTriangles.size() : 0u,
                attemptedBrushes,
                builtBrushes,
                generatedFaces,
                preOptimizeVertices,
                preOptimizeIndices / 3u);
    return true;
}
