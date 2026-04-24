#include "LevelEditorSceneIO.hpp"

#include "BinaryStream.hpp"

#include <cstdint>
#include <fstream>

namespace
{
constexpr std::uint32_t kBinarySceneMagic = 0x4D524C45u; // "ELRM"
constexpr std::uint32_t kBinarySceneVersion = 7;
 
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
    stream.writeI32(scene.lightmapAtlasCount());
    writeVec3(stream, scene.creationPivotPosition());
    writeVec3(stream, scene.creationPivotRotation());
    stream.writeU32(static_cast<std::uint32_t>(scene.lightmapUVs().size()));
    for (const LevelMeshLightmapUVs& meshUVs : scene.lightmapUVs())
    {
        stream.writeU32(static_cast<std::uint32_t>(meshUVs.faceVertexUVs.size()));
        for (const auto& faceUVs : meshUVs.faceVertexUVs)
        {
            stream.writeU32(static_cast<std::uint32_t>(faceUVs.size()));
            for (const glm::vec2& uv : faceUVs)
                writeVec2(stream, uv);
        }
        stream.writeU32(static_cast<std::uint32_t>(meshUVs.faceAtlasIndices.size()));
        for (int atlasIndex : meshUVs.faceAtlasIndices)
            stream.writeI32(atlasIndex);
    }

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
        // Trigger
        stream.writeF32(entity.triggerRadius);
        stream.writeStr(entity.targetName);
        // Teleporter
        writeVec3(stream, entity.teleportTarget);
        // SoundEmitter
        stream.writeStr(entity.soundPath);
        stream.writeF32(entity.soundRadius);
        stream.writeF32(entity.soundVolume);
        stream.writeU8(entity.soundLooping ? 1u : 0u);
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
    scene.lightmapAtlasCount() = (version >= 7) ? stream.readI32() : (!scene.lightmapPath().empty() ? 1 : 0);
    scene.creationPivotPosition() = (version >= 4) ? readVec3(stream) : glm::vec3(0.0f);
    scene.creationPivotRotation() = (version >= 4) ? readVec3(stream) : glm::vec3(0.0f);
    scene.lightmapUVs().clear();
    if (version >= 6)
    {
        const std::uint32_t meshUvCount = stream.readU32();
        scene.lightmapUVs().resize(meshUvCount);
        for (std::uint32_t meshIndex = 0; meshIndex < meshUvCount; ++meshIndex)
        {
            const std::uint32_t faceCount = stream.readU32();
            scene.lightmapUVs()[meshIndex].faceVertexUVs.resize(faceCount);
            for (std::uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex)
            {
                const std::uint32_t uvCount = stream.readU32();
                scene.lightmapUVs()[meshIndex].faceVertexUVs[faceIndex].reserve(uvCount);
                for (std::uint32_t uvIndex = 0; uvIndex < uvCount; ++uvIndex)
                    scene.lightmapUVs()[meshIndex].faceVertexUVs[faceIndex].push_back(readVec2(stream));
            }
            if (version >= 7)
            {
                const std::uint32_t atlasIndexCount = stream.readU32();
                scene.lightmapUVs()[meshIndex].faceAtlasIndices.reserve(atlasIndexCount);
                for (std::uint32_t atlasIndex = 0; atlasIndex < atlasIndexCount; ++atlasIndex)
                    scene.lightmapUVs()[meshIndex].faceAtlasIndices.push_back(stream.readI32());
            }
            else
            {
                scene.lightmapUVs()[meshIndex].faceAtlasIndices.assign(faceCount, 0);
            }
        }
    }

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
        // Trigger
        entity.triggerRadius = stream.readF32();
        entity.targetName = stream.readStr();
        // Teleporter
        entity.teleportTarget = readVec3(stream);
        // SoundEmitter
        entity.soundPath = stream.readStr();
        entity.soundRadius = stream.readF32();
        entity.soundVolume = stream.readF32();
        entity.soundLooping = stream.readU8() != 0;
        scene.entities().push_back(std::move(entity));
    }

    return true;
}
}

bool saveLevelEditorScene(const std::filesystem::path& path,
                          const LevelEditorScene& scene,
                          std::string& error)
{
    // Always save as binary — ensure extension is binary
    std::filesystem::path binPath = path;
    if (binPath.extension() != ".mredb" && binPath.extension() != ".mrbin")
        binPath.replace_extension(".mredb");
    return saveBinaryLevelEditorScene(binPath, scene, error);
}

bool loadLevelEditorScene(const std::filesystem::path& path,
                          LevelEditorScene& scene,
                          std::string& error)
{
    // Always load as binary
    return loadBinaryLevelEditorScene(path, scene, error);
}
