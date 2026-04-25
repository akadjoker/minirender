#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Math.hpp"

namespace mini_genesis
{
enum ChunkType : int32_t
{
    CHUNK_HEADER = 0,
    CHUNK_MODELS = 1,
    CHUNK_NODES = 2,
    CHUNK_BNODES = 3,
    CHUNK_LEAFS = 4,
    CHUNK_CLUSTERS = 5,
    CHUNK_LEAF_SIDES = 8,
    CHUNK_PORTALS = 9,
    CHUNK_PLANES = 10,
    CHUNK_FACES = 11,
    CHUNK_VERT_INDEX = 13,
    CHUNK_VERTS = 14,
    CHUNK_ENTDATA = 16,
    CHUNK_TEXINFO = 17,
    CHUNK_TEXTURES = 18,
    CHUNK_TEXDATA = 19,
    CHUNK_LIGHTDATA = 20,
    CHUNK_VISDATA = 21,
    CHUNK_PALETTES = 23,
    CHUNK_END = 0xffff,
};

struct BspFace
{
    int32_t firstVert = 0;
    int32_t numVerts = 0;
    int32_t planeNum = 0;
    int32_t planeSide = 0;
    int32_t texInfo = 0;
    int32_t lightOfs = -1;
    int32_t lightWidth = 0;
    int32_t lightHeight = 0;
};

struct BspTexInfo
{
    glm::vec3 vecs[2] = {};
    float shift[2] = {};
    float drawScale[2] = {1.0f, 1.0f};
    int32_t flags = 0;
    int32_t texture = -1;
};

struct BspTexture
{
    std::string name;
    uint32_t flags = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t offset = 0;
    int32_t paletteIndex = 0;
};

struct BspPlane
{
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float dist = 0.0f;
};

struct BspBNode
{
    int32_t children[2] = {0, 0};
    int32_t planeNum = -1;
};

struct BspNode
{
    int32_t children[2] = {0, 0};
    int32_t numFaces = 0;
    int32_t firstFace = 0;
    int32_t planeNum = -1;
    glm::vec3 mins = glm::vec3(0.0f);
    glm::vec3 maxs = glm::vec3(0.0f);
};

struct BspLeaf
{
    int32_t contents = 0;
    glm::vec3 mins = glm::vec3(0.0f);
    glm::vec3 maxs = glm::vec3(0.0f);
    int32_t firstFace = 0;
    int32_t numFaces = 0;
    int32_t firstPortal = 0;
    int32_t numPortals = 0;
    int32_t cluster = -1;
    int32_t area = -1;
    int32_t firstSide = 0;
    int32_t numSides = 0;
};

struct BspCluster
{
    int32_t visOfs = -1;
};

struct BspLeafSide
{
    int32_t planeNum = -1;
    int32_t planeSide = 0;
};

struct BspPortal
{
    glm::vec3 origin = glm::vec3(0.0f);
    int32_t leafTo = -1;
};

struct BspEntity
{
    std::unordered_map<std::string, std::string> kv;
};

struct BspModel
{
    int32_t rootNode = 0;
    int32_t rootBNode = 0;
    int32_t firstFace = 0;
    int32_t numFaces = 0;
};

struct GbspData
{
    std::vector<BspFace> faces;
    std::vector<glm::vec3> verts;
    std::vector<int32_t> vertIndices;
    std::vector<BspTexInfo> texInfos;
    std::vector<BspTexture> textures;
    std::vector<uint8_t> texData;
    std::vector<uint8_t> lightData;
    std::vector<uint8_t> visData;
    std::vector<uint8_t> palettes;
    std::vector<uint8_t> entData;
    std::vector<BspEntity> entities;
    std::vector<BspModel> models;
    std::vector<BspPlane> planes;
    std::vector<BspBNode> bnodes;
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leafs;
    std::vector<BspCluster> clusters;
    std::vector<BspLeafSide> leafSides;
    std::vector<BspPortal> portals;
    int32_t rootBNode = 0;
    int32_t rootNode = 0;
};

struct BrushFace
{
    std::vector<glm::vec3> points;
    std::string texture;
    float rotate = 0.0f;
    glm::vec2 shift = glm::vec2(0.0f);
    glm::vec2 scale = glm::vec2(1.0f);
    int flags = 0;
};

struct Brush
{
    std::string name;
    int flags = 0;
    int modelId = 0;
    int groupId = 0;
    float hullSize = 1.0f;
    int type = 0;
    std::vector<BrushFace> faces;
};

struct Entity
{
    std::unordered_map<std::string, std::string> kv;
};

struct Map3dtData
{
    std::string version;
    std::string textureLib;
    int numEntitiesHeader = 0;
    int numModelsHeader = 0;

    std::vector<Brush> brushes;
    std::vector<Entity> entities;
};

} // namespace mini_genesis
