#pragma once
#include "Mesh.hpp"
#include "Animation.hpp"
#include "BinaryStream.hpp"
#include <SDL2/SDL.h>
#include <string>
#include <cstdint>

// ============================================================
//  Constantes do formato
// ============================================================
constexpr uint32_t MESH_MAGIC           = 0x4D455348; // "MESH"
constexpr uint32_t MESH_VERSION         = 100;

constexpr uint32_t BUFFER_FLAG_SKINNED  = 1 << 0;
constexpr uint32_t BUFFER_FLAG_TANGENTS = 1 << 1;
constexpr uint32_t BUFFER_FLAG_COLORS   = 1 << 2;

constexpr uint32_t CHUNK_MATS           = 0x4D415453; // "MATS"
constexpr uint32_t CHUNK_BUFF           = 0x42554646; // "BUFF"
constexpr uint32_t CHUNK_VRTS           = 0x56525453; // "VRTS"
constexpr uint32_t CHUNK_IDXS           = 0x49445853; // "IDXS"
constexpr uint32_t CHUNK_SKEL           = 0x534B454C; // "SKEL"
constexpr uint32_t CHUNK_SKIN           = 0x534B494E; // "SKIN"
constexpr uint32_t CHUNK_SURF           = 0x53555246; // "SURF"

constexpr uint32_t ANIM_MAGIC           = 0x414E494D; // "ANIM"
constexpr uint32_t ANIM_VERSION         = 100;
constexpr uint32_t ANIM_CHUNK_INFO      = 0x494E464F; // "INFO"
constexpr uint32_t ANIM_CHUNK_CHAN      = 0x4348414E; // "CHAN"
constexpr uint32_t ANIM_CHUNK_KEYS      = 0x4B455953; // "KEYS"

struct ChunkHeader { uint32_t id; uint32_t length; };

 

// ============================================================
//  MeshWriter
// ============================================================
class MeshWriter
{
public:
    bool save(const Mesh         *mesh, const std::string &path);
    bool save(const AnimatedMesh *mesh, const std::string &path);

private:
    BinaryStream *s_ = nullptr;

    void beginChunk(uint32_t id, Sint64 *startOut);
    void endChunk  (Sint64 start);

    void writeMaterials(const std::vector<Material *> &mats);
    void writeSkeleton (const std::vector<Bone>       &bones);
    void writeSurfaces (const std::vector<Surface>    &surfs);
    void writeBuffer   (const MeshBuffer         &buf, const std::vector<Surface> &surfs);
    void writeBuffer   (const AnimatedMeshBuffer &buf, const std::vector<Surface> &surfs);
};

// ============================================================
//  MeshReader
// ============================================================
class MeshReader
{
public:
    // Optional: set before load() to resolve relative texture paths
    std::string textureDir;

    bool load(const std::string &path, Mesh         *mesh);
    bool load(const std::string &path, AnimatedMesh *mesh);

private:
    BinaryStream *s_ = nullptr;

    ChunkHeader readHeader();
    void        skipChunk(const ChunkHeader &h);

    void readMaterials(const ChunkHeader &h, std::vector<Material *> &mats);
    void readSkeleton (const ChunkHeader &h, std::vector<Bone>       &bones);
    void readSurfaces (const ChunkHeader &h, std::vector<Surface>    &surfs);
    void readBuffer   (const ChunkHeader &h, Mesh         *mesh);
    void readBuffer   (const ChunkHeader &h, AnimatedMesh *mesh);
};

 

// ============================================================
//  AnimWriter — Animation → .anim
// ============================================================
class AnimWriter
{
public:
    bool save(const Animation *anim, const std::string &path);

private:
    BinaryStream *s_ = nullptr;

    void beginChunk(uint32_t id, Sint64 *startOut);
    void endChunk  (Sint64 start);
};

// ============================================================
//  AnimReader — .anim → Animation
// ============================================================
class AnimReader
{
public:
    bool load(const std::string &path, Animation *anim);

private:
    BinaryStream *s_ = nullptr;
};