#include "M8Texture.hpp"
#include "Pixmap.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{
std::string ReplaceExtension(const std::string &path, const std::string &extension)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return path + extension;
    return path.substr(0, dot) + extension;
}
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: m8convert <input.m8> [output.png|jpg] [mip]" << std::endl;
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string outputPath = (argc >= 3) ? argv[2] : ReplaceExtension(inputPath, ".png");
    const int mipIndex = (argc >= 4) ? std::atoi(argv[3]) : 0;

    M8Image image;
    std::string error;
    if (!image.loadFromFile(inputPath, &error))
    {
        std::cerr << "failed to load M8: " << error << std::endl;
        return 2;
    }

    std::unique_ptr<Pixmap> pixmap(image.createPixmap(mipIndex));
    if (!pixmap)
    {
        std::cerr << "failed to build pixmap from mip level " << mipIndex << std::endl;
        return 3;
    }

    if (!pixmap->Save(outputPath.c_str()))
    {
        std::cerr << "failed to save output image: " << outputPath << std::endl;
        return 4;
    }

    const M8MipLevel *level = image.mipLevel(mipIndex);
    std::cout << "converted " << inputPath
              << " -> " << outputPath
              << " (" << level->width << "x" << level->height
              << ", mip " << mipIndex << ", name=\"" << image.name() << "\")"
              << std::endl;
    return 0;
}
