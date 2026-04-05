#include "Manager.hpp"
#include "BinaryStream.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{
struct Md2Header
{
    int ident = 0;
    int version = 0;
    int skinWidth = 0;
    int skinHeight = 0;
    int frameSize = 0;
    int numSkins = 0;
    int numVerts = 0;
    int numUV = 0;
    int numTris = 0;
    int numGLCmds = 0;
    int numFrames = 0;
    int ofsSkins = 0;
    int ofsUV = 0;
    int ofsTris = 0;
    int ofsFrames = 0;
    int ofsGLCmds = 0;
    int ofsEnd = 0;
};

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


struct Md2Tri
{
    uint16_t vi[3] = {};
    uint16_t ti[3] = {};
};

struct Md2UV
{
    int16_t u = 0;
    int16_t v = 0;
};

struct Md2VertexKey
{
    uint16_t vi = 0;
    uint16_t ti = 0;

    bool operator==(const Md2VertexKey &other) const
    {
        return vi == other.vi && ti == other.ti;
    }
};

struct Md2VertexKeyHash
{
    std::size_t operator()(const Md2VertexKey &key) const
    {
        return (std::size_t(key.vi) << 16u) ^ std::size_t(key.ti);
    }
};

constexpr int kMd2Ident = 844121161; // "IDP2"
constexpr int kMd2Version = 8;
constexpr int kMd3Ident = 860898377; // IDP3
constexpr int kMd3Version = 15;

glm::vec3 md2_to_engine_space(const glm::vec3 &p)
{
    // Preserve handedness while converting the MD2 basis to the engine basis.
    // A plain Y/Z swap would mirror the mesh.
    return glm::vec3(p.x, p.z, p.y);
}

 
std::string read_fixed_string(BinaryStream &s, size_t size)
{
    std::string out(size, '\0');
    s.readRaw(out.data(), size);
    const size_t end = out.find('\0');
    if (end != std::string::npos)
        out.resize(end);
    return out;
}

std::string trim(std::string value)
{
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](char c) { return !is_space((unsigned char)c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](char c) { return !is_space((unsigned char)c); }).base(),
                value.end());
    return value;
}

std::string lower_string(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

bool file_exists(const std::string &path)
{
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw)
        return false;
    SDL_RWclose(rw);
    return true;
}

std::string dir_of(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return {};
    return path.substr(0, slash);
}

std::string base_of(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return path;
    return path.substr(slash + 1);
}

std::string stem_of(const std::string &path)
{
    std::string base = base_of(path);
    const size_t dot = base.find_last_of('.');
    if (dot == std::string::npos)
        return base;
    return base.substr(0, dot);
}

std::string join_path(const std::string &a, const std::string &b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    if (b[0] == '/' || b[0] == '\\' || (b.size() > 1 && b[1] == ':'))
        return b;
    if (a.back() == '/' || a.back() == '\\')
        return a + b;
    return a + "/" + b;
}

} // namespace

VertexAnimatedMeshManager &VertexAnimatedMeshManager::instance()
{
    static VertexAnimatedMeshManager inst;
    return inst;
}

VertexAnimatedMesh *VertexAnimatedMeshManager::create(const std::string &name)
{
    if (has(name))
        return get(name);

    auto *mesh = new VertexAnimatedMesh();
    mesh->name = name;
    cache[name] = mesh;
    return mesh;
}

VertexAnimatedMesh *VertexAnimatedMeshManager::load(const std::string &name,
                                                    const std::string &path,
                                                    const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    const auto dot = path.rfind('.');
    if (dot == std::string::npos)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[VertexAnimatedMeshManager] No extension in path: %s", path.c_str());
        return nullptr;
    }

    const std::string ext = path.substr(dot + 1);
    if (ext == "md2")
        return load_md2(name, path, texture_dir);
    if (ext == "md3")
        return load_md3(name, path, texture_dir);

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[VertexAnimatedMeshManager] Unknown vertex anim mesh format '%s': %s",
                ext.c_str(), path.c_str());
    return nullptr;
}

VertexAnimatedMesh *VertexAnimatedMeshManager::load_md2(const std::string &name,
                                                        const std::string &path,
                                                        const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    BinaryStream s(path, "rb");
    if (!s.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD2] Failed to open %s", path.c_str());
        return nullptr;
    }

    Md2Header h;
    h.ident = s.readI32();
    h.version = s.readI32();
    h.skinWidth = s.readI32();
    h.skinHeight = s.readI32();
    h.frameSize = s.readI32();
    h.numSkins = s.readI32();
    h.numVerts = s.readI32();
    h.numUV = s.readI32();
    h.numTris = s.readI32();
    h.numGLCmds = s.readI32();
    h.numFrames = s.readI32();
    h.ofsSkins = s.readI32();
    h.ofsUV = s.readI32();
    h.ofsTris = s.readI32();
    h.ofsFrames = s.readI32();
    h.ofsGLCmds = s.readI32();
    h.ofsEnd = s.readI32();

    if (h.ident != kMd2Ident || h.version != kMd2Version ||
        h.numFrames <= 0 || h.numVerts <= 0 || h.numTris <= 0 || h.numUV <= 0 ||
        h.skinWidth <= 0 || h.skinHeight <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD2] Invalid MD2 header: %s", path.c_str());
        return nullptr;
    }

    std::string firstSkin;
    if (h.numSkins > 0)
    {
        s.seek(h.ofsSkins);
        char skinName[64] = {};
        s.readRaw(skinName, sizeof(skinName));
        firstSkin = skinName;
    }

    std::vector<Md2UV> uvs(h.numUV);
    s.seek(h.ofsUV);
    for (int i = 0; i < h.numUV; ++i)
    {
        uvs[i].u = s.readI16();
        uvs[i].v = s.readI16();
    }

    std::vector<Md2Tri> tris(h.numTris);
    s.seek(h.ofsTris);
    for (int i = 0; i < h.numTris; ++i)
    {
        for (int k = 0; k < 3; ++k)
            tris[i].vi[k] = s.readU16();
        for (int k = 0; k < 3; ++k)
            tris[i].ti[k] = s.readU16();
    }

    auto *mesh = new VertexAnimatedMesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve(std::size_t(h.numTris) * 3);
    mesh->buffer.indices.reserve(std::size_t(h.numTris) * 3);

    std::unordered_map<Md2VertexKey, uint16_t, Md2VertexKeyHash> vertexMap;
    std::vector<uint16_t> sourceVertexForRenderVertex;
    vertexMap.reserve(std::size_t(h.numTris) * 3);
    sourceVertexForRenderVertex.reserve(std::size_t(h.numTris) * 3);

    for (const Md2Tri &tri : tris)
    {
        for (int k = 0; k < 3; ++k)
        {
            Md2VertexKey key{tri.vi[k], tri.ti[k]};
            auto it = vertexMap.find(key);
            uint16_t renderIndex;

            if (it == vertexMap.end())
            {
                renderIndex = (uint16_t)mesh->buffer.vertices.size();
                vertexMap.emplace(key, renderIndex);
                sourceVertexForRenderVertex.push_back(key.vi);

                VertexAnimVertex v{};
                v.uv = glm::vec2((float)uvs[key.ti].u / (float)h.skinWidth,
                                  (float)uvs[key.ti].v / (float)h.skinHeight);
                mesh->buffer.vertices.push_back(v);
            }
            else
            {
                renderIndex = it->second;
            }

            mesh->buffer.indices.push_back(renderIndex);
        }
    }

    mesh->frameNames.resize(h.numFrames);
    mesh->buffer.positions.resize(mesh->buffer.vertices.size());
    mesh->framePositions.resize(std::size_t(h.numFrames) * mesh->buffer.vertices.size());

    std::vector<glm::vec3> frameVerts(h.numVerts);
    s.seek(h.ofsFrames);
    for (int frame = 0; frame < h.numFrames; ++frame)
    {
        glm::vec3 scale;
        glm::vec3 translate;
        scale.x = s.readF32(); scale.y = s.readF32(); scale.z = s.readF32();
        translate.x = s.readF32(); translate.y = s.readF32(); translate.z = s.readF32();

        char frameName[16] = {};
        s.readRaw(frameName, sizeof(frameName));
        mesh->frameNames[frame] = frameName;

        for (int v = 0; v < h.numVerts; ++v)
        {
            const uint8_t x = s.readU8();
            const uint8_t y = s.readU8();
            const uint8_t z = s.readU8();
            (void)s.readU8();
            const glm::vec3 raw = glm::vec3((float)x, (float)y, (float)z) * scale + translate;
            frameVerts[v] = md2_to_engine_space(raw);
        }

        for (std::size_t i = 0; i < sourceVertexForRenderVertex.size(); ++i)
        {
            const glm::vec3 p = frameVerts[sourceVertexForRenderVertex[i]];
            if (frame == 0)
            {
                mesh->buffer.vertices[i].position = p;
                mesh->buffer.positions[i] = p;
            }
            mesh->framePositions[std::size_t(frame) * sourceVertexForRenderVertex.size() + i] = p;
        }
    }

    mesh->compute_normals();
    mesh->add_surface(0, (uint32_t)mesh->buffer.indices.size(), 0);

    auto *mat = new Material();
    mat->name = name;
    mat->type = MaterialType::Custom;
    if (!firstSkin.empty())
    {
        std::string texPath;
        if (!texture_dir.empty())
            texPath = (texture_dir.back() == '/' || texture_dir.back() == '\\') ? texture_dir + firstSkin : texture_dir + "/" + firstSkin;
        else
        {
            const auto slash = path.find_last_of("/\\");
            texPath = (slash == std::string::npos) ? firstSkin : path.substr(0, slash + 1) + firstSkin;
        }

        Texture *tex = TextureManager::instance().load(firstSkin, texPath);
        if (tex)
            mat->setTexture("u_albedo", tex);
    }
    mesh->materials.push_back(mat);

    mesh->upload();
    cache[name] = mesh;
    return mesh;
}

 

VertexAnimatedMesh *VertexAnimatedMeshManager::load_md3(const std::string &name, const std::string &path, const std::string &texture_dir)
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
    h.name = read_fixed_string(s, 64);
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

    std::string modelDir = dir_of(path);
    std::string skinFile;
    std::string textureBaseDir = texture_dir;
    if (!texture_dir.empty())
    {
        const std::string lower = lower_string(texture_dir);
        if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".skin")
        {
            skinFile = texture_dir;
            textureBaseDir = dir_of(texture_dir);
        }
    }
    if (skinFile.empty())
    {
        const std::string candidate = join_path(modelDir, stem_of(path) + "_default.skin");
        if (file_exists(candidate))
            skinFile = candidate;
    }

    std::unordered_map<std::string, std::string> skinMap;
    if (!skinFile.empty())
    {
        std::ifstream in(skinFile);
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty())
                continue;
            const size_t comma = line.find(',');
            if (comma == std::string::npos)
                continue;
            std::string surfaceName = lower_string(trim(line.substr(0, comma)));
            std::string texRef = trim(line.substr(comma + 1));
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
            mesh->frameNames[frame] = read_fixed_string(s, 16);
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
                std::string tagName = read_fixed_string(s, 64);
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
        info.name = read_fixed_string(s, 64);
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
            shaderName = read_fixed_string(s, 64);
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
        auto skinIt = skinMap.find(lower_string(info.name));
        if (skinIt != skinMap.end())
            textureRef = skinIt->second;
        else
            textureRef = shaderName;

        Texture *tex = nullptr;
        if (!textureRef.empty())
        {
            std::vector<std::string> tries;
            tries.push_back(textureRef);
            tries.push_back(join_path(modelDir, textureRef));
            tries.push_back(join_path(modelDir, base_of(textureRef)));
            if (!textureBaseDir.empty())
            {
                tries.push_back(join_path(textureBaseDir, textureRef));
                tries.push_back(join_path(textureBaseDir, base_of(textureRef)));
            }

            for (const std::string &candidate : tries)
            {
                if (!file_exists(candidate))
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

    //mesh->compute_normals();
    mesh->upload();
    cache[name] = mesh;
    return mesh;
}
