#include "LevelEditorSceneIO.hpp"

#include "BinaryStream.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>

#include <json.hpp>

namespace nlohmann
{
template <>
struct adl_serializer<glm::vec3>
{
    static void to_json(json& j, const glm::vec3& v)
    {
        j = json{v.x, v.y, v.z};
    }

    static void from_json(const json& j, glm::vec3& v)
    {
        j.at(0).get_to(v.x);
        j.at(1).get_to(v.y);
        j.at(2).get_to(v.z);
    }
};
}

namespace
{
constexpr std::uint32_t kBinarySceneMagic = 0x4D524C45u; // "ELRM"
constexpr std::uint32_t kBinarySceneVersion = 5;
 
void writeVec2(BinaryStream& stream, const glm::vec2& value)
{
    stream.writeF32(value.x);
    stream.writeF32(value.y);
}

void writeVec3(BinaryStream& stream, const glm::vec3& value)
{
    stream.writeF32(value.x);
    stream.writeF32(value.y);
    stream.writeF32(value.z);
}

glm::vec2 readVec2(BinaryStream& stream)
{
    return glm::vec2(stream.readF32(), stream.readF32());
}

glm::vec3 readVec3(BinaryStream& stream)
{
    return glm::vec3(stream.readF32(), stream.readF32(), stream.readF32());
}

bool isBinaryScenePath(const std::filesystem::path& path)
{
    const std::string ext = path.extension().generic_string();
    return ext == ".mredb" || ext == ".mrbin";
}

bool isBinarySceneFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
        return false;

    std::uint32_t magic = 0;
    input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!input)
        return false;

    return SDL_SwapLE32(magic) == kBinarySceneMagic;
}

bool detectLegacyTerrainGridDimensions(const EditableMesh& mesh, int& outCols, int& outRows)
{
    outCols = 0;
    outRows = 0;
    const auto& vertices = mesh.vertices();
    const auto& faces = mesh.faces();
    if (vertices.size() < 4 || faces.empty())
        return false;

    for (const EditableFace& face : faces)
    {
        if (face.indices.size() != 4 || face.materialName != "terrain")
            return false;
    }

    const float firstZ = vertices.front().position.z;
    constexpr float eps = 1e-3f;
    int cols = 0;
    while (cols < static_cast<int>(vertices.size()) &&
           std::fabs(vertices[static_cast<std::size_t>(cols)].position.z - firstZ) <= eps)
    {
        ++cols;
    }
    if (cols < 2)
        return false;
    if (vertices.size() % static_cast<std::size_t>(cols) != 0)
        return false;

    const int rows = static_cast<int>(vertices.size() / static_cast<std::size_t>(cols));
    if (rows < 2)
        return false;
    if (static_cast<int>(faces.size()) != (cols - 1) * (rows - 1))
        return false;

    outCols = cols;
    outRows = rows;
    return true;
}

std::string primitiveTypeToString(LevelMeshPrimitive primitive)
{
    switch (primitive)
    {
    case LevelMeshPrimitive::Unknown: return "unknown";
    case LevelMeshPrimitive::Box: return "box";
    case LevelMeshPrimitive::Room: return "room";
    case LevelMeshPrimitive::Sector: return "sector";
    case LevelMeshPrimitive::RoomBoxesPart: return "room_boxes_part";
    case LevelMeshPrimitive::Cylinder: return "cylinder";
    case LevelMeshPrimitive::Cone: return "cone";
    case LevelMeshPrimitive::Sphere: return "sphere";
    case LevelMeshPrimitive::Torus: return "torus";
    case LevelMeshPrimitive::Tube: return "tube";
    case LevelMeshPrimitive::Pyramid: return "pyramid";
    case LevelMeshPrimitive::DoorFrame: return "door_frame";
    case LevelMeshPrimitive::Terrain: return "terrain";
    case LevelMeshPrimitive::Pillar: return "pillar";
    case LevelMeshPrimitive::Plane: return "plane";
    case LevelMeshPrimitive::Wedge: return "wedge";
    case LevelMeshPrimitive::Stairs: return "stairs";
    case LevelMeshPrimitive::SpiralStairs: return "spiral_stairs";
    case LevelMeshPrimitive::Text: return "text";
    case LevelMeshPrimitive::Imported: return "imported";
    case LevelMeshPrimitive::Empty: return "empty";
    }
    return "unknown";
}

LevelMeshPrimitive primitiveTypeFromString(const std::string& value)
{
    if (value == "box") return LevelMeshPrimitive::Box;
    if (value == "room") return LevelMeshPrimitive::Room;
    if (value == "sector") return LevelMeshPrimitive::Sector;
    if (value == "room_boxes_part") return LevelMeshPrimitive::RoomBoxesPart;
    if (value == "cylinder") return LevelMeshPrimitive::Cylinder;
    if (value == "cone") return LevelMeshPrimitive::Cone;
    if (value == "sphere") return LevelMeshPrimitive::Sphere;
    if (value == "torus") return LevelMeshPrimitive::Torus;
    if (value == "tube") return LevelMeshPrimitive::Tube;
    if (value == "pyramid") return LevelMeshPrimitive::Pyramid;
    if (value == "door_frame") return LevelMeshPrimitive::DoorFrame;
    if (value == "terrain") return LevelMeshPrimitive::Terrain;
    if (value == "pillar") return LevelMeshPrimitive::Pillar;
    if (value == "plane") return LevelMeshPrimitive::Plane;
    if (value == "wedge") return LevelMeshPrimitive::Wedge;
    if (value == "stairs") return LevelMeshPrimitive::Stairs;
    if (value == "spiral_stairs") return LevelMeshPrimitive::SpiralStairs;
    if (value == "text") return LevelMeshPrimitive::Text;
    if (value == "imported") return LevelMeshPrimitive::Imported;
    if (value == "empty") return LevelMeshPrimitive::Empty;
    return LevelMeshPrimitive::Unknown;
}

std::string blendModeToString(LevelMeshBlendMode mode)
{
    switch (mode)
    {
    case LevelMeshBlendMode::Alpha: return "alpha";
    case LevelMeshBlendMode::Additive: return "additive";
    }
    return "alpha";
}

LevelMeshBlendMode blendModeFromString(const std::string& value)
{
    if (value == "additive") return LevelMeshBlendMode::Additive;
    return LevelMeshBlendMode::Alpha;
}

std::string entityTypeToString(LevelEntityType type)
{
    switch (type)
    {
    case LevelEntityType::PlayerStart: return "player_start";
    case LevelEntityType::Light: return "light";
    case LevelEntityType::Door: return "door";
    case LevelEntityType::Elevator: return "elevator";
    case LevelEntityType::Platform: return "platform";
    case LevelEntityType::Placement: return "placement";
    }
    return "entity";
}

LevelEntityType entityTypeFromString(const std::string& value)
{
    if (value == "player_start") return LevelEntityType::PlayerStart;
    if (value == "light") return LevelEntityType::Light;
    if (value == "door") return LevelEntityType::Door;
    if (value == "elevator") return LevelEntityType::Elevator;
    if (value == "platform") return LevelEntityType::Platform;
    if (value == "placement") return LevelEntityType::Placement;
    return LevelEntityType::PlayerStart;
}

bool saveBinaryLevelEditorScene(const std::filesystem::path& path,
                                const LevelEditorScene& scene,
                                std::string& error)
{
    BinaryStream stream(path.string(), "wb");
    if (!stream.isOpen())
    {
        error = "could not open binary scene for writing";
        return false;
    }

    stream.writeU32(kBinarySceneMagic);
    stream.writeU32(kBinarySceneVersion);
    stream.writeStr(scene.assetRoot());
    stream.writeStr(scene.lightmapPath());
    writeVec3(stream, scene.creationPivotPosition());
    writeVec3(stream, scene.creationPivotRotation());

    stream.writeU32(static_cast<std::uint32_t>(scene.meshObjects().size()));
    for (const LevelMeshObject& object : scene.meshObjects())
    {
        stream.writeStr(object.name);
        stream.writeU32(static_cast<std::uint32_t>(object.primitive));
        writeVec3(stream, object.position);
        writeVec3(stream, object.rotationEuler);
        writeVec3(stream, object.scale);
        writeVec3(stream, object.pivot);
        stream.writeU8(object.visible ? 1u : 0u);
        stream.writeU8(object.locked ? 1u : 0u);
        stream.writeU8(object.blendEnabled ? 1u : 0u);
        stream.writeU8(object.twoSided ? 1u : 0u);
        stream.writeU32(static_cast<std::uint32_t>(object.blendMode));

        const auto& vertices = object.mesh.vertices();
        stream.writeU32(static_cast<std::uint32_t>(vertices.size()));
        for (const EditableVertex& vertex : vertices)
        {
            writeVec3(stream, vertex.position);
            writeVec3(stream, vertex.normal);
            writeVec2(stream, vertex.uv);
        }

        const auto& faces = object.mesh.faces();
        stream.writeU32(static_cast<std::uint32_t>(faces.size()));
        for (const EditableFace& face : faces)
        {
            stream.writeStr(face.materialName);
            writeVec2(stream, face.uvOffset);
            writeVec2(stream, face.uvScale);
            stream.writeF32(face.uvRotation);
            stream.writeU32(static_cast<std::uint32_t>(face.uvProjection));
            stream.writeU32(static_cast<std::uint32_t>(face.indices.size()));
            for (int index : face.indices)
                stream.writeI32(index);
        }

        stream.writeU32(static_cast<std::uint32_t>(object.terrainLayers.size()));
        for (const LevelMeshObject::TerrainTextureLayer& layer : object.terrainLayers)
        {
            stream.writeStr(layer.name);
            stream.writeStr(layer.texturePath);
            stream.writeF32(layer.opacity);
            stream.writeU8(layer.visible ? 1u : 0u);
            stream.writeI32(layer.maskWidth);
            stream.writeI32(layer.maskHeight);
            stream.writeU32(static_cast<std::uint32_t>(layer.maskData.size()));
            if (!layer.maskData.empty())
                stream.writeRaw(layer.maskData.data(), layer.maskData.size());
        }
    }

    stream.writeU32(static_cast<std::uint32_t>(scene.entities().size()));
    for (const LevelEntityObject& entity : scene.entities())
    {
        stream.writeStr(entity.name);
        stream.writeU32(static_cast<std::uint32_t>(entity.type));
        writeVec3(stream, entity.position);
        stream.writeU32(static_cast<std::uint32_t>(entity.lightType));
        writeVec3(stream, entity.color);
        stream.writeF32(entity.intensity);
        stream.writeF32(entity.radius);
        writeVec3(stream, entity.direction);
        stream.writeF32(entity.spotAngle);
        stream.writeF32(entity.spotSoftness);
        stream.writeU32(static_cast<std::uint32_t>(entity.doorType));
        stream.writeF32(entity.doorDistance);
        stream.writeF32(entity.doorSpeed);
        stream.writeU8(entity.doorStartOpen ? 1u : 0u);
        stream.writeI32(entity.linkedMeshIndex);
        writeVec3(stream, entity.endPosition);
        stream.writeF32(entity.moveSpeed);
        stream.writeF32(entity.waitTime);
        stream.writeI32(entity.itemType);
        stream.writeF32(entity.rotationY);
    }

    return true;
}

bool loadBinaryLevelEditorScene(const std::filesystem::path& path,
                                LevelEditorScene& scene,
                                std::string& error)
{
    BinaryStream stream(path.string(), "rb");
    if (!stream.isOpen())
    {
        error = "could not open binary scene for reading";
        return false;
    }

    if (stream.readU32() != kBinarySceneMagic)
    {
        error = "invalid binary scene magic";
        return false;
    }

    const std::uint32_t version = stream.readU32();
    if (version < 1 || version > kBinarySceneVersion)
    {
        error = "unsupported binary scene version";
        return false;
    }

    scene.meshObjects().clear();
    scene.entities().clear();
    scene.assetRoot() = stream.readStr();
    scene.lightmapPath() = (version >= 2) ? stream.readStr() : std::string();
    scene.creationPivotPosition() = (version >= 4) ? readVec3(stream) : glm::vec3(0.0f);
    scene.creationPivotRotation() = (version >= 4) ? readVec3(stream) : glm::vec3(0.0f);

    const std::uint32_t meshCount = stream.readU32();
    scene.meshObjects().reserve(meshCount);
    for (std::uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
    {
        LevelMeshObject object;
        object.name = stream.readStr();
        object.primitive = static_cast<LevelMeshPrimitive>(stream.readU32());
        object.position = readVec3(stream);
        object.rotationEuler = readVec3(stream);
        object.scale = readVec3(stream);
        object.pivot = readVec3(stream);
        object.visible = stream.readU8() != 0;
        object.locked = stream.readU8() != 0;
        if (version >= 3)
        {
            object.blendEnabled = stream.readU8() != 0;
            object.twoSided = (version >= 5) ? (stream.readU8() != 0) : false;
            const std::uint32_t blendMode = stream.readU32();
            object.blendMode = (blendMode <= static_cast<std::uint32_t>(LevelMeshBlendMode::Additive))
                ? static_cast<LevelMeshBlendMode>(blendMode)
                : LevelMeshBlendMode::Alpha;
        }

        std::vector<EditableVertex> vertices;
        const std::uint32_t vertexCount = stream.readU32();
        vertices.reserve(vertexCount);
        for (std::uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            EditableVertex vertex;
            vertex.position = readVec3(stream);
            vertex.normal = readVec3(stream);
            vertex.uv = readVec2(stream);
            vertices.push_back(vertex);
        }

        std::vector<EditableFace> faces;
        const std::uint32_t faceCount = stream.readU32();
        faces.reserve(faceCount);
        for (std::uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex)
        {
            EditableFace face;
            face.materialName = stream.readStr();
            face.uvOffset = readVec2(stream);
            face.uvScale = readVec2(stream);
            face.uvRotation = stream.readF32();
            const std::uint32_t uvProjection = stream.readU32();
            if (uvProjection <= static_cast<std::uint32_t>(UvProjection::Mesh))
                face.uvProjection = static_cast<UvProjection>(uvProjection);
            else
                face.uvProjection = UvProjection::Box;

            const std::uint32_t indexCount = stream.readU32();
            face.indices.reserve(indexCount);
            for (std::uint32_t index = 0; index < indexCount; ++index)
                face.indices.push_back(stream.readI32());
            faces.push_back(std::move(face));
        }

        object.mesh.setData(vertices, faces);

        const std::uint32_t layerCount = stream.readU32();
        object.terrainLayers.reserve(layerCount);
        for (std::uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
        {
            LevelMeshObject::TerrainTextureLayer layer;
            layer.name = stream.readStr();
            layer.texturePath = stream.readStr();
            layer.opacity = stream.readF32();
            layer.visible = stream.readU8() != 0;
            layer.maskWidth = stream.readI32();
            layer.maskHeight = stream.readI32();
            const std::uint32_t maskSize = stream.readU32();
            layer.maskData.resize(maskSize);
            if (maskSize > 0)
                stream.readRaw(layer.maskData.data(), layer.maskData.size());
            object.terrainLayers.push_back(std::move(layer));
        }

        scene.meshObjects().push_back(std::move(object));
    }

    const std::uint32_t entityCount = stream.readU32();
    scene.entities().reserve(entityCount);
    for (std::uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
    {
        LevelEntityObject entity;
        entity.name = stream.readStr();
        entity.type = static_cast<LevelEntityType>(stream.readU32());
        entity.position = readVec3(stream);
        entity.lightType = static_cast<LightType>(stream.readU32());
        entity.color = readVec3(stream);
        entity.intensity = stream.readF32();
        entity.radius = stream.readF32();
        entity.direction = readVec3(stream);
        entity.spotAngle = stream.readF32();
        entity.spotSoftness = stream.readF32();
        entity.doorType = static_cast<DoorType>(stream.readU32());
        entity.doorDistance = stream.readF32();
        entity.doorSpeed = stream.readF32();
        entity.doorStartOpen = stream.readU8() != 0;
        entity.linkedMeshIndex = stream.readI32();
        entity.endPosition = readVec3(stream);
        entity.moveSpeed = stream.readF32();
        entity.waitTime = stream.readF32();
        entity.itemType = stream.readI32();
        entity.rotationY = stream.readF32();
        scene.entities().push_back(std::move(entity));
    }

    return true;
}
}

bool saveLevelEditorScene(const std::filesystem::path& path,
                          const LevelEditorScene& scene,
                          std::string& error)
{
    if (isBinaryScenePath(path))
        return saveBinaryLevelEditorScene(path, scene, error);

    nlohmann::json root;
    root["version"] = 4;
    root["asset_root"] = scene.assetRoot();
    root["lightmap_path"] = scene.lightmapPath();
    root["creation_pivot_position"] = scene.creationPivotPosition();
    root["creation_pivot_rotation"] = scene.creationPivotRotation();
    root["mesh_objects"] = nlohmann::json::array();
    root["entities"] = nlohmann::json::array();

    for (const LevelMeshObject& object : scene.meshObjects())
    {
        nlohmann::json meshJson;
        meshJson["name"] = object.name;
        meshJson["primitive"] = primitiveTypeToString(object.primitive);
        meshJson["position"] = object.position;
        meshJson["rotation"] = object.rotationEuler;
        meshJson["scale"] = object.scale;
        meshJson["pivot"] = object.pivot;
        meshJson["visible"] = object.visible;
        meshJson["locked"] = object.locked;
        meshJson["blend_enabled"] = object.blendEnabled;
        meshJson["two_sided"] = object.twoSided;
        meshJson["blend_mode"] = blendModeToString(object.blendMode);
        meshJson["vertices"] = nlohmann::json::array();
        meshJson["faces"] = nlohmann::json::array();
        meshJson["terrain_layers"] = nlohmann::json::array();

        for (const EditableVertex& vertex : object.mesh.vertices())
        {
            nlohmann::json vj;
            vj["p"] = vertex.position;
            vj["n"] = vertex.normal;
            if (vertex.uv.x != 0.0f || vertex.uv.y != 0.0f)
                vj["uv"] = {vertex.uv.x, vertex.uv.y};
            meshJson["vertices"].push_back(vj);
        }

        for (const EditableFace& face : object.mesh.faces())
        {
            nlohmann::json faceJson;
            faceJson["indices"] = face.indices;
            faceJson["material"] = face.materialName;
            if (face.uvOffset.x != 0.0f || face.uvOffset.y != 0.0f)
                faceJson["uv_offset"] = {face.uvOffset.x, face.uvOffset.y};
            if (face.uvScale.x != 1.0f || face.uvScale.y != 1.0f)
                faceJson["uv_scale"] = {face.uvScale.x, face.uvScale.y};
            if (face.uvRotation != 0.0f)
                faceJson["uv_rotation"] = face.uvRotation;
            if (face.uvProjection != UvProjection::Box)
                faceJson["uv_projection"] = static_cast<int>(face.uvProjection);
            meshJson["faces"].push_back(faceJson);
        }

        for (const LevelMeshObject::TerrainTextureLayer& layer : object.terrainLayers)
        {
            nlohmann::json layerJson;
            layerJson["name"] = layer.name;
            layerJson["texture"] = layer.texturePath;
            layerJson["opacity"] = layer.opacity;
            layerJson["visible"] = layer.visible;
            if (layer.maskWidth > 0 && layer.maskHeight > 0 && !layer.maskData.empty())
            {
                layerJson["mask_width"] = layer.maskWidth;
                layerJson["mask_height"] = layer.maskHeight;
                layerJson["mask_data"] = layer.maskData;
            }
            meshJson["terrain_layers"].push_back(layerJson);
        }

        root["mesh_objects"].push_back(meshJson);
    }

    for (const LevelEntityObject& entity : scene.entities())
    {
        nlohmann::json entityJson;
        entityJson["name"] = entity.name;
        entityJson["type"] = entityTypeToString(entity.type);
        entityJson["position"] = entity.position;
        if (entity.type == LevelEntityType::Light)
        {
            entityJson["lightType"] = static_cast<int>(entity.lightType);
            entityJson["color"] = {entity.color.x, entity.color.y, entity.color.z};
            entityJson["intensity"] = entity.intensity;
            entityJson["radius"] = entity.radius;
            entityJson["direction"] = {entity.direction.x, entity.direction.y, entity.direction.z};
            entityJson["spotAngle"] = entity.spotAngle;
            entityJson["spotSoftness"] = entity.spotSoftness;
        }
        if (entity.type == LevelEntityType::PlayerStart)
        {
            entityJson["direction"] = {entity.direction.x, entity.direction.y, entity.direction.z};
        }
        if (entity.type == LevelEntityType::Door)
        {
            entityJson["doorType"] = static_cast<int>(entity.doorType);
            entityJson["direction"] = {entity.direction.x, entity.direction.y, entity.direction.z};
            entityJson["doorDistance"] = entity.doorDistance;
            entityJson["doorSpeed"] = entity.doorSpeed;
            entityJson["doorStartOpen"] = entity.doorStartOpen;
            entityJson["linkedMesh"] = entity.linkedMeshIndex;
        }
        if (entity.type == LevelEntityType::Elevator || entity.type == LevelEntityType::Platform)
        {
            entityJson["endPosition"] = {entity.endPosition.x, entity.endPosition.y, entity.endPosition.z};
            entityJson["moveSpeed"] = entity.moveSpeed;
            entityJson["waitTime"] = entity.waitTime;
            entityJson["linkedMesh"] = entity.linkedMeshIndex;
        }
        if (entity.type == LevelEntityType::Placement)
        {
            entityJson["itemType"] = entity.itemType;
            entityJson["rotationY"] = entity.rotationY;
        }
        root["entities"].push_back(entityJson);
    }

    std::ofstream output(path);
    if (!output.is_open())
    {
        error = "could not open file for writing";
        return false;
    }

    output << root.dump(2);
    return true;
}

bool loadLevelEditorScene(const std::filesystem::path& path,
                          LevelEditorScene& scene,
                          std::string& error)
{
    if (isBinarySceneFile(path))
        return loadBinaryLevelEditorScene(path, scene, error);

    std::ifstream input(path);
    if (!input.is_open())
    {
        error = "could not open file for reading";
        return false;
    }

    nlohmann::json root;
    try
    {
        input >> root;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }

    try
    {
        scene.meshObjects().clear();
        scene.entities().clear();
        scene.assetRoot() = root.value("asset_root", std::string("assets"));
        scene.lightmapPath() = root.value("lightmap_path", std::string());
        scene.creationPivotPosition() = root.value("creation_pivot_position", glm::vec3(0.0f));
        scene.creationPivotRotation() = root.value("creation_pivot_rotation", glm::vec3(0.0f));

        const auto& meshObjects = root.value("mesh_objects", nlohmann::json::array());
        for (const auto& meshJson : meshObjects)
        {
            LevelMeshObject object;
            object.name = meshJson.value("name", std::string("Mesh"));
            object.primitive = primitiveTypeFromString(meshJson.value("primitive", std::string("unknown")));
            object.position = meshJson.value("position", glm::vec3(0.0f));
            object.rotationEuler = meshJson.value("rotation", glm::vec3(0.0f));
            object.scale = meshJson.value("scale", glm::vec3(1.0f, 1.0f, 1.0f));
            object.pivot = meshJson.value("pivot", glm::vec3(0.0f));
            object.visible = meshJson.value("visible", true);
            object.locked = meshJson.value("locked", false);
            object.blendEnabled = meshJson.value("blend_enabled", false);
            object.twoSided = meshJson.value("two_sided", false);
            object.blendMode = blendModeFromString(meshJson.value("blend_mode", std::string("alpha")));

            std::vector<EditableVertex> vertices;
            for (const auto& vertexJson : meshJson.value("vertices", nlohmann::json::array()))
            {
                EditableVertex vertex;
                // Support both old format (bare vec3) and new format ({p, n})
                if (vertexJson.is_object())
                {
                    vertex.position = vertexJson.value("p", glm::vec3(0.0f));
                    vertex.normal = vertexJson.value("n", glm::vec3(0.0f, 1.0f, 0.0f));
                    if (vertexJson.contains("uv"))
                    {
                        const auto& arr = vertexJson["uv"];
                        vertex.uv = glm::vec2(arr[0].get<float>(), arr[1].get<float>());
                    }
                }
                else
                {
                    vertex.position = vertexJson.get<glm::vec3>();
                }
                vertices.push_back(vertex);
            }

            std::vector<EditableFace> faces;
            for (const auto& faceJson : meshJson.value("faces", nlohmann::json::array()))
            {
                EditableFace face;
                face.indices = faceJson.value("indices", std::vector<int>{});
                face.materialName = faceJson.value("material", std::string("default"));
                if (faceJson.contains("uv_offset"))
                {
                    auto arr = faceJson["uv_offset"];
                    face.uvOffset = glm::vec2(arr[0].get<float>(), arr[1].get<float>());
                }
                if (faceJson.contains("uv_scale"))
                {
                    auto arr = faceJson["uv_scale"];
                    face.uvScale = glm::vec2(arr[0].get<float>(), arr[1].get<float>());
                }
                face.uvRotation = faceJson.value("uv_rotation", 0.0f);
                const int uvProj = faceJson.value("uv_projection", 0);
                if (uvProj >= static_cast<int>(UvProjection::Box) &&
                    uvProj <= static_cast<int>(UvProjection::Mesh))
                {
                    face.uvProjection = static_cast<UvProjection>(uvProj);
                }
                else
                {
                    face.uvProjection = UvProjection::Box;
                }
                faces.push_back(face);
            }

            object.mesh.setData(vertices, faces);
            for (const auto& layerJson : meshJson.value("terrain_layers", nlohmann::json::array()))
            {
                LevelMeshObject::TerrainTextureLayer layer;
                layer.name = layerJson.value("name", std::string("Layer"));
                layer.texturePath = layerJson.value("texture", std::string());
                layer.opacity = layerJson.value("opacity", 1.0f);
                layer.visible = layerJson.value("visible", true);
                layer.maskWidth = layerJson.value("mask_width", 0);
                layer.maskHeight = layerJson.value("mask_height", 0);
                layer.maskData = layerJson.value("mask_data", std::vector<unsigned char>{});
                object.terrainLayers.push_back(std::move(layer));
            }
            if (object.primitive == LevelMeshPrimitive::Unknown)
            {
                int cols = 0;
                int rows = 0;
                if (detectLegacyTerrainGridDimensions(object.mesh, cols, rows))
                    object.primitive = LevelMeshPrimitive::Terrain;
            }
            scene.meshObjects().push_back(object);
        }

        const auto& entities = root.value("entities", nlohmann::json::array());
        for (const auto& entityJson : entities)
        {
            LevelEntityObject entity;
            entity.name = entityJson.value("name", std::string("Entity"));
            entity.type = entityTypeFromString(entityJson.value("type", std::string("player_start")));
            entity.position = entityJson.value("position", glm::vec3(0.0f));
            if (entity.type == LevelEntityType::Light)
            {
                entity.lightType = static_cast<LightType>(entityJson.value("lightType", 0));
                if (entityJson.contains("color") && entityJson["color"].is_array() && entityJson["color"].size() == 3)
                    entity.color = glm::vec3(entityJson["color"][0].get<float>(),
                                             entityJson["color"][1].get<float>(),
                                             entityJson["color"][2].get<float>());
                entity.intensity = entityJson.value("intensity", 1.0f);
                entity.radius = entityJson.value("radius", 500.0f);
                if (entityJson.contains("direction") && entityJson["direction"].is_array() && entityJson["direction"].size() == 3)
                    entity.direction = glm::vec3(entityJson["direction"][0].get<float>(),
                                                 entityJson["direction"][1].get<float>(),
                                                 entityJson["direction"][2].get<float>());
                entity.spotAngle = entityJson.value("spotAngle", 45.0f);
                entity.spotSoftness = entityJson.value("spotSoftness", 0.1f);
            }
            if (entity.type == LevelEntityType::PlayerStart)
            {
                if (entityJson.contains("direction") && entityJson["direction"].is_array() && entityJson["direction"].size() == 3)
                    entity.direction = glm::vec3(entityJson["direction"][0].get<float>(),
                                                 entityJson["direction"][1].get<float>(),
                                                 entityJson["direction"][2].get<float>());
            }
            if (entity.type == LevelEntityType::Door)
            {
                entity.doorType = static_cast<DoorType>(entityJson.value("doorType", 0));
                if (entityJson.contains("direction") && entityJson["direction"].is_array() && entityJson["direction"].size() == 3)
                    entity.direction = glm::vec3(entityJson["direction"][0].get<float>(),
                                                 entityJson["direction"][1].get<float>(),
                                                 entityJson["direction"][2].get<float>());
                entity.doorDistance = entityJson.value("doorDistance", 128.0f);
                entity.doorSpeed = entityJson.value("doorSpeed", 64.0f);
                entity.doorStartOpen = entityJson.value("doorStartOpen", false);
                entity.linkedMeshIndex = entityJson.value("linkedMesh", -1);
            }
            if (entity.type == LevelEntityType::Elevator || entity.type == LevelEntityType::Platform)
            {
                if (entityJson.contains("endPosition") && entityJson["endPosition"].is_array() && entityJson["endPosition"].size() == 3)
                    entity.endPosition = glm::vec3(entityJson["endPosition"][0].get<float>(),
                                                   entityJson["endPosition"][1].get<float>(),
                                                   entityJson["endPosition"][2].get<float>());
                entity.moveSpeed = entityJson.value("moveSpeed", 64.0f);
                entity.waitTime = entityJson.value("waitTime", 2.0f);
                entity.linkedMeshIndex = entityJson.value("linkedMesh", -1);
            }
            if (entity.type == LevelEntityType::Placement)
            {
                entity.itemType = entityJson.value("itemType", 0);
                entity.rotationY = entityJson.value("rotationY", 0.0f);
            }
            scene.entities().push_back(entity);
        }
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }

    if (scene.meshObjects().empty())
        scene.meshObjects().push_back(LevelMeshObject{});

    return true;
}
