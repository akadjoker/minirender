#include "genesis/Genesis3dtFile.hpp"

#include <sstream>

#include "genesis/GenesisUtils.hpp"

namespace mini_genesis
{
bool Genesis3dtFile::load(const std::string &path, Map3dtData &out, std::string &error) const
{
    out = {};

    std::string text;
    if (!readFileText(path, text, error))
        return false;

    std::istringstream in(text);
    std::string raw;

    Brush currentBrush;
    BrushFace currentFace;
    bool inBrush = false;
    bool inFace = false;
    bool inEntity = false;
    Entity currentEntity;

    while (std::getline(in, raw))
    {
        const std::string line = trim(raw);
        if (line.empty())
            continue;

        if (startsWith(line, "3dtVersion"))
        {
            const size_t p = line.find(' ');
            if (p != std::string::npos)
                out.version = trim(line.substr(p + 1));
            continue;
        }
        if (startsWith(line, "TextureLib"))
        {
            out.textureLib = parseQuoted(line);
            continue;
        }
        if (startsWith(line, "NumEntities"))
        {
            std::istringstream ls(line);
            std::string k;
            ls >> k >> out.numEntitiesHeader;
            continue;
        }
        if (startsWith(line, "NumModels"))
        {
            std::istringstream ls(line);
            std::string k;
            ls >> k >> out.numModelsHeader;
            continue;
        }

        if (startsWith(line, "CEntity"))
        {
            inEntity = true;
            currentEntity = {};
            continue;
        }
        if (startsWith(line, "End CEntity"))
        {
            if (inEntity)
                out.entities.push_back(std::move(currentEntity));
            inEntity = false;
            continue;
        }
        if (inEntity && startsWith(line, "Key "))
        {
            const size_t k0 = line.find(' ');
            const size_t kv = line.find(" Value ");
            if (k0 != std::string::npos && kv != std::string::npos && kv > k0)
            {
                const std::string key = trim(line.substr(k0 + 1, kv - (k0 + 1)));
                const std::string value = parseQuoted(line.substr(kv + 1));
                currentEntity.kv[toLower(key)] = value;
            }
            continue;
        }

        if (startsWith(line, "Brush "))
        {
            if (inFace)
            {
                currentBrush.faces.push_back(std::move(currentFace));
                inFace = false;
            }
            if (inBrush)
                out.brushes.push_back(std::move(currentBrush));

            inBrush = true;
            currentBrush = {};
            currentBrush.name = parseQuoted(line);
            continue;
        }

        if (!inBrush)
            continue;

        if (startsWith(line, "Flags "))
        {
            std::istringstream ls(line);
            std::string k;
            ls >> k >> currentBrush.flags;
            continue;
        }
        if (startsWith(line, "ModelId "))
        {
            std::istringstream ls(line);
            std::string k;
            ls >> k >> currentBrush.modelId;
            continue;
        }
        if (startsWith(line, "GroupId "))
        {
            std::istringstream ls(line);
            std::string k;
            ls >> k >> currentBrush.groupId;
            continue;
        }
        if (startsWith(line, "HullSize "))
        {
            std::istringstream ls(line);
            std::string k;
            ls >> k >> currentBrush.hullSize;
            continue;
        }
        if (startsWith(line, "Type "))
        {
            std::istringstream ls(line);
            std::string k;
            ls >> k >> currentBrush.type;
            continue;
        }

        if (startsWith(line, "NumPoints "))
        {
            if (inFace)
                currentBrush.faces.push_back(std::move(currentFace));
            currentFace = {};
            inFace = true;
            continue;
        }

        if (inFace && startsWith(line, "Vec3d "))
        {
            std::istringstream ls(line);
            std::string k;
            glm::vec3 p(0.0f);
            ls >> k >> p.x >> p.y >> p.z;
            currentFace.points.push_back(p);
            continue;
        }

        if (inFace && startsWith(line, "TexInfo "))
        {
            // Format: TexInfo Rotate R Shift U V Scale SU SV Name "tex"
            std::istringstream ls(line);
            std::string tmp;
            ls >> tmp; // TexInfo
            ls >> tmp; // Rotate
            ls >> currentFace.rotate;
            ls >> tmp; // Shift
            ls >> currentFace.shift.x >> currentFace.shift.y;
            ls >> tmp; // Scale
            ls >> currentFace.scale.x >> currentFace.scale.y;
            const std::string tex = parseQuoted(line);
            if (!tex.empty())
                currentFace.texture = tex;
            continue;
        }

        if (inFace && startsWith(line, "LightScale "))
        {
            // End-of-face marker in 3dt blocks.
            currentBrush.faces.push_back(std::move(currentFace));
            currentFace = {};
            inFace = false;
            continue;
        }
    }

    if (inFace)
        currentBrush.faces.push_back(std::move(currentFace));
    if (inBrush)
        out.brushes.push_back(std::move(currentBrush));

    if (out.brushes.empty() && out.entities.empty())
    {
        error = "3dt sem brushes e sem entidades";
        return false;
    }

    return true;
}
} // namespace mini_genesis
