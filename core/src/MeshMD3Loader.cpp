#include "Manager.hpp"
#include "BinaryStream.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace
{
struct Md3Header
{
    int ident = 0;
    int version = 0;
    std::string name;
    int flags = 0;
    int numFrames = 0;
    int numTags = 0;
    int numSurfaces = 0;
    int numSkins = 0;
    int ofsFrames = 0;
    int ofsTags = 0;
    int ofsSurfaces = 0;
    int ofsEnd = 0;
};

struct Md3SurfaceInfo
{
    int baseOffset = 0;
    std::string name;
    int numFrames = 0;
    int numShaders = 0;
    int numVerts = 0;
    int numTris = 0;
    int ofsTris = 0;
    int ofsShaders = 0;
    int ofsST = 0;
    int ofsXYZNormal = 0;
    int ofsEnd = 0;
};

constexpr int kMd3Ident = 860898377;
constexpr int kMd3Version = 15;

}

VertexAnimatedMesh *VertexAnimatedMeshManager::load_md3(const std::string &name,
                                                        const std::string &path,
                                                        const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    BinaryStream s(path, "rb");
    if (!s.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD3] Failed to open %s", path.c_str());
        return nullptr;
    }

    Md3Header h;
    h.ident = s.readI32();
    h.version = s.readI32();
    h.name = ReadFixedString(s, 64);
    h.flags = s.readI32();
    h.numFrames = s.readI32();
    h.numTags = s.readI32();
    h.numSurfaces = s.readI32();
    h.numSkins = s.readI32();
    h.ofsFrames = s.readI32();
    h.ofsTags = s.readI32();
    h.ofsSurfaces = s.readI32();
    h.ofsEnd = s.readI32();

    if (h.ident != kMd3Ident || h.version != kMd3Version ||
        h.numFrames <= 0 || h.numSurfaces <= 0 || h.ofsSurfaces <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD3] Invalid header: %s", path.c_str());
        return nullptr;
    }

    std::string modelDir = PathDirectory(path);
    std::string skinFile;
    std::string textureBaseDir = texture_dir;
    if (!texture_dir.empty())
    {
        const std::string lower = LowerString(texture_dir);
        if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".skin")
        {
            skinFile = texture_dir;
            textureBaseDir = PathDirectory(texture_dir);
        }
    }
    if (skinFile.empty())
    {
        const std::string candidate = PathJoin(modelDir, PathStem(path) + "_default.skin");
        if (FileExists(candidate))
            skinFile = candidate;
    }

    std::unordered_map<std::string, std::string> skinMap;
    if (!skinFile.empty())
    {
        std::ifstream in(skinFile);
        std::string line;
        while (std::getline(in, line))
        {
            line = TrimString(line);
            if (line.empty())
                continue;
            const size_t comma = line.find(',');
            if (comma == std::string::npos)
                continue;
            std::string surfaceName = LowerString(TrimString(line.substr(0, comma)));
            std::string texRef = TrimString(line.substr(comma + 1));
            if (surfaceName.empty() || texRef.empty())
                continue;
            if (surfaceName.rfind("tag_", 0) == 0)
                continue;
            skinMap[surfaceName] = texRef;
        }
    }

    auto *mesh = new VertexAnimatedMesh();
    mesh->name = name;
    mesh->frameNames.resize(h.numFrames);

    if (h.ofsFrames > 0)
    {
        s.seek(h.ofsFrames);
        for (int frame = 0; frame < h.numFrames; ++frame)
        {
            for (int i = 0; i < 10; ++i)
                (void)s.readF32();
            mesh->frameNames[frame] = ReadFixedString(s, 16);
        }
    }

    if (h.numTags > 0 && h.ofsTags > 0)
    {
        mesh->tagsPerFrame = h.numTags;
        mesh->tags.resize((size_t)h.numFrames * (size_t)h.numTags);

        s.seek(h.ofsTags);
        for (int frame = 0; frame < h.numFrames; ++frame)
        {
            for (int tagIndex = 0; tagIndex < h.numTags; ++tagIndex)
            {
                MeshTag tag{};
                std::string tagName = ReadFixedString(s, 64);
                std::snprintf(tag.tag, sizeof(tag.tag), "%s", tagName.c_str());

                glm::vec3 origin;
                origin.x = s.readF32();
                origin.y = s.readF32();
                origin.z = s.readF32();
                tag.origin = origin;

                for (int axis = 0; axis < 3; ++axis)
                {
                    glm::vec3 v;
                    v.x = s.readF32();
                    v.y = s.readF32();
                    v.z = s.readF32();
                    tag.axis[axis] = glm::normalize(v);
                }

                mesh->tags[(size_t)frame * (size_t)h.numTags + (size_t)tagIndex] = tag;
            }
        }
    }

    std::vector<Md3SurfaceInfo> surfaces;
    surfaces.reserve(h.numSurfaces);
    size_t totalVerts = 0;
    size_t totalIndices = 0;
    int surfaceOffset = h.ofsSurfaces;
    for (int i = 0; i < h.numSurfaces; ++i)
    {
        s.seek(surfaceOffset);
        Md3SurfaceInfo info;
        info.baseOffset = surfaceOffset;
        const int ident = s.readI32();
        info.name = ReadFixedString(s, 64);
        (void)s.readI32();
        info.numFrames = s.readI32();
        info.numShaders = s.readI32();
        info.numVerts = s.readI32();
        info.numTris = s.readI32();
        info.ofsTris = s.readI32();
        info.ofsShaders = s.readI32();
        info.ofsST = s.readI32();
        info.ofsXYZNormal = s.readI32();
        info.ofsEnd = s.readI32();

        if (ident != kMd3Ident || info.numFrames != h.numFrames ||
            info.numVerts <= 0 || info.numTris <= 0 || info.ofsEnd <= 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD3] Invalid surface in %s", path.c_str());
            delete mesh;
            return nullptr;
        }

        surfaces.push_back(info);
        totalVerts += (size_t)info.numVerts;
        totalIndices += (size_t)info.numTris * 3u;
        surfaceOffset += info.ofsEnd;
    }

    if (surfaces.empty() || totalVerts == 0 || totalIndices == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD3] No usable surfaces in %s", path.c_str());
        delete mesh;
        return nullptr;
    }

    mesh->buffer.vertices.resize(totalVerts);
    mesh->buffer.positions.resize(totalVerts);
    mesh->buffer.indices.reserve(totalIndices);
    mesh->framePositions.resize((size_t)h.numFrames * totalVerts);

    Texture *white = TextureManager::instance().getWhite();
    size_t vertexBase = 0;

    for (const Md3SurfaceInfo &info : surfaces)
    {
        std::string shaderName;
        if (info.numShaders > 0)
        {
            s.seek(info.baseOffset + info.ofsShaders);
            shaderName = ReadFixedString(s, 64);
            (void)s.readI32();
        }

        s.seek(info.baseOffset + info.ofsST);
        for (int v = 0; v < info.numVerts; ++v)
        {
            mesh->buffer.vertices[vertexBase + (size_t)v].uv.x = s.readF32();
            mesh->buffer.vertices[vertexBase + (size_t)v].uv.y = s.readF32();
        }

        const uint32_t indexStart = (uint32_t)mesh->buffer.indices.size();
        s.seek(info.baseOffset + info.ofsTris);
        for (int tri = 0; tri < info.numTris; ++tri)
        {
            int A = s.readI32();
            int B = s.readI32();
            int C = s.readI32();
            mesh->buffer.indices.push_back((uint16_t)(vertexBase + (size_t)C));
            mesh->buffer.indices.push_back((uint16_t)(vertexBase + (size_t)B));
            mesh->buffer.indices.push_back((uint16_t)(vertexBase + (size_t)A));
        }

        s.seek(info.baseOffset + info.ofsXYZNormal);
        for (int frame = 0; frame < h.numFrames; ++frame)
        {
            const size_t frameBase = (size_t)frame * totalVerts + vertexBase;
            for (int v = 0; v < info.numVerts; ++v)
            {
                glm::vec3 p;
                p.x = (float)s.readI16() / 64.0f;
                p.y = (float)s.readI16() / 64.0f;
                p.z = (float)s.readI16() / 64.0f;

                const uint8_t latByte = s.readU8();
                const uint8_t lngByte = s.readU8();

                mesh->framePositions[frameBase + (size_t)v] = p;

                if (frame == 0)
                {
                    const float lat = latByte * (2.0f * glm::pi<float>() / 255.0f);
                    const float lng = lngByte * (2.0f * glm::pi<float>() / 255.0f);

                    glm::vec3 normal;
                    normal.x = std::cos(lat) * std::sin(lng);
                    normal.y = std::sin(lat) * std::sin(lng);
                    normal.z = std::cos(lng);

                    mesh->buffer.vertices[vertexBase + (size_t)v].position = p;
                    mesh->buffer.vertices[vertexBase + (size_t)v].normal = glm::normalize(normal);
                    mesh->buffer.positions[vertexBase + (size_t)v] = p;
                }
            }
        }

        std::string textureRef;
        auto skinIt = skinMap.find(LowerString(info.name));
        if (skinIt != skinMap.end())
            textureRef = skinIt->second;
        else
            textureRef = shaderName;

        Texture *tex = nullptr;
        if (!textureRef.empty())
        {
            std::vector<std::string> tries;
            tries.push_back(textureRef);
            tries.push_back(PathJoin(modelDir, textureRef));
            tries.push_back(PathJoin(modelDir, PathFilename(textureRef)));
            if (!textureBaseDir.empty())
            {
                tries.push_back(PathJoin(textureBaseDir, textureRef));
                tries.push_back(PathJoin(textureBaseDir, PathFilename(textureRef)));
            }

            for (const std::string &candidate : tries)
            {
                if (!FileExists(candidate))
                    continue;
                tex = TextureManager::instance().load(candidate, candidate);
                if (tex)
                    break;
            }
        }

        auto *mat = new Material();
        mat->name = info.name.empty() ? (name + "_surface") : info.name;
        mat->type = MaterialType::Custom;
        mat->setVec4("u_color", glm::vec4(1.0f));
        mat->setTexture("u_albedo", tex ? tex : white);
        const int materialIndex = mesh->add_material(mat);
        mesh->add_surface(indexStart, (uint32_t)info.numTris * 3u, materialIndex);

        vertexBase += (size_t)info.numVerts;
    }

    mesh->upload();
    cache[name] = mesh;
    return mesh;
}
