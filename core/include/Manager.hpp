#pragma once
#include "Material.hpp"
#include "Mesh.hpp"
#include <SDL2/SDL.h>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>


class Pixmap;

// ═════════════════════════════════════════════════════════════════════════════
//  Base — raw pointer cache, manager owns the memory
// ═════════════════════════════════════════════════════════════════════════════
template <typename T>
class ResourceManager
{
public:
    T *get(const std::string &name) const
    {
        auto it = cache.find(name);
        return (it != cache.end()) ? it->second : nullptr;
    }

    bool has(const std::string &name) const
    {
        return cache.count(name) > 0;
    }

    void unload(const std::string &name)
    {
        auto it = cache.find(name);
        if (it != cache.end())
        {
            delete it->second;
            cache.erase(it);
        }
    }

    void unloadAll()
    {
       for (auto &pair : cache)
       {
            auto &ptr  = pair.second;
            delete ptr;
        }
        cache.clear();

    }

    const std::unordered_map<std::string, T *> &all() const { return cache; }

protected:
    std::unordered_map<std::string, T *> cache;
};

// ═════════════════════════════════════════════════════════════════════════════
//  ShaderManager
// ═════════════════════════════════════════════════════════════════════════════
class ShaderManager : public ResourceManager<Shader>
{
public:
    static ShaderManager &instance();

    Shader *load(const std::string &name,
                 const std::string &vertPath,
                 const std::string &fragPath);

    Shader *loadFromSource(const std::string &name,
                           const std::string &vertSrc,
                           const std::string &fragSrc);

    

    bool reload(const std::string &name);
    void reloadAll();

private:
    ShaderManager() = default;

    struct ShaderPaths
    {
        std::string vert, frag;
    };
    std::unordered_map<std::string, ShaderPaths> paths;
};

// ═════════════════════════════════════════════════════════════════════════════
//  TextureManager
// ═════════════════════════════════════════════════════════════════════════════
struct TextureLoadOptions
{
    bool genMipmaps    = true;
    bool sRGB          = false;
    bool flipVertical  = false;
    GLint wrapS        = GL_REPEAT;
    GLint wrapT        = GL_REPEAT;
    GLint filterMin    = GL_LINEAR_MIPMAP_LINEAR;
    GLint filterMag    = GL_LINEAR;
};

class TextureManager : public ResourceManager<Texture>
{
public:
    static TextureManager &instance();

    // ── Default-options setters (fluent, affect all subsequent loads) ──────
    TextureManager &setMipmaps(bool enabled)          { defaultOpts_.genMipmaps   = enabled; return *this; }
    TextureManager &setSRGB(bool enabled)             { defaultOpts_.sRGB         = enabled; return *this; }
    TextureManager &setFlipVertical(bool flip)        { defaultOpts_.flipVertical = flip;    return *this; }
    TextureManager &setFilter(GLint mag, GLint min)   { defaultOpts_.filterMag    = mag;
                                                        defaultOpts_.filterMin    = min;     return *this; }
    TextureManager &setWrap(GLint s, GLint t)         { defaultOpts_.wrapS = s;
                                                        defaultOpts_.wrapT = t;              return *this; }
    void            resetDefaults()                   { defaultOpts_ = {};                  }

    // ── Load functions — use current default options automatically ─────────
    Texture *load(const std::string &name,
                  const std::string &path);

    Texture *loadCubemap(const std::string &name,
                         const std::vector<std::string> &faces);

    Texture *createFromMemory(const std::string &name,
                              int width, int height,
                              PixelType pixelType,
                              const void *data,
                              std::size_t sizeBytes);

    Texture *createFromPixmap(const std::string &name, const Pixmap &pixmap);

    Texture *getWhite();
    Texture *getBlack();
    Texture *getFlatNormal();
    Texture *getPattern();

private:
    TextureManager() = default;

    TextureLoadOptions defaultOpts_;

    Texture *uploadSurface(const std::string &name,
                           SDL_Surface *surf,
                           const TextureLoadOptions &opts);

    Texture *uploadMemory(const std::string &name,
                          int width, int height,
                          PixelType pixelType,
                          const void *data,
                          std::size_t sizeBytes,
                          const TextureLoadOptions &opts);

    Texture *makeSolid(const std::string &name,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

// ═════════════════════════════════════════════════════════════════════════════
//  MaterialManager
// ═════════════════════════════════════════════════════════════════════════════
class MaterialManager : public ResourceManager<Material>
{
public:
    static MaterialManager &instance();

    Material *create(const std::string &name);
    Material *clone(const std::string &srcName, const std::string &newName);

    // Set once before any mesh load; create() auto-assigns the shader,
    // applyDefaults() fills in the fallback texture for untextured materials.
    void setDefaults(Shader *shader, Texture *fallback)
    {
        defaultShader_  = shader;
        fallbackTex_    = fallback;
    }

    // Iterate all materials: assign shader if missing, assign fallback texture
    // if no "u_albedo" slot is present.  Called automatically by MeshManager
    // after loading, but can also be called manually.
    void applyDefaults();

    const Material *activeMaterial() const { return active; }

private:
    MaterialManager() = default;

    Shader  *defaultShader_ = nullptr;
    Texture *fallbackTex_   = nullptr;
    const Material *active  = nullptr;
};

// ============================================================
//  MeshManager
// ============================================================
class MeshManager : public ResourceManager<Mesh>
{
public:
    static MeshManager &instance();

    Mesh *create(const std::string &name);

    // Auto-detects format from extension (.obj / .gltf / .glb)
    // texture_dir: base path for textures; defaults to same folder as mesh file
    Mesh *load(const std::string &name, const std::string &path,
               const std::string &texture_dir = "");

    // Explicit loaders (static geometry only)
    Mesh *load_obj (const std::string &name, const std::string &path,
                    const std::string &texture_dir = "");
    Mesh *load_gltf(const std::string &name, const std::string &path,
                    const std::string &texture_dir = "");
    Mesh *load_h3d (const std::string &name, const std::string &path,
                    const std::string &texture_dir = "");

    Mesh *create_cube(const std::string &name, float size = 1.0f);
    Mesh *create_wire_cube(const std::string &name, float size = 1.0f);
    Mesh *create_plane(const std::string &name, float width = 1.0f, float depth = 1.0f, int subdivs = 1);
    Mesh *create_screen_quad(const std::string &name);
    Mesh *create_sphere(const std::string &name, float radius = 1.0f, int segments = 16);   
    Mesh *create_cylinder(const std::string &name, float radius = 1.0f, float height = 2.0f, int segments = 16);
    Mesh *create_capsule(const std::string &name, float radius = 1.0f, float height = 2.0f, int segments = 16);
    Mesh *create_torus(const std::string &name, float radius = 1.0f, float tubeRadius = 0.3f, int segments = 16, int tubeSegments = 8);
    Mesh *create_quad(const std::string &name, float width = 1.0f, float height = 1.0f);
    Mesh *create_circle(const std::string &name, float radius = 1.0f, int segments = 16);   
    Mesh *create_cone(const std::string &name, float radius = 1.0f, float height = 2.0f, int segments = 16);    
    Mesh *create_arrow(const std::string &name, float shaftRadius = 0.05f, float headRadius = 0.1f, float headLength = 0.2f, int segments = 16);

private:
    MeshManager() = default;
};
