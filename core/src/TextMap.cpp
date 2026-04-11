#include "TextMap.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{
std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace((unsigned char)value[start]))
        ++start;

    size_t end = value.size();
    while (end > start && std::isspace((unsigned char)value[end - 1]))
        --end;

    return value.substr(start, end - start);
}

bool parseQuotedString(const std::string &line, size_t &cursor, std::string &out)
{
    const size_t begin = line.find('"', cursor);
    if (begin == std::string::npos)
        return false;

    const size_t end = line.find('"', begin + 1);
    if (end == std::string::npos)
        return false;

    out = line.substr(begin + 1, end - begin - 1);
    cursor = end + 1;
    return true;
}

bool parseKeyValueLine(const std::string &line, TextMapKeyValue &out)
{
    size_t cursor = 0;
    if (!parseQuotedString(line, cursor, out.keyword))
        return false;
    if (!parseQuotedString(line, cursor, out.value))
        return false;
    return true;
}

void skipSpaces(const char *&cursor)
{
    while (*cursor && std::isspace((unsigned char)*cursor))
        ++cursor;
}

bool consume(const char *&cursor, char expected)
{
    skipSpaces(cursor);
    if (*cursor != expected)
        return false;
    ++cursor;
    return true;
}

bool parseFloatToken(const char *&cursor, float &out)
{
    skipSpaces(cursor);
    if (!*cursor)
        return false;

    char *end = nullptr;
    errno = 0;
    const float value = std::strtof(cursor, &end);
    if (end == cursor || errno == ERANGE)
        return false;

    out = value;
    cursor = end;
    return true;
}

bool parseVec3(const char *&cursor, glm::vec3 &out)
{
    if (!consume(cursor, '('))
        return false;
    if (!parseFloatToken(cursor, out.x))
        return false;
    if (!parseFloatToken(cursor, out.y))
        return false;
    if (!parseFloatToken(cursor, out.z))
        return false;
    if (!consume(cursor, ')'))
        return false;
    return true;
}

bool parseWord(const char *&cursor, std::string &out)
{
    skipSpaces(cursor);
    if (!*cursor)
        return false;

    const char *start = cursor;
    while (*cursor && !std::isspace((unsigned char)*cursor) && *cursor != ';')
        ++cursor;

    if (cursor == start)
        return false;

    out.assign(start, cursor - start);
    return true;
}

bool parseFaceLine(const std::string &line, int lineNumber, TextMapFace &out)
{
    const char *cursor = line.c_str();
    if (!parseVec3(cursor, out.points[0]))
        return false;
    if (!parseVec3(cursor, out.points[1]))
        return false;
    if (!parseVec3(cursor, out.points[2]))
        return false;
    if (!parseWord(cursor, out.texture))
        return false;
    if (!parseFloatToken(cursor, out.shiftX))
        return false;
    if (!parseFloatToken(cursor, out.shiftY))
        return false;
    if (!parseFloatToken(cursor, out.rotation))
        return false;
    if (!parseFloatToken(cursor, out.scaleX))
        return false;
    if (!parseFloatToken(cursor, out.scaleY))
        return false;

    skipSpaces(cursor);
    if (*cursor == ';')
        out.comment = trim(cursor + 1);

    out.line = lineNumber;
    return true;
}

glm::vec3 parseVec3Property(const std::string &value, const glm::vec3 &fallback)
{
    std::istringstream stream(value);
    glm::vec3 result = fallback;
    if (!(stream >> result.x >> result.y >> result.z))
        return fallback;
    return result;
}

std::string makeError(int line, const std::string &message)
{
    std::ostringstream out;
    out << "line " << line << ": " << message;
    return out.str();
}

bool isIgnorableLine(const std::string &line)
{
    return line.empty() || line[0] == ';';
}

int nextMeaningfulLineIndex(const std::vector<std::string> &lines, int startIndex)
{
    for (int i = startIndex; i < (int)lines.size(); ++i)
    {
        if (!isIgnorableLine(lines[i]))
            return i;
    }
    return -1;
}

bool parseBrushBlock(const std::vector<std::string> &lines,
                     int &index,
                     TextMapBrush &outBrush,
                     std::string *error)
{
    while (index < (int)lines.size())
    {
        const std::string &trimmed = lines[index];
        const int lineNumber = index + 1;

        if (isIgnorableLine(trimmed))
        {
            ++index;
            continue;
        }

        if (trimmed == "}")
        {
            ++index;
            return true;
        }

        TextMapFace face;
        if (!parseFaceLine(trimmed, lineNumber, face))
        {
            if (error)
                *error = makeError(lineNumber, "invalid brush face line");
            return false;
        }

        outBrush.faces.push_back(face);
        ++index;
    }

    if (error)
        *error = "unexpected end of file while parsing brush";
    return false;
}

bool parseEntityBlock(const std::vector<std::string> &lines,
                      int &index,
                      int startLine,
                      TextMapDocument &outDocument,
                      std::string *error)
{
    TextMapEntity entity;
    entity.line = startLine;

    while (index < (int)lines.size())
    {
        const std::string &trimmed = lines[index];
        const int lineNumber = index + 1;

        if (isIgnorableLine(trimmed))
        {
            ++index;
            continue;
        }

        if (trimmed == "}")
        {
            outDocument.entities.push_back(entity);
            ++index;
            return true;
        }

        if (trimmed == "{")
        {
            const int nestedIndex = nextMeaningfulLineIndex(lines, index + 1);
            if (nestedIndex < 0)
            {
                if (error)
                    *error = "unexpected end of file after '{'";
                return false;
            }

            const std::string &nestedLine = lines[nestedIndex];
            ++index;
            if (!nestedLine.empty() && nestedLine[0] == '"')
            {
                if (!parseEntityBlock(lines, index, lineNumber, outDocument, error))
                    return false;
            }
            else
            {
                TextMapBrush brush;
                brush.line = lineNumber;
                if (!parseBrushBlock(lines, index, brush, error))
                    return false;
                entity.brushes.push_back(brush);
            }
            continue;
        }

        TextMapKeyValue kv;
        if (!parseKeyValueLine(trimmed, kv))
        {
            if (error)
                *error = makeError(lineNumber, "invalid key/value line");
            return false;
        }
        entity.keypairs.push_back(kv);
        ++index;
    }

    if (error)
        *error = "unexpected end of file while parsing entity";
    return false;
}
}

const TextMapKeyValue *TextMapEntity::findKey(const std::string &keyword) const
{
    for (size_t i = 0; i < keypairs.size(); ++i)
    {
        if (keypairs[i].keyword == keyword)
            return &keypairs[i];
    }
    return nullptr;
}

std::string TextMapEntity::value(const std::string &keyword, const std::string &fallback) const
{
    const TextMapKeyValue *found = findKey(keyword);
    return found ? found->value : fallback;
}

glm::vec3 TextMapEntity::origin(const glm::vec3 &fallback) const
{
    const TextMapKeyValue *found = findKey("origin");
    return found ? parseVec3Property(found->value, fallback) : fallback;
}

void TextMapDocument::clear()
{
    entities.clear();
}

int TextMapDocument::brushCount() const
{
    int total = 0;
    for (size_t i = 0; i < entities.size(); ++i)
        total += (int)entities[i].brushes.size();
    return total;
}

int TextMapDocument::faceCount() const
{
    int total = 0;
    for (size_t i = 0; i < entities.size(); ++i)
    {
        for (size_t j = 0; j < entities[i].brushes.size(); ++j)
            total += (int)entities[i].brushes[j].faces.size();
    }
    return total;
}

const TextMapEntity *TextMapDocument::worldspawn() const
{
    for (size_t i = 0; i < entities.size(); ++i)
    {
        if (entities[i].classname() == "worldspawn")
            return &entities[i];
    }
    return nullptr;
}

bool TextMapParser::loadFromFile(const std::string &path, TextMapDocument &outDocument, std::string *error)
{
    std::ifstream file(path);
    if (!file)
    {
        if (error)
            *error = "failed to open file: " + path;
        return false;
    }

    std::ostringstream text;
    text << file.rdbuf();
    return parseText(text.str(), outDocument, error);
}

bool TextMapParser::parseText(const std::string &text, TextMapDocument &outDocument, std::string *error)
{
    outDocument.clear();

    std::istringstream input(text);
    std::string rawLine;
    std::vector<std::string> lines;
    while (std::getline(input, rawLine))
        lines.push_back(trim(rawLine));

    int index = 0;
    while (index < (int)lines.size())
    {
        const std::string &trimmed = lines[index];
        const int lineNumber = index + 1;

        if (isIgnorableLine(trimmed))
        {
            ++index;
            continue;
        }

        if (trimmed != "{")
        {
            if (error)
                *error = makeError(lineNumber, "expected entity start '{'");
            return false;
        }

        ++index;
        if (!parseEntityBlock(lines, index, lineNumber, outDocument, error))
            return false;
    }

    return true;
}
