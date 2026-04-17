#include "LevelEditorSceneIO.hpp"

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
std::string entityTypeToString(LevelEntityType type)
{
    switch (type)
    {
    case LevelEntityType::PlayerStart: return "player_start";
    case LevelEntityType::Light: return "light";
    case LevelEntityType::Door: return "door";
    case LevelEntityType::Elevator: return "elevator";
    }
    return "entity";
}

LevelEntityType entityTypeFromString(const std::string& value)
{
    if (value == "player_start") return LevelEntityType::PlayerStart;
    if (value == "light") return LevelEntityType::Light;
    if (value == "door") return LevelEntityType::Door;
    if (value == "elevator") return LevelEntityType::Elevator;
    return LevelEntityType::PlayerStart;
}
}

bool saveLevelEditorScene(const std::filesystem::path& path,
                          const LevelEditorScene& scene,
                          std::string& error)
{
    nlohmann::json root;
    root["version"] = 2;
    root["mesh_objects"] = nlohmann::json::array();
    root["entities"] = nlohmann::json::array();

    for (const LevelMeshObject& object : scene.meshObjects())
    {
        nlohmann::json meshJson;
        meshJson["name"] = object.name;
        meshJson["position"] = object.position;
        meshJson["rotation"] = object.rotationEuler;
        meshJson["scale"] = object.scale;
        meshJson["pivot"] = object.pivot;
        meshJson["visible"] = object.visible;
        meshJson["locked"] = object.locked;
        meshJson["vertices"] = nlohmann::json::array();
        meshJson["faces"] = nlohmann::json::array();

        for (const EditableVertex& vertex : object.mesh.vertices())
        {
            nlohmann::json vj;
            vj["p"] = vertex.position;
            vj["n"] = vertex.normal;
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
            meshJson["faces"].push_back(faceJson);
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

        const auto& meshObjects = root.value("mesh_objects", nlohmann::json::array());
        for (const auto& meshJson : meshObjects)
        {
            LevelMeshObject object;
            object.name = meshJson.value("name", std::string("Mesh"));
            object.position = meshJson.value("position", glm::vec3(0.0f));
            object.rotationEuler = meshJson.value("rotation", glm::vec3(0.0f));
            object.scale = meshJson.value("scale", glm::vec3(1.0f, 1.0f, 1.0f));
            object.pivot = meshJson.value("pivot", glm::vec3(0.0f));
            object.visible = meshJson.value("visible", true);
            object.locked = meshJson.value("locked", false);

            std::vector<EditableVertex> vertices;
            for (const auto& vertexJson : meshJson.value("vertices", nlohmann::json::array()))
            {
                EditableVertex vertex;
                // Support both old format (bare vec3) and new format ({p, n})
                if (vertexJson.is_object())
                {
                    vertex.position = vertexJson.value("p", glm::vec3(0.0f));
                    vertex.normal = vertexJson.value("n", glm::vec3(0.0f, 1.0f, 0.0f));
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
                faces.push_back(face);
            }

            object.mesh.setData(vertices, faces);
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
