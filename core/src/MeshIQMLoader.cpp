#include "Manager.hpp"
#include "Animation.hpp"
#include "BinaryStream.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
constexpr uint32_t IQM_VERSION = 2;

constexpr uint32_t IQM_POSITION = 0;
constexpr uint32_t IQM_TEXCOORD = 1;
constexpr uint32_t IQM_NORMAL = 2;
constexpr uint32_t IQM_BLENDINDEXES = 4;
constexpr uint32_t IQM_BLENDWEIGHTS = 5;

constexpr uint32_t IQM_UBYTE = 1;
constexpr uint32_t IQM_FLOAT = 7;
constexpr uint32_t IQM_LOOP = 1;

#pragma pack(push, 1)
struct IqmHeader
{
    char magic[16];
    uint32_t version;
    uint32_t filesize;
    uint32_t flags;
    uint32_t num_text;
    uint32_t ofs_text;
    uint32_t num_meshes;
    uint32_t ofs_meshes;
    uint32_t num_vertexarrays;
    uint32_t num_vertexes;
    uint32_t ofs_vertexarrays;
    uint32_t num_triangles;
    uint32_t ofs_triangles;
    uint32_t ofs_adjacency;
    uint32_t num_joints;
    uint32_t ofs_joints;
    uint32_t num_poses;
    uint32_t ofs_poses;
    uint32_t num_anims;
    uint32_t ofs_anims;
    uint32_t num_frames;
    uint32_t num_framechannels;
    uint32_t ofs_frames;
    uint32_t ofs_bounds;
    uint32_t num_comment;
    uint32_t ofs_comment;
    uint32_t num_extensions;
    uint32_t ofs_extensions;
};

struct IqmMesh
{
    uint32_t name;
    uint32_t material;
    uint32_t first_vertex;
    uint32_t num_vertexes;
    uint32_t first_triangle;
    uint32_t num_triangles;
};

struct IqmTriangle
{
    uint32_t vertex[3];
};

struct IqmJoint
{
    uint32_t name;
    int32_t parent;
    float translate[3];
    float rotate[4];
    float scale[3];
};

struct IqmPose
{
    int32_t parent;
    uint32_t channelmask;
    float channeloffset[10];
    float channelscale[10];
};

struct IqmAnim
{
    uint32_t name;
    uint32_t first_frame;
    uint32_t num_frames;
    float framerate;
    uint32_t flags;
};

struct IqmVertexArray
{
    uint32_t type;
    uint32_t flags;
    uint32_t format;
    uint32_t size;
    uint32_t offset;
};
#pragma pack(pop)

struct FrameGroupClip
{
    std::string name;
    int firstFrame = 0;
    int numFrames = 0;
    float fps = 24.0f;
    bool loop = true;
};

static_assert(sizeof(IqmHeader) == 124, "IQM header size mismatch");
static_assert(sizeof(IqmMesh) == 24, "IQM mesh size mismatch");
static_assert(sizeof(IqmTriangle) == 12, "IQM triangle size mismatch");
static_assert(sizeof(IqmJoint) == 48, "IQM joint size mismatch");
static_assert(sizeof(IqmPose) == 88, "IQM pose size mismatch");
static_assert(sizeof(IqmAnim) == 20, "IQM anim size mismatch");
static_assert(sizeof(IqmVertexArray) == 20, "IQM vertex-array size mismatch");

std::string iqmTextAt(const std::vector<char> &text, uint32_t ofs)
{
    if (ofs >= text.size())
        return {};
    return std::string(&text[ofs]);
}

template <typename T>
bool readArrayAt(BinaryStream &s,
                 Sint64 fileSize,
                 uint32_t ofs,
                 uint32_t count,
                 std::vector<T> &out)
{
    out.clear();
    if (count == 0)
        return true;
    if (ofs == 0)
        return false;

    const uint64_t bytes = uint64_t(count) * uint64_t(sizeof(T));
    const uint64_t end = uint64_t(ofs) + bytes;
    if (end > uint64_t(fileSize))
        return false;

    out.resize(count);
    s.seek(ofs);
    s.readRaw(out.data(), static_cast<size_t>(bytes));
    return true;
}

glm::mat4 makeTRS(const glm::vec3 &t, const glm::quat &r, const glm::vec3 &s)
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t);
    m *= glm::toMat4(r);
    return glm::scale(m, s);
}

void decodeFramePose(uint32_t frameIndex,
                     uint32_t numFrameChannels,
                     const std::vector<IqmPose> &poses,
                     const std::vector<uint16_t> &frameData,
                     std::vector<glm::vec3> &outPos,
                     std::vector<glm::quat> &outRot,
                     std::vector<glm::vec3> &outScale)
{
    const size_t poseCount = poses.size();
    outPos.resize(poseCount, glm::vec3(0.0f));
    outRot.resize(poseCount, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    outScale.resize(poseCount, glm::vec3(1.0f));

    if (poseCount == 0 || frameData.empty() || numFrameChannels == 0)
        return;

    const uint64_t base = uint64_t(frameIndex) * uint64_t(numFrameChannels);
    uint32_t p = 0;

    for (size_t i = 0; i < poseCount; ++i)
    {
        const IqmPose &pose = poses[i];
        float ch[10];
        for (int c = 0; c < 10; ++c)
            ch[c] = pose.channeloffset[c];

        for (int c = 0; c < 10; ++c)
        {
            if ((pose.channelmask & (1u << c)) == 0u)
                continue;
            const uint64_t idx = base + uint64_t(p);
            if (idx < frameData.size())
                ch[c] += float(frameData[idx]) * pose.channelscale[c];
            ++p;
        }

        outPos[i] = glm::vec3(ch[0], ch[1], ch[2]);

        glm::quat q(ch[6], ch[3], ch[4], ch[5]);
        const float qLen = glm::length(q);
        if (qLen > 1e-8f)
            q /= qLen;
        else
            q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        outRot[i] = q;

        outScale[i] = glm::vec3(ch[7], ch[8], ch[9]);
    }
}

glm::mat4 evalGlobalBindRecursive(int i,
                                  const std::vector<IqmJoint> &joints,
                                  const std::vector<glm::mat4> &localBind,
                                  std::vector<glm::mat4> &globalBind,
                                  std::vector<uint8_t> &state)
{
    const int jointCount = static_cast<int>(joints.size());
    if (i < 0 || i >= jointCount)
        return glm::mat4(1.0f);
    if (state[i] == 2)
        return globalBind[i];
    if (state[i] == 1)
        return localBind[i];

    state[i] = 1;
    const int parent = joints[i].parent;
    if (parent >= 0 && parent < jointCount)
        globalBind[i] = evalGlobalBindRecursive(parent, joints, localBind, globalBind, state) * localBind[i];
    else
        globalBind[i] = localBind[i];
    state[i] = 2;
    return globalBind[i];
}

bool parseSkinFile(const std::string &path,
                   std::unordered_map<std::string, std::string> &outMap)
{
    outMap.clear();
    std::ifstream in(path);
    if (!in.is_open())
        return false;

    std::string line;
    while (std::getline(in, line))
    {
        line = TrimString(line);
        if (line.empty() || line[0] == '#')
            continue;

        const size_t comma = line.find(',');
        if (comma == std::string::npos)
            continue;

        const std::string meshName = TrimString(line.substr(0, comma));
        const std::string texName = TrimString(line.substr(comma + 1));
        if (!meshName.empty() && !texName.empty())
            outMap[meshName] = texName;
    }
    return !outMap.empty();
}

bool parseFrameGroups(const std::string &path,
                      std::vector<FrameGroupClip> &outClips)
{
    outClips.clear();

    std::ifstream in(path);
    if (!in.is_open())
        return false;

    std::string line;
    while (std::getline(in, line))
    {
        line = TrimString(line);
        if (line.empty() || line[0] == '#')
            continue;

        std::string name;
        const size_t commentPos = line.find("//");
        if (commentPos != std::string::npos)
        {
            name = TrimString(line.substr(commentPos + 2));
            line = TrimString(line.substr(0, commentPos));
        }

        if (line.empty())
            continue;

        std::istringstream ss(line);
        FrameGroupClip clip;
        int loopInt = 1;
        if (!(ss >> clip.firstFrame >> clip.numFrames >> clip.fps >> loopInt))
            continue;

        if (clip.numFrames <= 0)
            continue;
        clip.loop = (loopInt != 0);
        if (clip.fps <= 0.0f)
            clip.fps = 24.0f;
        if (name.empty())
            name = "clip_" + std::to_string(outClips.size());
        clip.name = name;
        outClips.push_back(clip);
    }

    return !outClips.empty();
}

bool loadIQMAnimated(const std::string &name,
                     const std::string &iqmPath,
                     const std::string &textureDir,
                     AnimatedMesh *outMesh)
{
    if (!outMesh)
        return false;

    outMesh->name = name;
    outMesh->buffer.vertices.clear();
    outMesh->buffer.indices.clear();
    outMesh->surfaces.clear();
    outMesh->release_materials();
    outMesh->releaseAnimations();
    outMesh->bones.clear();
    outMesh->finalMatrices.clear();

    BinaryStream s(iqmPath, "rb");
    if (!s.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Failed to open: %s", iqmPath.c_str());
        return false;
    }

    const Sint64 fileSize = s.size();
    if (fileSize < static_cast<Sint64>(sizeof(IqmHeader)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] File too small: %s", iqmPath.c_str());
        return false;
    }

    IqmHeader hdr{};
    s.readRaw(&hdr, sizeof(hdr));

    const char iqmMagic[16] = "INTERQUAKEMODEL";
    if (std::memcmp(hdr.magic, iqmMagic, 15) != 0 || hdr.version != IQM_VERSION)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Invalid header/version in %s", iqmPath.c_str());
        return false;
    }

    if (hdr.num_vertexes == 0 || hdr.num_triangles == 0 || hdr.num_meshes == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Empty geometry in %s", iqmPath.c_str());
        return false;
    }

    std::vector<char> text;
    if (hdr.num_text > 0)
    {
        const uint64_t textEnd = uint64_t(hdr.ofs_text) + uint64_t(hdr.num_text);
        if (hdr.ofs_text == 0 || textEnd > uint64_t(fileSize))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Invalid text table in %s", iqmPath.c_str());
            return false;
        }
        text.resize(hdr.num_text);
        s.seek(hdr.ofs_text);
        s.readRaw(text.data(), text.size());
    }

    std::vector<IqmMesh> meshes;
    std::vector<IqmVertexArray> varrays;
    std::vector<IqmTriangle> triangles;
    std::vector<IqmJoint> joints;
    std::vector<IqmPose> poses;
    std::vector<IqmAnim> iqmAnims;

    if (!readArrayAt(s, fileSize, hdr.ofs_meshes, hdr.num_meshes, meshes) ||
        !readArrayAt(s, fileSize, hdr.ofs_vertexarrays, hdr.num_vertexarrays, varrays) ||
        !readArrayAt(s, fileSize, hdr.ofs_triangles, hdr.num_triangles, triangles))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Corrupt geometry chunks in %s", iqmPath.c_str());
        return false;
    }

    if (hdr.num_joints > 0 && !readArrayAt(s, fileSize, hdr.ofs_joints, hdr.num_joints, joints))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Corrupt joints chunk in %s", iqmPath.c_str());
        return false;
    }

    if (hdr.num_poses > 0 && !readArrayAt(s, fileSize, hdr.ofs_poses, hdr.num_poses, poses))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Corrupt poses chunk in %s", iqmPath.c_str());
        return false;
    }

    if (hdr.num_anims > 0 && !readArrayAt(s, fileSize, hdr.ofs_anims, hdr.num_anims, iqmAnims))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Corrupt anim chunk in %s", iqmPath.c_str());
        return false;
    }

    std::vector<uint16_t> frameData;
    const uint64_t frameCount = uint64_t(hdr.num_frames) * uint64_t(hdr.num_framechannels);
    if (frameCount > 0)
    {
        const uint64_t bytes = frameCount * sizeof(uint16_t);
        const uint64_t end = uint64_t(hdr.ofs_frames) + bytes;
        if (hdr.ofs_frames == 0 || end > uint64_t(fileSize))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[IQM] Corrupt frame data in %s", iqmPath.c_str());
            return false;
        }
        frameData.resize(static_cast<size_t>(frameCount));
        s.seek(hdr.ofs_frames);
        s.readRaw(frameData.data(), static_cast<size_t>(bytes));
    }

    if (!joints.empty())
    {
        const int jointCount = static_cast<int>(joints.size());
        std::vector<glm::mat4> localBind(jointCount, glm::mat4(1.0f));
        std::vector<glm::mat4> globalBind(jointCount, glm::mat4(1.0f));
        std::vector<uint8_t> state(jointCount, 0);

        outMesh->bones.resize(jointCount);
        for (int i = 0; i < jointCount; ++i)
        {
            const IqmJoint &j = joints[i];
            const glm::vec3 t(j.translate[0], j.translate[1], j.translate[2]);
            const glm::quat q(j.rotate[3], j.rotate[0], j.rotate[1], j.rotate[2]);
            const glm::vec3 sc(j.scale[0], j.scale[1], j.scale[2]);

            localBind[i] = makeTRS(t, glm::normalize(q), sc);

            Bone bone;
            bone.name = iqmTextAt(text, j.name);
            bone.parent = j.parent;
            bone.localPose = localBind[i];
            outMesh->bones[i] = bone;
        }

        for (int i = 0; i < jointCount; ++i)
            outMesh->bones[i].offset = glm::inverse(evalGlobalBindRecursive(i, joints, localBind, globalBind, state));
    }

    const int maxShaderBones = 100;
    const bool clampBoneIds = static_cast<int>(outMesh->bones.size()) > maxShaderBones;
    if (clampBoneIds)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[IQM] %s has %zu bones, shader supports %d; clamping bone IDs",
                    iqmPath.c_str(), outMesh->bones.size(), maxShaderBones);
    }

    const size_t vertexCount = hdr.num_vertexes;
    std::vector<glm::vec3> positions(vertexCount, glm::vec3(0.0f));
    std::vector<glm::vec3> normals(vertexCount, glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<glm::vec2> uvs(vertexCount, glm::vec2(0.0f));
    std::vector<glm::ivec4> boneIds(vertexCount, glm::ivec4(0));
    std::vector<glm::vec4> boneWeights(vertexCount, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

    for (const IqmVertexArray &va : varrays)
    {
        if (va.offset == 0)
            continue;

        s.seek(va.offset);

        if (va.type == IQM_POSITION && va.format == IQM_FLOAT && va.size == 3)
        {
            for (size_t i = 0; i < vertexCount; ++i)
                positions[i] = glm::vec3(s.readF32(), s.readF32(), s.readF32());
        }
        else if (va.type == IQM_NORMAL && va.format == IQM_FLOAT && va.size == 3)
        {
            for (size_t i = 0; i < vertexCount; ++i)
                normals[i] = glm::vec3(s.readF32(), s.readF32(), s.readF32());
        }
        else if (va.type == IQM_TEXCOORD && va.format == IQM_FLOAT && va.size == 2)
        {
            for (size_t i = 0; i < vertexCount; ++i)
                uvs[i] = glm::vec2(s.readF32(),  s.readF32());
        }
        else if (va.type == IQM_BLENDINDEXES && va.format == IQM_UBYTE && va.size == 4)
        {
            for (size_t i = 0; i < vertexCount; ++i)
            {
                int b0 = int(s.readU8());
                int b1 = int(s.readU8());
                int b2 = int(s.readU8());
                int b3 = int(s.readU8());
                if (clampBoneIds)
                {
                    b0 = std::min(b0, maxShaderBones - 1);
                    b1 = std::min(b1, maxShaderBones - 1);
                    b2 = std::min(b2, maxShaderBones - 1);
                    b3 = std::min(b3, maxShaderBones - 1);
                }
                boneIds[i] = glm::ivec4(b0, b1, b2, b3);
            }
        }
        else if (va.type == IQM_BLENDWEIGHTS && va.format == IQM_UBYTE && va.size == 4)
        {
            for (size_t i = 0; i < vertexCount; ++i)
            {
                const float w0 = float(s.readU8());
                const float w1 = float(s.readU8());
                const float w2 = float(s.readU8());
                const float w3 = float(s.readU8());
                const float sum = w0 + w1 + w2 + w3;
                if (sum > 0.0f)
                    boneWeights[i] = glm::vec4(w0, w1, w2, w3) / sum;
            }
        }
        else if (va.type == IQM_BLENDWEIGHTS && va.format == IQM_FLOAT && va.size == 4)
        {
            for (size_t i = 0; i < vertexCount; ++i)
                boneWeights[i] = glm::vec4(s.readF32(), s.readF32(), s.readF32(), s.readF32());
        }
    }

    outMesh->buffer.vertices.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
    {
        AnimatedVertex v{};
        v.position = positions[i];
        v.normal = glm::normalize(normals[i]);
        v.uv = uvs[i];
        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        v.boneIds = boneIds[i];

        const float sum = boneWeights[i].x + boneWeights[i].y + boneWeights[i].z + boneWeights[i].w;
        if (sum > 1e-8f)
            v.boneWeights = boneWeights[i] / sum;
        else
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

        outMesh->buffer.vertices[i] = v;
    }

    outMesh->buffer.indices.reserve(hdr.num_triangles * 3u);
    for (const IqmTriangle &tri : triangles)
    {
        outMesh->buffer.indices.push_back(tri.vertex[2]);
        outMesh->buffer.indices.push_back(tri.vertex[1]);
        outMesh->buffer.indices.push_back(tri.vertex[0]);
    }

    std::unordered_map<std::string, std::string> skinMap;
    const std::string baseDir = textureDir.empty() ? PathDirectory(iqmPath) : textureDir;
    const std::string iqmFile = PathFilename(iqmPath);
    const std::vector<std::string> skinCandidates = {
        iqmPath + "_1.skin",
        iqmPath + "_0.skin",
        PathJoin(baseDir, iqmFile + "_1.skin"),
        PathJoin(baseDir, iqmFile + "_0.skin")
    };
    for (const std::string &candidate : skinCandidates)
    {
        if (FileExists(candidate) && parseSkinFile(candidate, skinMap))
            break;
    }

    Texture *white = TextureManager::instance().getWhite();
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        const IqmMesh &m = meshes[i];
        const std::string meshName = iqmTextAt(text, m.name);
        const std::string matName = iqmTextAt(text, m.material);

        std::string texToken = matName;
        auto it = skinMap.find(meshName);
        if (it != skinMap.end())
            texToken = it->second;

        std::string texPath = ResolveTexturePath(baseDir, texToken);
        if (texPath.empty() && texToken != matName)
            texPath = ResolveTexturePath(baseDir, matName);

        Material *mat = new Material();
        mat->name = outMesh->name + "::" + (meshName.empty() ? ("mesh_" + std::to_string(i)) : meshName);
        Texture *albedo = nullptr;
        if (!texPath.empty())
            albedo = TextureManager::instance().load(mat->name + "::albedo", texPath);
        mat->setTexture("u_albedo", albedo ? albedo : white);
        mat->setVec4("u_color", glm::vec4(1.0f));

        const int matIndex = outMesh->add_material(mat);

        uint32_t start = m.first_triangle * 3u;
        uint32_t count = m.num_triangles * 3u;
        const uint32_t maxCount = static_cast<uint32_t>(outMesh->buffer.indices.size());
        if (start > maxCount)
            start = maxCount;
        if (start + count > maxCount)
            count = (maxCount > start) ? (maxCount - start) : 0u;

        if (count > 0)
            outMesh->add_surface(start, count, matIndex);
    }

    std::vector<FrameGroupClip> clips;
    parseFrameGroups(iqmPath + ".framegroups", clips);

    if (clips.empty() && !iqmAnims.empty())
    {
        for (size_t i = 0; i < iqmAnims.size(); ++i)
        {
            const IqmAnim &a = iqmAnims[i];
            FrameGroupClip clip;
            clip.firstFrame = static_cast<int>(a.first_frame);
            clip.numFrames = static_cast<int>(a.num_frames);
            clip.fps = (a.framerate > 0.0f) ? a.framerate : 24.0f;
            clip.loop = (a.flags & IQM_LOOP) != 0u;
            clip.name = iqmTextAt(text, a.name);
            if (clip.name.empty())
                clip.name = "anim_" + std::to_string(i);
            if (clip.numFrames > 0)
                clips.push_back(clip);
        }
    }

    if (clips.empty() && hdr.num_frames > 0)
    {
        FrameGroupClip fallback;
        fallback.name = "all";
        fallback.firstFrame = 0;
        fallback.numFrames = static_cast<int>(hdr.num_frames);
        fallback.fps = 24.0f;
        fallback.loop = true;
        clips.push_back(fallback);
    }

    const size_t boneCount = std::min(outMesh->bones.size(), poses.size());
    std::vector<glm::vec3> framePos;
    std::vector<glm::quat> frameRot;
    std::vector<glm::vec3> frameScale;

    for (const FrameGroupClip &clip : clips)
    {
        if (clip.numFrames <= 0 || boneCount == 0)
            continue;

        Animation *anim = new Animation();
        anim->name = clip.name;
        anim->ticksPerSecond = (clip.fps > 0.0f) ? clip.fps : 24.0f;
        anim->duration = (clip.numFrames > 1) ? float(clip.numFrames - 1) : 1.0f;
        anim->channels.resize(boneCount);

        for (size_t b = 0; b < boneCount; ++b)
            anim->channels[b].boneName = outMesh->bones[b].name;

        for (int fi = 0; fi < clip.numFrames; ++fi)
        {
            int srcFrame = clip.firstFrame + fi;
            srcFrame = std::max(0, std::min(srcFrame, int(hdr.num_frames) - 1));
            decodeFramePose(static_cast<uint32_t>(srcFrame),
                            hdr.num_framechannels,
                            poses,
                            frameData,
                            framePos, frameRot, frameScale);

            const float t = float(fi);
            for (size_t b = 0; b < boneCount; ++b)
            {
                anim->channels[b].posKeys.push_back({t, framePos[b]});
                anim->channels[b].rotKeys.push_back({t, frameRot[b]});
                anim->channels[b].scaleKeys.push_back({t, frameScale[b]});
            }
        }

        outMesh->animations.push_back(anim);
    }

    if (!outMesh->buffer.indices.empty() && !outMesh->buffer.vertices.empty())
        outMesh->compute_tangents();

    if (outMesh->bones.empty())
    {
        outMesh->finalMatrices.resize(1, glm::mat4(1.0f));
        for (AnimatedVertex &v : outMesh->buffer.vertices)
        {
            v.boneIds = glm::ivec4(0);
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    else
    {
        outMesh->finalMatrices.resize(outMesh->bones.size(), glm::mat4(1.0f));
    }

    outMesh->upload();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[IQM] Loaded '%s': verts=%zu tris=%zu bones=%zu surfs=%zu anims=%zu",
                iqmPath.c_str(),
                outMesh->buffer.vertices.size(),
                outMesh->buffer.indices.size() / 3u,
                outMesh->bones.size(),
                outMesh->surfaces.size(),
                outMesh->animations.size());
    return true;
}
}

AnimatedMesh *AnimatedMeshManager::load_imq(const std::string &name,
                                            const std::string &path,
                                            const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    AnimatedMesh *mesh = new AnimatedMesh();
    if (!loadIQMAnimated(name, path, texture_dir, mesh))
    {
        delete mesh;
        return nullptr;
    }

    cache[name] = mesh;
    return mesh;
}
