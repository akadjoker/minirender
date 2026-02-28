// ─────────────────────────────────────────────────────────────────────────────
//  TextureManager.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "Manager.hpp"
#include <SDL2/SDL_image.h>

namespace
{
struct SurfaceUploadSpec
{
    PixelType pixelType = PixelType::RGBA;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    GLenum internalFormat = GL_RGBA8;
    int rowLength = 0;
};

PixelType toChannelClass(PixelType type)
{
    switch (type)
    {
    case PixelType::RGB24:
    case PixelType::BGR24:
    case PixelType::RGB565:
        return PixelType::RGB;
    case PixelType::RGBA32:
    case PixelType::BGRA32:
    case PixelType::RGBA4444:
    case PixelType::RGBA5551:
        return PixelType::RGBA;
    default:
        return type;
    }
}

PixelType inferPixelType(const SDL_Surface *surf)
{
    if (!surf || !surf->format)
        return PixelType::RGBA;

    switch (surf->format->format)
    {
    case SDL_PIXELFORMAT_RGB24:
        return PixelType::RGB24;
    case SDL_PIXELFORMAT_BGR24:
        return PixelType::BGR24;
    case SDL_PIXELFORMAT_RGBA32:
        return PixelType::RGBA32;
    case SDL_PIXELFORMAT_BGRA32:
        return PixelType::BGRA32;
    case SDL_PIXELFORMAT_RGB565:
        return PixelType::RGB565;
    case SDL_PIXELFORMAT_RGBA4444:
        return PixelType::RGBA4444;
    case SDL_PIXELFORMAT_RGBA5551:
        return PixelType::RGBA5551;
    default:
        break;
    }

    const Uint32 fmt = surf->format->format;
    const int bpp = surf->format->BytesPerPixel;
    const bool hasAlpha = SDL_ISPIXELFORMAT_ALPHA(fmt) || (surf->format->Amask != 0);

    if (SDL_ISPIXELFORMAT_INDEXED(fmt))
        return hasAlpha ? PixelType::RGBA : PixelType::RGB;

    int channelCount = 0;
    channelCount += (surf->format->Rmask != 0) ? 1 : 0;
    channelCount += (surf->format->Gmask != 0) ? 1 : 0;
    channelCount += (surf->format->Bmask != 0) ? 1 : 0;
    channelCount += (surf->format->Amask != 0) ? 1 : 0;
    if (channelCount == 4)
        return PixelType::RGBA;
    if (channelCount == 3)
        return PixelType::RGB;
    if (channelCount == 2)
        return hasAlpha ? PixelType::RGBA : PixelType::RG;
    if (channelCount == 1)
        return PixelType::R;

    switch (bpp)
    {
    case 1:
        return PixelType::R;
    case 2:
        return hasAlpha ? PixelType::RGBA : PixelType::RG;
    case 3:
        return PixelType::RGB;
    case 4:
        return PixelType::RGBA;
    default:
        return hasAlpha ? PixelType::RGBA : PixelType::RGB;
    }
}

GLenum defaultInternalFormat(PixelType type, bool sRGB)
{
    const PixelType channelType = toChannelClass(type);
    if (sRGB)
    {
        if (channelType == PixelType::RGBA)
            return GL_SRGB8_ALPHA8;
        if (channelType == PixelType::RGB)
            return GL_SRGB8;
    }

    switch (channelType)
    {
    case PixelType::R:
        return GL_R8;
    case PixelType::RG:
        return GL_RG8;
    case PixelType::RGB:
        return GL_RGB8;
    case PixelType::RGBA:
    default:
        return GL_RGBA8;
    }
}

GLenum pickInternalFormat(const SDL_Surface *surf, PixelType type, bool sRGB)
{
    if (sRGB)
        return defaultInternalFormat(type, true);

    switch (surf->format->format)
    {
    case SDL_PIXELFORMAT_RGB565:
        return GL_RGB565;
    case SDL_PIXELFORMAT_RGBA4444:
        return GL_RGBA4;
    case SDL_PIXELFORMAT_RGBA5551:
        return GL_RGB5_A1;
    default:
        return defaultInternalFormat(type, false);
    }
}

bool mapExternalUploadFormat(const SDL_Surface *surf, GLenum &format, GLenum &type)
{
    switch (surf->format->format)
    {
    case SDL_PIXELFORMAT_RGB24:
        format = GL_RGB;
        type = GL_UNSIGNED_BYTE;
        return true;
    case SDL_PIXELFORMAT_BGR24:
        format = GL_BGR_EXT;
        type = GL_UNSIGNED_BYTE;
        return true;
    case SDL_PIXELFORMAT_RGBA32:
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        return true;
    case SDL_PIXELFORMAT_BGRA32:
        format = GL_BGRA_EXT;
        type = GL_UNSIGNED_BYTE;
        return true;
    case SDL_PIXELFORMAT_RGB565:
        format = GL_RGB;
        type = GL_UNSIGNED_SHORT_5_6_5;
        return true;
    case SDL_PIXELFORMAT_RGBA4444:
        format = GL_RGBA;
        type = GL_UNSIGNED_SHORT_4_4_4_4;
        return true;
    case SDL_PIXELFORMAT_RGBA5551:
        format = GL_RGBA;
        type = GL_UNSIGNED_SHORT_5_5_5_1;
        return true;
    default:
        return false;
    }
}

bool mapMemoryUploadFormat(PixelType pixelType, GLenum &format, GLenum &type, int &bytesPerPixel)
{
    switch (pixelType)
    {
    case PixelType::R:
        format = GL_RED;
        type = GL_UNSIGNED_BYTE;
        bytesPerPixel = 1;
        return true;
    case PixelType::RG:
        format = GL_RG;
        type = GL_UNSIGNED_BYTE;
        bytesPerPixel = 2;
        return true;
    case PixelType::RGB:
    case PixelType::RGB24:
        format = GL_RGB;
        type = GL_UNSIGNED_BYTE;
        bytesPerPixel = 3;
        return true;
    case PixelType::BGR24:
        format = GL_BGR_EXT;
        type = GL_UNSIGNED_BYTE;
        bytesPerPixel = 3;
        return true;
    case PixelType::RGBA:
    case PixelType::RGBA32:
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        bytesPerPixel = 4;
        return true;
    case PixelType::BGRA32:
        format = GL_BGRA_EXT;
        type = GL_UNSIGNED_BYTE;
        bytesPerPixel = 4;
        return true;
    case PixelType::RGB565:
        format = GL_RGB;
        type = GL_UNSIGNED_SHORT_5_6_5;
        bytesPerPixel = 2;
        return true;
    case PixelType::RGBA4444:
        format = GL_RGBA;
        type = GL_UNSIGNED_SHORT_4_4_4_4;
        bytesPerPixel = 2;
        return true;
    case PixelType::RGBA5551:
        format = GL_RGBA;
        type = GL_UNSIGNED_SHORT_5_5_5_1;
        bytesPerPixel = 2;
        return true;
    default:
        return false;
    }
}

bool buildUploadSpec(const SDL_Surface *surf, const TextureLoadOptions &opts, SurfaceUploadSpec &spec)
{
    if (!surf || !surf->format || surf->format->BytesPerPixel == 0)
        return false;

    if (!mapExternalUploadFormat(surf, spec.format, spec.type))
        return false;

    spec.pixelType = inferPixelType(surf);
    spec.internalFormat = pickInternalFormat(surf, spec.pixelType, opts.sRGB);
    spec.rowLength = surf->pitch / surf->format->BytesPerPixel;
    return true;
}
} // namespace

TextureManager &TextureManager::instance()
{
    static TextureManager instance;
    return instance;
}

Texture *TextureManager::uploadSurface(const std::string &name,
                                       SDL_Surface *surf,
                                       const TextureLoadOptions &opts)
{
    SurfaceUploadSpec spec;
    if (!buildUploadSpec(surf, opts, spec))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[TextureManager] Unsupported surface format for '%s': %s",
                     name.c_str(), SDL_GetPixelFormatName(surf ? surf->format->format : SDL_PIXELFORMAT_UNKNOWN));
        SDL_FreeSurface(surf);
        return nullptr;
    }

    Texture *t = new Texture();
    t->width = surf->w;
    t->height = surf->h;
    t->target = GL_TEXTURE_2D;
    t->type = spec.pixelType;

    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);

    GLint prevAlignment = 0;
    GLint prevRowLength = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRowLength);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, spec.rowLength);

    glTexImage2D(GL_TEXTURE_2D, 0, spec.internalFormat,
                 surf->w, surf->h, 0,
                 spec.format, spec.type, surf->pixels);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRowLength);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlignment);

    if (opts.genMipmaps)
        glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, opts.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, opts.wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, opts.filterMin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, opts.filterMag);

    glBindTexture(GL_TEXTURE_2D, 0);
    SDL_FreeSurface(surf);

    cache[name] = t;
    return t;
}

Texture *TextureManager::load(const std::string &name,
                              const std::string &path,
                              const TextureLoadOptions &opts)
{
    if (Texture *existing = get(name))
    {
        SDL_Log("[TextureManager] '%s' already cached", name.c_str());
        return existing;
    }

    SDL_Surface *surf = IMG_Load(path.c_str());
    if (!surf)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[TextureManager] IMG_Load failed '%s' (%s): %s",
                     name.c_str(), path.c_str(), IMG_GetError());
        return nullptr;
    }

    Texture *t = uploadSurface(name, surf, opts);
    if (t)
        SDL_Log("[TextureManager] Loaded '%s' (%dx%d)", name.c_str(), t->width, t->height);

    return t;
}

Texture *TextureManager::createFromMemory(const std::string &name,
                                          int width, int height,
                                          PixelType pixelType,
                                          const void *data,
                                          std::size_t sizeBytes,
                                          const TextureLoadOptions &opts)
{
    if (Texture *existing = get(name))
    {
        SDL_Log("[TextureManager] '%s' already cached", name.c_str());
        return existing;
    }

    if (width <= 0 || height <= 0 || !data)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[TextureManager] createFromMemory invalid args for '%s'",
                     name.c_str());
        return nullptr;
    }

    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    int bytesPerPixel = 0;
    if (!mapMemoryUploadFormat(pixelType, format, type, bytesPerPixel))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[TextureManager] createFromMemory unsupported PixelType (%u) for '%s'",
                     static_cast<unsigned>(pixelType), name.c_str());
        return nullptr;
    }

    const std::size_t expectedBytes = static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) *
                                      static_cast<std::size_t>(bytesPerPixel);
    if (sizeBytes < expectedBytes)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[TextureManager] createFromMemory small buffer for '%s' (%zu < %zu)",
                     name.c_str(), sizeBytes, expectedBytes);
        return nullptr;
    }

    Texture *t = new Texture();
    t->width = width;
    t->height = height;
    t->target = GL_TEXTURE_2D;
    t->type = pixelType;

    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);

    GLint prevAlignment = 0;
    GLint prevRowLength = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRowLength);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    const GLenum internalFmt = (pixelType == PixelType::RGB565) ? GL_RGB565
                              : (pixelType == PixelType::RGBA4444) ? GL_RGBA4
                              : (pixelType == PixelType::RGBA5551) ? GL_RGB5_A1
                                                                    : defaultInternalFormat(pixelType, opts.sRGB);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt,
                 width, height, 0,
                 format, type, data);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRowLength);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlignment);

    if (opts.genMipmaps)
        glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, opts.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, opts.wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, opts.filterMin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, opts.filterMag);

    glBindTexture(GL_TEXTURE_2D, 0);
    t->name = name;    t->name = name;
    cache[name] = t;
    return t;
}

Texture *TextureManager::loadCubemap(const std::string &name,
                                     const std::vector<std::string> &faces,
                                     const TextureLoadOptions &opts)
{
    if (Texture *existing = get(name))
        return existing;

    Texture *t = new Texture();
    t->width = 0;
    t->height = 0;
    t->target = GL_TEXTURE_CUBE_MAP;
    t->type = PixelType::RGBA;

    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, t->id);

    constexpr GLenum targets[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};

    PixelType cubemapType = PixelType::RGBA;
    GLenum cubemapInternalFormat = GL_RGBA8;
    for (int i = 0; i < 6; ++i)
    {
        SDL_Surface *surf = IMG_Load(faces[i].c_str());
        if (!surf)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[TextureManager] Cubemap face %d failed (%s): %s",
                         i, faces[i].c_str(), IMG_GetError());
            glDeleteTextures(1, &t->id);
            delete t;
            return nullptr;
        }

        SurfaceUploadSpec faceSpec;
        if (!buildUploadSpec(surf, opts, faceSpec))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[TextureManager] Cubemap face %d unsupported format (%s): %s",
                         i, faces[i].c_str(), SDL_GetPixelFormatName(surf->format->format));
            SDL_FreeSurface(surf);
            glDeleteTextures(1, &t->id);
            delete t;
            return nullptr;
        }

        const PixelType faceType = faceSpec.pixelType;
        if (i == 0)
        {
            cubemapType = faceType;
            cubemapInternalFormat = faceSpec.internalFormat;
            t->type = cubemapType;
        }
        else if (toChannelClass(faceType) != toChannelClass(cubemapType))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[TextureManager] Cubemap '%s' face %d channel mismatch (%s)",
                         name.c_str(), i, SDL_GetPixelFormatName(surf->format->format));
            SDL_FreeSurface(surf);
            glDeleteTextures(1, &t->id);
            delete t;
            return nullptr;
        }

        if (i > 0 && (surf->w != t->width || surf->h != t->height))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[TextureManager] Cubemap '%s' face %d size mismatch (%dx%d != %dx%d)",
                         name.c_str(), i, surf->w, surf->h, t->width, t->height);
            SDL_FreeSurface(surf);
            glDeleteTextures(1, &t->id);
            delete t;
            return nullptr;
        }

        GLint prevAlignment = 0;
        GLint prevRowLength = 0;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRowLength);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, faceSpec.rowLength);

        glTexImage2D(targets[i], 0, cubemapInternalFormat,
                     surf->w, surf->h, 0,
                     faceSpec.format, faceSpec.type, surf->pixels);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRowLength);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlignment);

        if (i == 0)
        {
            t->width = surf->w;
            t->height = surf->h;
        }
        SDL_FreeSurface(surf);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, opts.filterMag);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, opts.filterMag);

    if (opts.genMipmaps)
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    t->name = name;
    cache[name] = t;
    SDL_Log("[TextureManager] Loaded cubemap '%s' (%dx%d)", name.c_str(), t->width, t->height);
    return t;
}

Texture *TextureManager::makeSolid(const std::string &name,
                                   uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint8_t pixels[4] = {r, g, b, a};
    TextureLoadOptions opts{};
    opts.genMipmaps = false;
    opts.filterMin = GL_NEAREST;
    opts.filterMag = GL_NEAREST;
    return createFromMemory(name, 1, 1, PixelType::RGBA, pixels, sizeof(pixels), opts);
}

Texture *TextureManager::getWhite() { return get("__white") ? get("__white") : makeSolid("__white", 255, 255, 255, 255); }
Texture *TextureManager::getBlack() { return get("__black") ? get("__black") : makeSolid("__black", 0, 0, 0, 255); }
Texture *TextureManager::getFlatNormal() { return get("__flat_normal") ? get("__flat_normal") : makeSolid("__flat_normal", 128, 128, 255, 255); }

Texture *TextureManager::getPattern()
{
    if (Texture *existing = get("__pattern"))
        return existing;

    const int size = 64 * 64 * 4;
    std::vector<uint8_t> pixels(size);
    for (int i = 0; i < size; i += 4)
    {
        int x = (i / 4) % 64;
        int y = (i / 4) / 64;
        bool checker = ((x / 8) % 2) ^ ((y / 8) % 2);
        pixels[i + 0] = checker ? 255 : 0; // R
        pixels[i + 1] = checker ? 255 : 0; // G
        pixels[i + 2] = checker ? 255 : 0; // B
        pixels[i + 3] = 255;               // A
    }
    TextureLoadOptions opts{};
    opts.genMipmaps = false;
    opts.filterMin = GL_NEAREST;
    opts.filterMag = GL_NEAREST;
    return createFromMemory("__pattern", 64, 64, PixelType::RGBA, pixels.data(), pixels.size(), opts);
}
