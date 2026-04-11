#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

struct TextMapKeyValue
{
    std::string keyword;
    std::string value;
};

struct TextMapFace
{
    glm::vec3 points[3] = {};
    std::string texture;
    float shiftX = 0.0f;
    float shiftY = 0.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    std::string comment;
    int line = 0;
};

struct TextMapBrush
{
    std::vector<TextMapFace> faces;
    int line = 0;
};

struct TextMapEntity
{
    std::vector<TextMapKeyValue> keypairs;
    std::vector<TextMapBrush> brushes;
    int line = 0;

    const TextMapKeyValue *findKey(const std::string &keyword) const;
    std::string value(const std::string &keyword, const std::string &fallback = "") const;
    glm::vec3 origin(const glm::vec3 &fallback = glm::vec3(0.0f)) const;
    std::string classname() const { return value("classname"); }
};

struct TextMapDocument
{
    std::vector<TextMapEntity> entities;

    void clear();
    int entityCount() const { return (int)entities.size(); }
    int brushCount() const;
    int faceCount() const;
    const TextMapEntity *worldspawn() const;
};

class TextMapParser
{
public:
    static bool loadFromFile(const std::string &path, TextMapDocument &outDocument, std::string *error = nullptr);
    static bool parseText(const std::string &text, TextMapDocument &outDocument, std::string *error = nullptr);
};
