#include "M8Texture.hpp"

#include "Pixmap.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cstring>

namespace
{
struct M8Header
{
    int32_t id;
    char name[32];
    uint32_t width[16];
    uint32_t height[16];
    uint32_t offsets[16];
    char animname[32];
    uint8_t palette[768];
    int32_t flags;
    int32_t contents;
    int32_t value;
};

static_assert(sizeof(M8Header) == 1040, "Unexpected M8 header size");

std::string ReadNullTerminatedName(const char *value, size_t size)
{
    size_t length = 0;
    while (length < size && value[length] != '\0')
        ++length;
    return std::string(value, length);
}
} // namespace

void M8Image::clear()
{
    name_.clear();
    animationName_.clear();
    palette_.clear();
    levels_.clear();
}

const M8MipLevel *M8Image::mipLevel(int index) const
{
    if (index < 0 || index >= static_cast<int>(levels_.size()))
        return nullptr;
    return &levels_[static_cast<size_t>(index)];
}

bool M8Image::loadFromFile(const std::string &path, std::string *error)
{
    clear();

    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes))
    {
        if (error)
            *error = "failed to read file: " + path;
        return false;
    }

    if (bytes.size() < sizeof(M8Header))
    {
        if (error)
            *error = "file too small to be a valid M8 texture";
        return false;
    }

    M8Header header = {};
    memcpy(&header, bytes.data(), sizeof(M8Header));

    name_ = ReadNullTerminatedName(header.name, sizeof(header.name));
    animationName_ = ReadNullTerminatedName(header.animname, sizeof(header.animname));
    palette_.assign(header.palette, header.palette + sizeof(header.palette));

    for (int mipIndex = 0; mipIndex < 16; ++mipIndex)
    {
        const uint32_t width = header.width[mipIndex];
        const uint32_t height = header.height[mipIndex];
        const uint32_t offset = header.offsets[mipIndex];
        if (width == 0 || height == 0 || offset == 0)
            continue;

        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t end = static_cast<size_t>(offset) + pixelCount;
        if (end > bytes.size())
        {
            if (error)
                *error = "mip level " + std::to_string(mipIndex) + " exceeds file size";
            clear();
            return false;
        }

        M8MipLevel level;
        level.width = static_cast<int>(width);
        level.height = static_cast<int>(height);
        level.indices.assign(bytes.begin() + static_cast<ptrdiff_t>(offset),
                             bytes.begin() + static_cast<ptrdiff_t>(end));
        levels_.push_back(level);
    }

    if (levels_.empty())
    {
        if (error)
            *error = "no valid mip levels found in M8 file";
        return false;
    }

    return true;
}

Pixmap *M8Image::createPixmap(int mipLevelIndex) const
{
    const M8MipLevel *level = mipLevel(mipLevelIndex);
    if (!level || palette_.size() < 768)
        return nullptr;

    Pixmap *pixmap = new Pixmap(level->width, level->height, 3);
    for (int y = 0; y < level->height; ++y)
    {
        for (int x = 0; x < level->width; ++x)
        {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(level->width) + static_cast<size_t>(x);
            const uint8_t paletteIndex = level->indices[index];
            const size_t paletteOffset = static_cast<size_t>(paletteIndex) * 3;
            pixmap->SetPixel(x, y,
                             palette_[paletteOffset + 0],
                             palette_[paletteOffset + 1],
                             palette_[paletteOffset + 2],
                             255);
        }
    }

    return pixmap;
}
