#include "Md3Loader.hpp"

#include "BinaryStream.hpp"
#include "Manager.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

constexpr int kMd3Ident = 860898377; // IDP3
constexpr int kMd3Version = 15;

uint16_t readU16(BinaryStream &s)
{
    uint16_t v = 0;
    s.readRaw(&v, sizeof(v));
    return SDL_SwapLE16(v);
}

int16_t readI16(BinaryStream &s)
{
    return static_cast<int16_t>(readU16(s));
}

std::string trim(const std::string &x)
{
    size_t a = 0;
    while (a < x.size() && std::isspace(static_cast<unsigned char>(x[a])))
        ++a;

    size_t b = x.size();
    while (b > a && std::isspace(static_cast<unsigned char>(x[b - 1])))
        --b;

    return x.substr(a, b - a);
}

std::string toLower(std::string x)
{
    std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return x;
}

bool startsWith(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string stripFilePrefix(const std::string &p)
{
    if (startsWith(p, "file:"))
        return p.substr(5);
    return p;
}

std::string sanitizePath(std::string p)
{
    p = stripFilePrefix(p);
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

bool isAbsolutePath(const std::string &p)
{
    if (p.empty())
        return false;
    if (p[0] == '/' || p[0] == '\\')
        return true;
    return p.size() > 1 && p[1] == ':';
}

std::string dirnameOf(const std::string &path)
{
    const std::string p = sanitizePath(path);
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos)
        return std::string();
    return p.substr(0, slash);
}

std::string basenameOf(const std::string &path)
{
    const std::string p = sanitizePath(path);
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos)
        return p;
    return p.substr(slash + 1);
}

std::string stemOf(const std::string &path)
{
    std::string b = basenameOf(path);
    const size_t dot = b.find_last_of('.');
    if (dot == std::string::npos)
        return b;
    return b.substr(0, dot);
}

std::string extensionOf(const std::string &path)
{
    std::string b = basenameOf(path);
    const size_t dot = b.find_last_of('.');
    if (dot == std::string::npos)
        return std::string();
    return b.substr(dot);
}

std::string joinPath(const std::string &a, const std::string &b)
{
    if (b.empty())
        return a;
    if (a.empty())
        return b;
    if (isAbsolutePath(b))
        return b;
    if (a[a.size() - 1] == '/')
        return a + b;
    return a + "/" + b;
}

std::string readFixedString(BinaryStream &s, size_t size)
{
    std::vector<char> buf(size + 1, 0);
    s.readRaw(buf.data(), size);
    return std::string(buf.data());
}

bool fileExistsSDL(const std::string &path)
{
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw)
        return false;
    SDL_RWclose(rw);
    return true;
}

bool readTextFileSDL(const std::string &path, std::string &out)
{
    out.clear();

    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw)
        return false;

    const Sint64 sz = SDL_RWsize(rw);
    if (sz < 0)
    {
        SDL_RWclose(rw);
        return false;
    }

    out.assign(static_cast<size_t>(sz), '\0');
    if (sz > 0)
    {
        const size_t readBytes = SDL_RWread(rw, out.data(), 1, static_cast<size_t>(sz));
        out.resize(readBytes);
    }

    SDL_RWclose(rw);
    return true;
}

std::vector<std::string> fileCandidates(const std::string &path)
{
    std::vector<std::string> out;
    const std::string clean = sanitizePath(path);
    if (clean.empty())
        return out;

    out.push_back(clean);
    if (!isAbsolutePath(clean))
    {
        out.push_back(std::string("bin/") + clean);
        out.push_back(std::string("../") + clean);
        out.push_back(std::string("../../") + clean);
    }

    std::vector<std::string> uniq;
    for (const std::string &p : out)
    {
        if (std::find(uniq.begin(), uniq.end(), p) == uniq.end())
            uniq.push_back(p);
    }
    return uniq;
}

std::string resolveExistingPath(const std::string &path)
{
    const std::vector<std::string> tries = fileCandidates(path);
    for (const std::string &p : tries)
    {
        if (fileExistsSDL(p))
            return p;
    }
    return {};
}

glm::vec3 decodeNormal(uint8_t lat, uint8_t lng)
{
    const float latR = static_cast<float>(lat) * 6.28318530718f / 255.0f;
    const float lngR = static_cast<float>(lng) * 6.28318530718f / 255.0f;

    return glm::vec3(std::cos(latR) * std::sin(lngR),
                     std::sin(latR) * std::sin(lngR),
                     std::cos(lngR));
}

BoundingBox computeSurfaceAabb(const Md3SurfaceRuntime &surf)
{
    BoundingBox bb;
    for (const glm::vec3 &p : surf.framePositions)
        bb.expand(p);
    return bb;
}

glm::vec3 framePos(const Md3SurfaceRuntime &surf, int frame, int vert)
{
    if (surf.numFrames <= 0 || surf.numVerts <= 0)
        return glm::vec3(0.0f);

    frame = std::clamp(frame, 0, surf.numFrames - 1);
    vert = std::clamp(vert, 0, surf.numVerts - 1);

    const size_t idx = static_cast<size_t>(frame) * static_cast<size_t>(surf.numVerts) +
                       static_cast<size_t>(vert);
    if (idx >= surf.framePositions.size())
        return glm::vec3(0.0f);
    return surf.framePositions[idx];
}

glm::vec3 frameNormal(const Md3SurfaceRuntime &surf, int frame, int vert)
{
    if (surf.numFrames <= 0 || surf.numVerts <= 0)
        return glm::vec3(0.0f, 1.0f, 0.0f);

    frame = std::clamp(frame, 0, surf.numFrames - 1);
    vert = std::clamp(vert, 0, surf.numVerts - 1);

    const size_t idx = static_cast<size_t>(frame) * static_cast<size_t>(surf.numVerts) +
                       static_cast<size_t>(vert);
    if (idx >= surf.frameNormals.size())
        return glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::vec3 n = surf.frameNormals[idx];
    const float len2 = glm::dot(n, n);
    if (len2 > 1e-10f)
        return glm::normalize(n);
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

bool readSurface(BinaryStream &s,
                 const Md3Header &header,
                 int surfaceOfs,
                 Md3SurfaceRuntime &out,
                 int &surfaceSize)
{
    s.seek(surfaceOfs);

    (void)s.readI32(); // surface ident
    out.name = readFixedString(s, 64);
    (void)s.readI32(); // flags
    out.numFrames = s.readI32();
    const int numShaders = s.readI32();
    out.numVerts = s.readI32();
    out.numTris = s.readI32();

    const int ofsTris = s.readI32();
    const int ofsShaders = s.readI32();
    const int ofsST = s.readI32();
    const int ofsXYZNormal = s.readI32();
    surfaceSize = s.readI32();

    if (out.numFrames <= 0 || out.numVerts <= 0 || out.numTris <= 0 ||
        surfaceSize <= 0 || out.numFrames > header.numFrames)
    {
        return false;
    }

    if (numShaders > 0)
    {
        s.seek(surfaceOfs + ofsShaders);
        out.shaderName = readFixedString(s, 64);
        (void)s.readI32();
    }

    out.indices.assign(static_cast<size_t>(out.numTris) * 3u, 0u);
    s.seek(surfaceOfs + ofsTris);
    for (size_t i = 0; i < out.indices.size(); ++i)
    {
        const int idx = s.readI32();
        out.indices[i] = (idx >= 0) ? static_cast<uint32_t>(idx) : 0u;
    }

    out.texcoords.assign(static_cast<size_t>(out.numVerts), glm::vec2(0.0f));
    s.seek(surfaceOfs + ofsST);
    for (int v = 0; v < out.numVerts; ++v)
    {
        const float u = s.readF32();
        const float vv = s.readF32();
        out.texcoords[static_cast<size_t>(v)] = glm::vec2(u, vv);
    }

    out.framePositions.assign(static_cast<size_t>(out.numFrames) * static_cast<size_t>(out.numVerts),
                              glm::vec3(0.0f));
    out.frameNormals.assign(static_cast<size_t>(out.numFrames) * static_cast<size_t>(out.numVerts),
                            glm::vec3(0.0f, 1.0f, 0.0f));

    s.seek(surfaceOfs + ofsXYZNormal);
    for (int f = 0; f < out.numFrames; ++f)
    {
        const size_t frameBase = static_cast<size_t>(f) * static_cast<size_t>(out.numVerts);
        for (int v = 0; v < out.numVerts; ++v)
        {
            const float rx = static_cast<float>(readI16(s)) / 64.0f;
            const float ry = static_cast<float>(readI16(s)) / 64.0f;
            const float rz = static_cast<float>(readI16(s)) / 64.0f;
            const uint8_t lat = s.readU8();
            const uint8_t lng = s.readU8();

            const size_t dst = frameBase + static_cast<size_t>(v);
            out.framePositions[dst] = glm::vec3(rx, ry, rz);
            out.frameNormals[dst] = decodeNormal(lat, lng);
        }
    }

    out.aabb = computeSurfaceAabb(out);
    return out.ready();
}

void parseTags(BinaryStream &s,
               const Md3Header &h,
               std::vector<VertexTagTrack> &dst)
{
    dst.clear();
    if (h.numTags <= 0 || h.ofsTags <= 0)
        return;

    const int tagStride = 112;

    std::vector<std::string> names(static_cast<size_t>(h.numTags));
    for (int t = 0; t < h.numTags; ++t)
    {
        s.seek(h.ofsTags + t * tagStride);
        names[static_cast<size_t>(t)] = readFixedString(s, 64);
    }

    dst.resize(static_cast<size_t>(h.numTags));
    for (int t = 0; t < h.numTags; ++t)
    {
        VertexTagTrack track;
        track.name = names[static_cast<size_t>(t)];
        track.frames.resize(static_cast<size_t>(h.numFrames));

        for (int f = 0; f < h.numFrames; ++f)
        {
            const int ofs = h.ofsTags + (f * h.numTags + t) * tagStride;
            s.seek(ofs + 64);

            const glm::vec3 org(s.readF32(), s.readF32(), s.readF32());

            float a[9] = {};
            for (float &it : a)
                it = s.readF32();

            glm::mat3 m(1.0f);
            m[0] = glm::vec3(a[0], a[1], a[2]);
            m[1] = glm::vec3(a[3], a[4], a[5]);
            m[2] = glm::vec3(a[6], a[7], a[8]);

            VertexTagFrame tf;
            tf.position = org;
            tf.rotation = glm::normalize(glm::quat_cast(m));
            tf.scale = glm::vec3(1.0f);

            track.frames[static_cast<size_t>(f)] = tf;
        }

        dst[static_cast<size_t>(t)] = track;
    }
}

std::vector<std::string> textureCandidates(const std::string &modelPath,
                                           const std::string &ref)
{
    const std::string modelDir = dirnameOf(modelPath);
    const std::string parentDir = dirnameOf(modelDir);
    const std::string cleanRef = sanitizePath(ref);

    std::vector<std::string> out;

    if (!cleanRef.empty())
    {
        out.push_back(cleanRef);
        out.push_back(joinPath(modelDir, cleanRef));

        const std::string base = basenameOf(cleanRef);
        out.push_back(joinPath(modelDir, base));
        out.push_back(joinPath(parentDir, base));

        if (extensionOf(base).empty())
        {
            static const char *kExts[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
            for (const char *ext : kExts)
            {
                out.push_back(joinPath(modelDir, base + ext));
                out.push_back(joinPath(parentDir, base + ext));
            }
        }
    }

    const std::string modelStem = stemOf(modelPath);
    static const char *kExts[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    for (const char *ext : kExts)
        out.push_back(joinPath(modelDir, modelStem + ext));

    std::vector<std::string> uniq;
    for (const std::string &p : out)
    {
        if (p.empty())
            continue;
        if (std::find(uniq.begin(), uniq.end(), p) == uniq.end())
            uniq.push_back(p);
    }
    return uniq;
}

std::map<std::string, std::string> loadSkinMap(const std::string &modelPath,
                                               const std::string &skinPath)
{
    std::map<std::string, std::string> skin;

    std::string resolvedSkin = skinPath.empty() ? std::string() : resolveExistingPath(skinPath);
    if (resolvedSkin.empty())
    {
        const std::string modelDir = dirnameOf(modelPath);
        const std::string modelStem = stemOf(modelPath);
        resolvedSkin = resolveExistingPath(joinPath(modelDir, modelStem + "_default.skin"));
    }

    if (resolvedSkin.empty())
        return skin;

    std::string text;
    if (!readTextFileSDL(resolvedSkin, text))
        return skin;

    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.empty() || startsWith(line, "//"))
            continue;

        const size_t comma = line.find(',');
        if (comma == std::string::npos)
            continue;

        const std::string surf = toLower(trim(line.substr(0, comma)));
        const std::string tex = trim(line.substr(comma + 1));
        if (surf.empty() || tex.empty())
            continue;
        if (startsWith(surf, "tag_"))
            continue;

        skin[surf] = tex;
    }

    return skin;
}

Texture *resolveSurfaceTexture(const std::string &modelPath,
                               const Md3SurfaceRuntime &surf,
                               const std::map<std::string, std::string> &skinMap)
{
    std::string ref;

    const std::string key = toLower(trim(surf.name));
    auto it = skinMap.find(key);
    if (it != skinMap.end())
        ref = it->second;
    else
        ref = surf.shaderName;

    auto &texMgr = TextureManager::instance();
    const std::vector<std::string> tries = textureCandidates(modelPath, ref);
    for (const std::string &p : tries)
    {
        const std::string existing = resolveExistingPath(p);
        if (existing.empty())
            continue;

        const std::string texKey = std::string("md3:") + existing;
        Texture *tex = texMgr.load(texKey, existing);
        if (tex)
            return tex;
    }

    return nullptr;
}

} // namespace

bool Md3Loader::load(const std::string &modelPath,
                     const Options &options,
                     Mesh *outMesh,
                     Md3PartRuntime *outRuntime,
                     VertexAnimController *outController) const
{
    if (!outMesh)
    {
        LogError("[Md3Loader] outMesh is null");
        return false;
    }

    const std::string model = resolveExistingPath(modelPath);
    if (model.empty())
    {
        LogError("[Md3Loader] model not found: %s", modelPath.c_str());
        return false;
    }

    BinaryStream s(model, "rb");
    if (!s.isOpen())
    {
        LogError("[Md3Loader] failed to open: %s", model.c_str());
        return false;
    }

    Md3Header h;
    h.ident = s.readI32();
    h.version = s.readI32();
    h.name = readFixedString(s, 64);
    h.flags = s.readI32();
    h.numFrames = s.readI32();
    h.numTags = s.readI32();
    h.numSurfaces = s.readI32();
    h.numSkins = s.readI32();
    h.ofsFrames = s.readI32();
    h.ofsTags = s.readI32();
    h.ofsSurfaces = s.readI32();
    h.ofsEnd = s.readI32();

    const Sint64 fileSize = s.size();
    if (h.ident != kMd3Ident || h.version != kMd3Version ||
        h.numFrames <= 0 || h.numSurfaces <= 0 ||
        h.ofsSurfaces <= 0 || h.ofsEnd <= 0 || h.ofsEnd > fileSize)
    {
        LogError("[Md3Loader] invalid header for %s", model.c_str());
        return false;
    }

    Md3PartRuntime local;
    local.id = options.partId.empty() ? stemOf(model) : options.partId;

    const std::map<std::string, std::string> skinMap = loadSkinMap(model, options.skinPath);

    local.surfaces.clear();
    local.surfaces.reserve(static_cast<size_t>(h.numSurfaces));

    int surfaceOfs = h.ofsSurfaces;
    for (int i = 0; i < h.numSurfaces; ++i)
    {
        Md3SurfaceRuntime surf;
        int surfSize = 0;
        if (!readSurface(s, h, surfaceOfs, surf, surfSize))
        {
            LogWarning("[Md3Loader] skipped invalid surface #%d", i);
            break;
        }

        local.surfaces.push_back(std::move(surf));
        surfaceOfs += surfSize;
    }

    if (local.surfaces.empty())
    {
        LogError("[Md3Loader] no valid surfaces in %s", model.c_str());
        return false;
    }

    parseTags(s, h, local.tagTracks);
    local.frameCount = local.surfaces[0].numFrames;

    outMesh->name = options.meshName.empty() ? local.id : options.meshName;
    outMesh->buffer.dynamic = true;
    outMesh->buffer.vertices.clear();
    outMesh->buffer.indices.clear();
    outMesh->surfaces.clear();
    outMesh->materials.clear();

    auto &matMgr = MaterialManager::instance();
    auto &texMgr = TextureManager::instance();

    BoundingBox global;
    bool anySurface = false;

    for (size_t i = 0; i < local.surfaces.size(); ++i)
    {
        Md3SurfaceRuntime &src = local.surfaces[i];
        if (!src.ready())
            continue;

        anySurface = true;

        src.vertexStart = static_cast<uint32_t>(outMesh->buffer.vertices.size());
        for (int v = 0; v < src.numVerts; ++v)
        {
            Vertex vx;
            vx.position = src.framePositions[static_cast<size_t>(v)];
            vx.normal = src.frameNormals[static_cast<size_t>(v)];
            vx.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            vx.uv = src.texcoords[static_cast<size_t>(v)];
            outMesh->buffer.vertices.push_back(vx);
        }

        src.indexStart = static_cast<uint32_t>(outMesh->buffer.indices.size());
        for (uint32_t idx : src.indices)
        {
            if (idx >= static_cast<uint32_t>(src.numVerts))
                idx = 0;
            outMesh->buffer.indices.push_back(src.vertexStart + idx);
        }
        src.indexCount = static_cast<uint32_t>(outMesh->buffer.indices.size()) - src.indexStart;

        src.aabb = computeSurfaceAabb(src);
        global.expand(src.aabb);

        std::ostringstream matName;
        matName << (options.materialPrefix.empty() ? "md3_mat" : options.materialPrefix)
                << "_" << local.id << "_" << i << "_" << toLower(src.name);

        Material *mat = matMgr.create(matName.str());
        if (mat)
        {
            if (options.shader)
                mat->setShader(options.shader);

            mat->setCullFace(!options.disableBackfaceCulling ? true : false);

            Texture *tex = resolveSurfaceTexture(model, src, skinMap);
            if (!tex)
                tex = texMgr.getPattern();

            mat->setTexture("u_albedo", tex);
            mat->setVec3("u_color", glm::vec3(1.0f));
        }

        const int matIndex = static_cast<int>(outMesh->materials.size());
        outMesh->materials.push_back(mat);

        Surface &surf = outMesh->add_surface(src.indexStart, src.indexCount, matIndex);
        surf.aabb = src.aabb;
    }

    if (!anySurface)
    {
        LogError("[Md3Loader] no drawable surfaces in %s", model.c_str());
        return false;
    }

    outMesh->aabb = global;
    local.aabb = global;
    outMesh->buffer.upload();

    if (outRuntime)
        *outRuntime = std::move(local);

    if (outController)
    {
        *outController = VertexAnimController();
        const int last = std::max(0, h.numFrames - 1);
        outController->addClip(VertexAnimClip{"all", 0, last, 10.0f, true});
    }

    const size_t surfaceCount = outRuntime ? outRuntime->surfaces.size() : local.surfaces.size();
    const size_t tagCount = outRuntime ? outRuntime->tagTracks.size() : local.tagTracks.size();
    LogInfo("[Md3Loader] loaded: %s frames=%d surfaces=%zu tags=%zu",
            model.c_str(),
            h.numFrames,
            surfaceCount,
            tagCount);

    return true;
}

Mesh *Md3Loader::load(const std::string &modelPath,
                      const Options &options,
                      Md3PartRuntime *outRuntime,
                      VertexAnimController *outController) const
{
    const std::string meshName = options.meshName.empty() ? "md3_mesh" : options.meshName;
    Mesh *mesh = MeshManager::instance().create(meshName);
    if (!mesh)
        return nullptr;

    if (!load(modelPath, options, mesh, outRuntime, outController))
        return nullptr;

    return mesh;
}

static void applyMd3AssetRuntimeSample(Mesh *mesh,
                                       const VertexAnimSample &sample,
                                       const void *userData)
{
    const Md3PartRuntime *rt = static_cast<const Md3PartRuntime *>(userData);
    if (!rt)
        return;
    Md3Loader::applySample(mesh, *rt, sample);
}

bool Md3Loader::buildAsset(Mesh *templateMesh,
                           const Md3PartRuntime &runtime,
                           VertexAnimAsset *outAsset)
{
    if (!templateMesh || !runtime.valid() || !outAsset)
        return false;

    outAsset->templateMesh = templateMesh;
    outAsset->clips.clear();
    outAsset->tags = runtime.tagTracks;
    outAsset->clips.emplace_back("all", 0, std::max(0, runtime.frameCount - 1), 10.0f, true);
    outAsset->applySample = &applyMd3AssetRuntimeSample;
    outAsset->applyUserData = &runtime;

    return outAsset->valid();
}

void Md3Loader::applySample(Mesh *mesh,
                            const Md3PartRuntime &runtime,
                            const VertexAnimSample &sample)
{
    if (!mesh || !runtime.valid())
        return;

    auto &verts = mesh->buffer.vertices;
    if (verts.empty())
        return;

    const float tCur = std::clamp(sample.currentInterp, 0.0f, 1.0f);
    const float tPrev = std::clamp(sample.previousInterp, 0.0f, 1.0f);
    const float tBlend = std::clamp(sample.clipBlend, 0.0f, 1.0f);

    for (const Md3SurfaceRuntime &surf : runtime.surfaces)
    {
        if (!surf.ready())
            continue;

        for (int v = 0; v < surf.numVerts; ++v)
        {
            glm::vec3 pos = glm::mix(framePos(surf, sample.currentFrame, v),
                                     framePos(surf, sample.nextFrame, v),
                                     tCur);

            glm::vec3 nrm = glm::mix(frameNormal(surf, sample.currentFrame, v),
                                     frameNormal(surf, sample.nextFrame, v),
                                     tCur);

            if (sample.hasPrevious && tBlend > 0.0f)
            {
                const glm::vec3 prevPos = glm::mix(framePos(surf, sample.previousFrame, v),
                                                   framePos(surf, sample.previousNextFrame, v),
                                                   tPrev);
                const glm::vec3 prevNrm = glm::mix(frameNormal(surf, sample.previousFrame, v),
                                                   frameNormal(surf, sample.previousNextFrame, v),
                                                   tPrev);

                pos = glm::mix(pos, prevPos, tBlend);
                nrm = glm::mix(nrm, prevNrm, tBlend);
            }

            const float len2 = glm::dot(nrm, nrm);
            if (len2 > 1e-10f)
                nrm = glm::normalize(nrm);
            else
                nrm = glm::vec3(0.0f, 1.0f, 0.0f);

            const uint32_t dst = surf.vertexStart + static_cast<uint32_t>(v);
            if (dst < verts.size())
            {
                verts[dst].position = pos;
                verts[dst].normal = nrm;
            }
        }
    }

    mesh->buffer.update();
}

bool Md3Loader::parseAnimationCfg(const std::string &cfgPath,
                                  std::vector<Md3AnimCfgClip> &outClips)
{
    outClips.clear();

    const std::string resolved = resolveExistingPath(cfgPath);
    if (resolved.empty())
    {
        LogWarning("[Md3Loader] animation.cfg not found: %s", cfgPath.c_str());
        return false;
    }

    std::string text;
    if (!readTextFileSDL(resolved, text))
        return false;

    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        std::string name;

        const size_t comment = line.find("//");
        if (comment != std::string::npos)
        {
            name = trim(line.substr(comment + 2));
            line = line.substr(0, comment);
        }

        line = trim(line);
        if (line.empty())
            continue;

        std::istringstream ls(line);
        int start = 0;
        int count = 0;
        int loopingFrames = 0;
        int fps = 0;
        if (!(ls >> start >> count >> loopingFrames >> fps))
            continue;

        if (count <= 0)
            continue;

        Md3AnimCfgClip c;
        c.name = name.empty() ? ("clip_" + std::to_string(outClips.size())) : name;
        c.first = start;
        c.last = start + count - 1;
        c.fps = (fps > 0) ? static_cast<float>(fps) : 10.0f;
        c.loop = (loopingFrames != 0);

        outClips.push_back(std::move(c));
    }

    return !outClips.empty();
}

void Md3Loader::applyClipsFromCfg(const std::vector<Md3AnimCfgClip> &clips,
                                  const std::string &requiredPrefix,
                                  int frameCount,
                                  VertexAnimController *outController,
                                  std::vector<std::string> *outClipNames,
                                  bool forceLoop)
{
    if (!outController)
        return;

    *outController = VertexAnimController();
    if (outClipNames)
        outClipNames->clear();

    if (frameCount <= 0)
        return;

    const std::string prefixLower = toLower(requiredPrefix);

    bool addedAny = false;
    for (const Md3AnimCfgClip &c : clips)
    {
        std::string lowerName = toLower(c.name);
        if (!prefixLower.empty() && !startsWith(lowerName, prefixLower))
            continue;

        std::string finalName = c.name;
        if (!prefixLower.empty() && startsWith(lowerName, prefixLower))
            finalName = trim(lowerName.substr(prefixLower.size()));
        if (finalName.empty())
            finalName = c.name;

        const int first = std::clamp(c.first, 0, frameCount - 1);
        const int last = std::clamp(c.last, first, frameCount - 1);
        const float fps = std::max(c.fps, 0.01f);
        const bool loop = forceLoop ? true : c.loop;

        outController->addClip(VertexAnimClip{finalName, first, last, fps, loop});
        addedAny = true;
        if (outClipNames)
            outClipNames->push_back(finalName);
    }

    if (!addedAny)
    {
        outController->addClip(VertexAnimClip{"all", 0, frameCount - 1, 10.0f, true});
        if (outClipNames)
            outClipNames->push_back("all");
    }
}
