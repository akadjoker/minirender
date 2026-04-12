#include "EditorSceneIO.hpp"

#include "EditorConvexBrushOps.hpp"

#include <fstream>

#include <json.hpp>

namespace nlohmann {
template <>
struct adl_serializer<glm::vec3> {
    static void to_json(json &j, const glm::vec3 &v)
    {
        j = json{v.x, v.y, v.z};
    }

    static void from_json(const json &j, glm::vec3 &v)
    {
        j.at(0).get_to(v.x);
        j.at(1).get_to(v.y);
        j.at(2).get_to(v.z);
    }
};

template <>
struct adl_serializer<glm::vec2> {
    static void to_json(json &j, const glm::vec2 &v)
    {
        j = json{v.x, v.y};
    }

    static void from_json(const json &j, glm::vec2 &v)
    {
        j.at(0).get_to(v.x);
        j.at(1).get_to(v.y);
    }
};

template <>
struct adl_serializer<BrushVolume::FaceUV> {
    static void to_json(json &j, const BrushVolume::FaceUV &uv)
    {
        j = json{
            {"offset", uv.offset},
            {"scale", uv.scale},
            {"rotation", uv.rotation}
        };
    }

    static void from_json(const json &j, BrushVolume::FaceUV &uv)
    {
        uv.offset = j.value("offset", glm::vec2(0.0f));
        uv.scale = j.value("scale", glm::vec2(1.0f));
        uv.rotation = j.value("rotation", 0.0f);
    }
};

template <>
struct adl_serializer<BrushVolume> {
    static void to_json(json &j, const BrushVolume &brush)
    {
        j = json{
            {"mins", brush.mins},
            {"maxs", brush.maxs},
            {"color", brush.color},
            {"name", brush.name},
            {"hidden", brush.hidden},
            {"texturePath", brush.texturePath},
            {"faceTextures", brush.faceTextures},
            {"faceUV", brush.faceUV},
            {"uvOffset", brush.uvOffset},
            {"uvScale", brush.uvScale},
            {"uvRotation", brush.uvRotation}
        };
    }

    static void from_json(const json &j, BrushVolume &brush)
    {
        brush.mins = j.value("mins", glm::vec3(0.0f));
        brush.maxs = j.value("maxs", glm::vec3(0.0f));
        brush.color = j.value("color", glm::vec3(0.47f, 0.82f, 1.0f));
        brush.name = j.value("name", std::string());
        brush.hidden = j.value("hidden", false);
        brush.texturePath = j.value("texturePath", std::string());
        brush.faceTextures = j.value("faceTextures", std::array<std::string, 6>{});
        brush.faceUV = j.value("faceUV", std::array<BrushVolume::FaceUV, 6>{});
        brush.uvOffset = j.value("uvOffset", glm::vec2(0.0f));
        brush.uvScale = j.value("uvScale", glm::vec2(1.0f));
        brush.uvRotation = j.value("uvRotation", 0.0f);
        brush.dirty = true;
    }
};

template <>
struct adl_serializer<EditorKeyValue> {
    static void to_json(json &j, const EditorKeyValue &kv)
    {
        j = json{
            {"key", kv.key},
            {"value", kv.value}
        };
    }

    static void from_json(const json &j, EditorKeyValue &kv)
    {
        kv.key = j.value("key", std::string());
        kv.value = j.value("value", std::string());
    }
};

template <>
struct adl_serializer<EditorBrushPrimitive> {
    static void to_json(json &j, const EditorBrushPrimitive &primitive)
    {
        j = static_cast<int>(primitive);
    }

    static void from_json(const json &j, EditorBrushPrimitive &primitive)
    {
        primitive = static_cast<EditorBrushPrimitive>(j.get<int>());
    }
};

template <>
struct adl_serializer<EditorBrushFace> {
    static void to_json(json &j, const EditorBrushFace &face)
    {
        j = json{
            {"planePoints", face.planePoints},
            {"texturePath", face.texturePath},
            {"uvOffset", face.uvOffset},
            {"uvScale", face.uvScale},
            {"uvRotation", face.uvRotation}
        };
    }

    static void from_json(const json &j, EditorBrushFace &face)
    {
        face.planePoints = j.value("planePoints", std::array<glm::vec3, 3>{});
        face.texturePath = j.value("texturePath", std::string());
        face.uvOffset = j.value("uvOffset", glm::vec2(0.0f));
        face.uvScale = j.value("uvScale", glm::vec2(1.0f));
        face.uvRotation = j.value("uvRotation", 0.0f);
    }
};

template <>
struct adl_serializer<EditorBrush> {
    static void to_json(json &j, const EditorBrush &brush)
    {
        j = json{
            {"name", brush.name},
            {"primitive", brush.primitive},
            {"color", brush.color},
            {"hidden", brush.hidden},
            {"faces", brush.faces}
        };
    }

    static void from_json(const json &j, EditorBrush &brush)
    {
        brush.name = j.value("name", std::string());
        brush.primitive = j.value("primitive", EditorBrushPrimitive::Custom);
        brush.color = j.value("color", glm::vec3(0.47f, 0.82f, 1.0f));
        brush.hidden = j.value("hidden", false);
        brush.faces = j.value("faces", std::vector<EditorBrushFace>{});
        brush.dirty = true;
    }
};

template <>
struct adl_serializer<EditorEntity> {
    static void to_json(json &j, const EditorEntity &entity)
    {
        j = json{
            {"name", entity.name},
            {"classname", entity.classname},
            {"origin", entity.origin},
            {"hidden", entity.hidden},
            {"keyvalues", entity.keyvalues},
            {"brushes", entity.brushes},
            {"convexBrushes", entity.convexBrushes}
        };
    }

    static void from_json(const json &j, EditorEntity &entity)
    {
        entity.name = j.value("name", std::string());
        entity.classname = j.value("classname", std::string("worldspawn"));
        entity.origin = j.value("origin", glm::vec3(0.0f));
        entity.hidden = j.value("hidden", false);
        entity.keyvalues = j.value("keyvalues", std::vector<EditorKeyValue>{});
        entity.brushes = j.value("brushes", std::vector<BrushVolume>{});
        entity.convexBrushes = j.value("convexBrushes", std::vector<EditorBrush>{});
    }
};

void to_json(json &j, const EditorLayoutMode &m)
{
    j = static_cast<int>(m);
}

void from_json(const json &j, EditorLayoutMode &m)
{
    m = static_cast<EditorLayoutMode>(j.get<int>());
}

void to_json(json &j, const EditorRenderingMode &m)
{
    j = static_cast<int>(m);
}

void from_json(const json &j, EditorRenderingMode &m)
{
    m = static_cast<EditorRenderingMode>(j.get<int>());
}
} // namespace nlohmann

struct EditorSceneData
{
    int version = 2;
    glm::vec3 focus = glm::vec3(0.0f);
    std::string currentTexturePath;
    std::vector<EditorEntity> entities;
    std::vector<BrushVolume> brushes;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EditorSceneData, version, focus, currentTexturePath, entities, brushes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EditorSettings::ViewSettings, orthoSize, perspectiveDistance, perspectiveYaw, perspectivePitch)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EditorSettings, assetRoot, currentTexturePath, focus, gridStep, snapSize,
                                   layoutMode, defaultBrushThickness, defaultBrushHeight, showGrid, showAxes,
                                   snapEnabled, sidebarTopHeight, assetPanelHeight, sidebarWidth, assetViewAsGrid,
                                   renderingMode, enableTransparency, transparency, textureLock, views)

static std::string makePortablePathForSave(const std::string &rawPath);
std::string resolveTexturePathForLoad(const std::string &rawPath);

static EditorEntity makeWorldspawnEntity(const std::vector<BrushVolume> &brushes)
{
    EditorEntity worldspawn;
    worldspawn.name = "World";
    worldspawn.classname = "worldspawn";
    worldspawn.brushes = brushes;
    return worldspawn;
}

static void normalizeEntityPathsForSave(EditorEntity &entity)
{
    for (BrushVolume &brush : entity.brushes)
    {
        brush.texturePath = makePortablePathForSave(brush.texturePath);
        for (std::string &faceTexture : brush.faceTextures)
            faceTexture = makePortablePathForSave(faceTexture);
    }

    for (EditorBrush &brush : entity.convexBrushes)
    {
        for (EditorBrushFace &face : brush.faces)
            face.texturePath = makePortablePathForSave(face.texturePath);
    }
}

static int normalizeEntityPathsForLoad(EditorEntity &entity)
{
    int fixedPaths = 0;
    for (BrushVolume &brush : entity.brushes)
    {
        const std::string oldTexturePath = brush.texturePath;
        brush.texturePath = resolveTexturePathForLoad(brush.texturePath);
        if (brush.texturePath != oldTexturePath)
            ++fixedPaths;

        for (std::string &faceTexture : brush.faceTextures)
        {
            const std::string oldFace = faceTexture;
            faceTexture = resolveTexturePathForLoad(faceTexture);
            if (faceTexture != oldFace)
                ++fixedPaths;
        }
    }

    for (EditorBrush &brush : entity.convexBrushes)
    {
        for (EditorBrushFace &face : brush.faces)
        {
            const std::string oldTexturePath = face.texturePath;
            face.texturePath = resolveTexturePathForLoad(face.texturePath);
            if (face.texturePath != oldTexturePath)
                ++fixedPaths;
        }
    }
    return fixedPaths;
}

static void syncEntityLegacyBrushesToConvexBrushes(EditorEntity &entity)
{
    std::vector<EditorBrush> extras;
    if (entity.convexBrushes.size() > entity.brushes.size())
    {
        extras.assign(entity.convexBrushes.begin() + (ptrdiff_t)entity.brushes.size(),
                      entity.convexBrushes.end());
    }

    entity.convexBrushes.clear();
    entity.convexBrushes.reserve(entity.brushes.size() + extras.size());
    for (const BrushVolume &brush : entity.brushes)
        entity.convexBrushes.push_back(makeConvexBrushFromVolume(brush));
    entity.convexBrushes.insert(entity.convexBrushes.end(), extras.begin(), extras.end());
}

void saveEditorSettings(const EditorSettings &settings, const std::string &filename)
{
    nlohmann::json j = settings;
    std::ofstream file(filename);
    if (file.is_open())
    {
        file << j.dump(4);
        file.close();
    }
}

bool loadEditorSettings(EditorSettings &settings, const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        return false;

    try
    {
        nlohmann::json j;
        file >> j;
        settings = j.get<EditorSettings>();
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

std::filesystem::path ensureSceneExtension(const std::filesystem::path &path)
{
    if (path.extension() == ".mred")
        return path;
    std::filesystem::path out = path;
    out.replace_extension(".mred");
    return out;
}

static std::string normalizePathSlashes(std::string path)
{
    for (char &c : path)
    {
        if (c == '\\')
            c = '/';
    }
    return path;
}

static bool pathExistsFile(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

static std::string makePortablePathForSave(const std::string &rawPath)
{
    if (rawPath.empty())
        return rawPath;

    const std::filesystem::path path = std::filesystem::path(rawPath).lexically_normal();
    if (!path.is_absolute())
        return normalizePathSlashes(path.generic_string());

    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec).lexically_normal();
    if (!ec)
    {
        std::error_code relEc;
        const std::filesystem::path rel = std::filesystem::relative(path, cwd, relEc);
        if (!relEc && !rel.empty())
        {
            const std::string relStr = rel.generic_string();
            if (!(relStr.rfind("..", 0) == 0))
                return normalizePathSlashes(relStr);
        }
    }

    return normalizePathSlashes(path.generic_string());
}

std::string resolveTexturePathForLoad(const std::string &rawPath)
{
    if (rawPath.empty())
        return rawPath;

    const std::string normalized = normalizePathSlashes(rawPath);
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec).lexically_normal();
    const std::filesystem::path cwdParent = cwd.parent_path();

    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(std::filesystem::path(normalized));
    if (!ec)
    {
        candidates.emplace_back((cwd / normalized).lexically_normal());
        if (!cwdParent.empty())
            candidates.emplace_back((cwdParent / normalized).lexically_normal());
    }

    const std::string marker = "assets/";
    const size_t assetsPos = normalized.find(marker);
    if (assetsPos != std::string::npos && !ec)
    {
        const std::string assetsTail = normalized.substr(assetsPos);
        candidates.emplace_back((cwd / assetsTail).lexically_normal());
        if (!cwdParent.empty())
            candidates.emplace_back((cwdParent / assetsTail).lexically_normal());
    }

    for (const auto &candidate : candidates)
    {
        if (pathExistsFile(candidate))
            return normalizePathSlashes(candidate.generic_string());
    }

    return normalized;
}

bool saveEditorScene(const std::filesystem::path &path,
                     const std::vector<BrushVolume> &brushes,
                     const glm::vec3 &focus,
                     const std::string &currentTexturePath,
                     std::string &error)
{
    std::vector<EditorEntity> entities;
    entities.push_back(makeWorldspawnEntity(brushes));
    return saveEditorScene(path, entities, focus, currentTexturePath, error);
}

bool saveEditorScene(const std::filesystem::path &path,
                     const std::vector<EditorEntity> &entities,
                     const glm::vec3 &focus,
                     const std::string &currentTexturePath,
                     std::string &error)
{
    error.clear();
    std::error_code ec;
    const std::filesystem::path finalPath = ensureSceneExtension(path);
    const std::filesystem::path parent = finalPath.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);

    EditorSceneData scene;
    scene.version = 2;
    scene.focus = focus;
    scene.currentTexturePath = makePortablePathForSave(currentTexturePath);
    scene.entities = entities;
    if (scene.entities.empty())
        scene.entities.push_back(makeWorldspawnEntity({}));
    for (EditorEntity &entity : scene.entities)
    {
        syncEntityLegacyBrushesToConvexBrushes(entity);
        normalizeEntityPathsForSave(entity);
    }

    try
    {
        nlohmann::json j = scene;
        std::ofstream file(finalPath);
        if (!file.is_open())
        {
            error = "Nao foi possivel abrir ficheiro para escrita: " + finalPath.string();
            return false;
        }
        file << j.dump(2);
        file.close();
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}

bool loadEditorScene(const std::filesystem::path &path,
                     std::vector<BrushVolume> &brushes,
                     glm::vec3 &focus,
                     std::string &currentTexturePath,
                     std::string &error)
{
    std::vector<EditorEntity> entities;
    if (!loadEditorScene(path, entities, focus, currentTexturePath, error))
        return false;

    brushes.clear();
    for (const EditorEntity &entity : entities)
    {
        brushes.insert(brushes.end(), entity.brushes.begin(), entity.brushes.end());
    }
    return true;
}

bool loadEditorScene(const std::filesystem::path &path,
                     std::vector<EditorEntity> &entities,
                     glm::vec3 &focus,
                     std::string &currentTexturePath,
                     std::string &error)
{
    error.clear();
    try
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            error = "Nao foi possivel abrir ficheiro: " + path.string();
            return false;
        }

        nlohmann::json j;
        file >> j;

        EditorSceneData scene = j.get<EditorSceneData>();
        focus = scene.focus;
        if (!scene.currentTexturePath.empty())
            currentTexturePath = resolveTexturePathForLoad(scene.currentTexturePath);

        entities = scene.entities;
        if (entities.empty() && !scene.brushes.empty())
            entities.push_back(makeWorldspawnEntity(scene.brushes));
        if (entities.empty())
            entities.push_back(makeWorldspawnEntity({}));

        for (EditorEntity &entity : entities)
        {
            if (entity.convexBrushes.empty() && !entity.brushes.empty())
                syncEntityLegacyBrushesToConvexBrushes(entity);
        }

        int fixedPaths = 0;
        for (EditorEntity &entity : entities)
            fixedPaths += normalizeEntityPathsForLoad(entity);

        if (fixedPaths > 0)
            error = "fixed_paths=" + std::to_string(fixedPaths);
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}
