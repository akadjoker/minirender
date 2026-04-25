#include "genesis/GenesisGbspFile.hpp"

#include <cstring>

#include "genesis/GenesisUtils.hpp"

namespace mini_genesis
{
bool GenesisGbspFile::parseEntData(const std::vector<uint8_t> &entData,
                                   std::vector<BspEntity> &entities,
                                   std::string &error) const
{
    entities.clear();
    if (entData.empty())
        return true;

    size_t cursor = 0;
    if (cursor + 4 > entData.size())
        return true;

    const int32_t count = readI32Raw(entData, cursor);
    cursor += 4;
    if (count < 0 || count > 100000)
    {
        error = "contador de entidades invalido";
        return false;
    }

    entities.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i)
    {
        if (cursor + 4 > entData.size())
        {
            error = "entidade truncada";
            return false;
        }
        const int32_t pairs = readI32Raw(entData, cursor);
        cursor += 4;
        if (pairs < 0 || pairs > 20000)
        {
            error = "pares de entidade invalidos";
            return false;
        }

        BspEntity entity;
        for (int32_t p = 0; p < pairs; ++p)
        {
            if (cursor + 4 > entData.size())
            {
                error = "entidade key truncada";
                return false;
            }
            const int32_t keySize = readI32Raw(entData, cursor);
            cursor += 4;
            if (keySize < 0 || cursor + static_cast<size_t>(keySize) > entData.size())
            {
                error = "entidade key invalida";
                return false;
            }
            std::string key;
            if (keySize > 0)
            {
                key.assign(reinterpret_cast<const char *>(entData.data() + cursor),
                           reinterpret_cast<const char *>(entData.data() + cursor + static_cast<size_t>(keySize)));
                if (!key.empty() && key.back() == '\0')
                    key.pop_back();
            }
            cursor += static_cast<size_t>(keySize);

            if (cursor + 4 > entData.size())
            {
                error = "entidade value truncada";
                return false;
            }
            const int32_t valueSize = readI32Raw(entData, cursor);
            cursor += 4;
            if (valueSize < 0 || cursor + static_cast<size_t>(valueSize) > entData.size())
            {
                error = "entidade value invalida";
                return false;
            }
            std::string value;
            if (valueSize > 0)
            {
                value.assign(reinterpret_cast<const char *>(entData.data() + cursor),
                             reinterpret_cast<const char *>(entData.data() + cursor + static_cast<size_t>(valueSize)));
                if (!value.empty() && value.back() == '\0')
                    value.pop_back();
            }
            cursor += static_cast<size_t>(valueSize);

            entity.kv[toLower(key)] = value;
        }
        entities.push_back(std::move(entity));
    }

    return true;
}

bool GenesisGbspFile::load(const std::string &path, GbspData &out, std::string &error) const
{
    out = {};

    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes, error))
        return false;

    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "IBSP", 4) == 0)
    {
        error = "IBSP (Quake) nao e GBSP Genesis";
        return false;
    }

    size_t cursor = 0;
    bool sawHeader = false;
    while (cursor + 12 <= bytes.size())
    {
        const int32_t type = readI32Raw(bytes, cursor + 0);
        const int32_t elemSize = readI32Raw(bytes, cursor + 4);
        const int32_t count = readI32Raw(bytes, cursor + 8);
        cursor += 12;

        if (type == CHUNK_END)
            break;

        if (elemSize < 0 || count < 0)
        {
            error = "chunk size/count negativos";
            return false;
        }

        const size_t dataSize = static_cast<size_t>(elemSize) * static_cast<size_t>(count);
        if (cursor + dataSize > bytes.size())
        {
            error = "chunk GBSP truncado";
            return false;
        }

        switch (type)
        {
        case CHUNK_HEADER:
            if (dataSize >= 5 && std::memcmp(bytes.data() + cursor, "GBSP", 4) == 0)
                sawHeader = true;
            break;
        case CHUNK_MODELS:
            out.models.clear();
            out.models.reserve(static_cast<size_t>(count));
            if (elemSize >= 8 && count > 0)
            {
                for (int32_t i = 0; i < count; ++i)
                {
                    const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                    BspModel model;
                    model.rootNode = readI32Raw(bytes, o + 0);
                    model.rootBNode = readI32Raw(bytes, o + 4);
                    if (elemSize >= 16)
                    {
                        model.firstFace = readI32Raw(bytes, o + 8);
                        model.numFaces = readI32Raw(bytes, o + 12);
                    }
                    out.models.push_back(model);
                }

                out.rootNode = out.models[0].rootNode;
                out.rootBNode = out.models[0].rootBNode;
            }
            break;
        case CHUNK_NODES:
            if (elemSize < 44)
            {
                error = "chunk nodes invalido";
                return false;
            }
            out.nodes.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspNode n;
                n.children[0] = readI32Raw(bytes, o + 0);
                n.children[1] = readI32Raw(bytes, o + 4);
                n.numFaces = readI32Raw(bytes, o + 8);
                n.firstFace = readI32Raw(bytes, o + 12);
                n.planeNum = readI32Raw(bytes, o + 16);
                n.mins = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 20), readF32Raw(bytes, o + 24), readF32Raw(bytes, o + 28)));
                n.maxs = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 32), readF32Raw(bytes, o + 36), readF32Raw(bytes, o + 40)));
                out.nodes.push_back(n);
            }
            break;
        case CHUNK_LEAFS:
            if (elemSize < 60)
            {
                error = "chunk leafs invalido";
                return false;
            }
            out.leafs.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspLeaf leaf;
                leaf.contents = readI32Raw(bytes, o + 0);
                leaf.mins = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 4), readF32Raw(bytes, o + 8), readF32Raw(bytes, o + 12)));
                leaf.maxs = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 16), readF32Raw(bytes, o + 20), readF32Raw(bytes, o + 24)));
                leaf.firstFace = readI32Raw(bytes, o + 28);
                leaf.numFaces = readI32Raw(bytes, o + 32);
                leaf.firstPortal = readI32Raw(bytes, o + 36);
                leaf.numPortals = readI32Raw(bytes, o + 40);
                leaf.cluster = readI32Raw(bytes, o + 44);
                leaf.area = readI32Raw(bytes, o + 48);
                leaf.firstSide = readI32Raw(bytes, o + 52);
                leaf.numSides = readI32Raw(bytes, o + 56);
                out.leafs.push_back(leaf);
            }
            break;
        case CHUNK_CLUSTERS:
            if (elemSize < 4)
            {
                error = "chunk clusters invalido";
                return false;
            }
            out.clusters.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspCluster c;
                c.visOfs = readI32Raw(bytes, o + 0);
                out.clusters.push_back(c);
            }
            break;
        case CHUNK_LEAF_SIDES:
            if (elemSize < 8)
            {
                error = "chunk leaf_sides invalido";
                return false;
            }
            out.leafSides.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspLeafSide s;
                s.planeNum = readI32Raw(bytes, o + 0);
                s.planeSide = readI32Raw(bytes, o + 4);
                out.leafSides.push_back(s);
            }
            break;
        case CHUNK_PORTALS:
            if (elemSize < 16)
            {
                error = "chunk portals invalido";
                return false;
            }
            out.portals.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspPortal p;
                p.origin = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 0), readF32Raw(bytes, o + 4), readF32Raw(bytes, o + 8)));
                p.leafTo = readI32Raw(bytes, o + 12);
                out.portals.push_back(p);
            }
            break;
        case CHUNK_BNODES:
            if (elemSize < 12)
            {
                error = "chunk bnodes invalido";
                return false;
            }
            out.bnodes.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspBNode n;
                n.children[0] = readI32Raw(bytes, o + 0);
                n.children[1] = readI32Raw(bytes, o + 4);
                n.planeNum = readI32Raw(bytes, o + 8);
                out.bnodes.push_back(n);
            }
            break;
        case CHUNK_PLANES:
            if (elemSize < 16)
            {
                error = "chunk planes invalido";
                return false;
            }
            out.planes.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspPlane p;
                p.normal = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 0), readF32Raw(bytes, o + 4), readF32Raw(bytes, o + 8)));
                p.dist = readF32Raw(bytes, o + 12);
                out.planes.push_back(p);
            }
            break;
        case CHUNK_FACES:
            if (elemSize < 36)
            {
                error = "chunk faces invalido";
                return false;
            }
            out.faces.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspFace f;
                f.firstVert = readI32Raw(bytes, o + 0);
                f.numVerts = readI32Raw(bytes, o + 4);
                f.planeNum = readI32Raw(bytes, o + 8);
                f.planeSide = readI32Raw(bytes, o + 12);
                f.texInfo = readI32Raw(bytes, o + 16);
                f.lightOfs = readI32Raw(bytes, o + 20);
                f.lightWidth = readI32Raw(bytes, o + 24);
                f.lightHeight = readI32Raw(bytes, o + 28);
                out.faces.push_back(f);
            }
            break;
        case CHUNK_VERT_INDEX:
            if (elemSize != 4)
            {
                error = "chunk vert index invalido";
                return false;
            }
            out.vertIndices.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
                out.vertIndices.push_back(readI32Raw(bytes, cursor + static_cast<size_t>(i) * 4u));
            break;
        case CHUNK_VERTS:
            if (elemSize != 12)
            {
                error = "chunk verts invalido";
                return false;
            }
            out.verts.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * 12u;
                out.verts.push_back(genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 0),
                                                                   readF32Raw(bytes, o + 4),
                                                                   readF32Raw(bytes, o + 8))));
            }
            break;
        case CHUNK_TEXINFO:
            if (elemSize < 64)
            {
                error = "chunk texinfo invalido";
                return false;
            }
            out.texInfos.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspTexInfo t;
                t.vecs[0] = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 0), readF32Raw(bytes, o + 4), readF32Raw(bytes, o + 8)));
                t.vecs[1] = genesisPointToEngine(glm::vec3(readF32Raw(bytes, o + 12), readF32Raw(bytes, o + 16), readF32Raw(bytes, o + 20)));
                t.shift[0] = readF32Raw(bytes, o + 24);
                t.shift[1] = readF32Raw(bytes, o + 28);
                t.drawScale[0] = readF32Raw(bytes, o + 32);
                t.drawScale[1] = readF32Raw(bytes, o + 36);
                t.flags = readI32Raw(bytes, o + 40);
                t.texture = readI32Raw(bytes, o + 60);
                out.texInfos.push_back(t);
            }
            break;
        case CHUNK_TEXTURES:
            if (elemSize < 52)
            {
                error = "chunk textures invalido";
                return false;
            }
            out.textures.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                BspTexture t;
                t.name = readFixedStringRaw(bytes, o, 32);
                t.flags = static_cast<uint32_t>(readI32Raw(bytes, o + 32));
                t.width = readI32Raw(bytes, o + 36);
                t.height = readI32Raw(bytes, o + 40);
                t.offset = readI32Raw(bytes, o + 44);
                t.paletteIndex = readI32Raw(bytes, o + 48);
                out.textures.push_back(std::move(t));
            }
            break;
        case CHUNK_TEXDATA:
            out.texData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case CHUNK_LIGHTDATA:
            out.lightData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case CHUNK_VISDATA:
            out.visData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case CHUNK_PALETTES:
            out.palettes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case CHUNK_ENTDATA:
            out.entData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        default:
            break;
        }

        cursor += dataSize;
    }

    if (!sawHeader)
    {
        error = "cabecalho GBSP nao encontrado";
        return false;
    }

    if (out.faces.empty() || out.verts.empty() || out.vertIndices.empty())
    {
        error = "gbsp sem geometria";
        return false;
    }

    return parseEntData(out.entData, out.entities, error);
}
} // namespace mini_genesis
