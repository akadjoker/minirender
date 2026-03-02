#pragma once
#include "Types.hpp"
#include "glad/glad.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
 


struct Texture
{
    std::string name;
    GLuint id       = 0;
    int width  = 0;
    int height = 0;
    GLenum target = GL_TEXTURE_2D;
    PixelType type = PixelType::RGBA;

    void release()
    {
        if (id) { glDeleteTextures(1, &id); id = 0; }
    }

    ~Texture() { release(); }

    Texture()                          = default;
    Texture(const Texture &)           = delete;
    Texture &operator=(const Texture&) = delete;
    Texture(Texture &&)                = default;
    Texture &operator=(Texture &&)     = default;
};

struct TextureSlot 
{
    Texture*    texture = nullptr;
    std::string uniform; // "u_albedo", "u_normal"
};

class Shader
{
public:
    Shader(const std::string& n) : name(n) {}

    bool load(const std::string& vertPath, const std::string& fragPath);
    bool loadFromSource(const std::string& vertSrc, const std::string& fragSrc);

    void setInt  (const std::string& u, int v)              const;
    void setFloat(const std::string& u, float v)            const;
    void setVec2 (const std::string& u, const glm::vec2& v) const;
    void setVec3 (const std::string& u, const glm::vec3& v) const;
    void setVec4 (const std::string& u, const glm::vec4& v) const;
    void setMat3 (const std::string& u, const glm::mat3& v) const;
    void setMat4 (const std::string& u, const glm::mat4& v) const;
 

    GLuint   getId()      const { return id; }
    uint32_t getAttribs() const { return attribs; }

    ~Shader();

private:
    std::string name;
    uint32_t    attribs = 0;
    GLuint      id      = 0;

    mutable std::unordered_map<std::string, GLint> uniformCache;

    static bool readFile(const std::string& path, std::string& out);
    GLuint      compileSource(const std::string& src, GLenum type) const;
    void        resolveAttribs();
    GLint       getLoc(const std::string& u) const;
};



class Material
{
private:


    std::vector<TextureSlot> textures;
    std::unordered_map<std::string, UniformValue> uniforms;
    Shader *shader = nullptr;
    

public:
    std::string name;
    bool cullFace = true;
    bool blend = false;
    GLenum blendSrc = GL_SRC_ALPHA;
    GLenum blendDst = GL_ONE_MINUS_SRC_ALPHA;
    bool depthTest = true;
    bool depthWrite = true;

    Material *setShader(Shader *s)
    {
        shader = s;
        return this;
    }
    bool hasTexture(const std::string &uniform) const
    {
        for (const auto &slot : textures)
            if (slot.uniform == uniform && slot.texture)
                return true;
        return false;
    }
    Material *setCullFace(bool v)
    {
        cullFace = v;
        return this;
    }
    Material *setBlend(bool v)
    {
        blend = v;
        return this;
    }
    Material *setBlendFunc(GLenum src, GLenum dst)
    {
        blendSrc = src; blendDst = dst;
        return this;
    }
    Material *setDepthTest(bool v)
    {
        depthTest = v;
        return this;
    }
    Material *setDepthWrite(bool v)
    {
        depthWrite = v;
        return this;
    }
    Material *setTexture(const std::string &u, Texture *t);
    Material *setInt(const std::string &u, int v);
    Material *setBool(const std::string &u, bool v) { return setInt(u, v ? 1 : 0); }
    Material *setFloat(const std::string &u, float v);
    Material *setVec2(const std::string &u, glm::vec2 v);
    Material *setVec3(const std::string &u, glm::vec3 v);
    Material *setVec4(const std::string &u, glm::vec4 v);
    Material *setMat3(const std::string &u, glm::mat3 v);
    Material *setMat4(const std::string &u, glm::mat4 v);

    Shader *getShader() const { return shader; }
    const std::vector<TextureSlot> &getTextures() const { return textures; }

    // Get first texture bound to the given uniform slot (nullptr if absent)
    Texture *getTexture(const std::string &uniform) const
    {
        for (const auto &slot : textures)
            if (slot.uniform == uniform)
                return slot.texture;
        return nullptr;
    }

    // Get a vec3 uniform value (returns default if not found)
    glm::vec3 getVec3(const std::string &uniform,
                      glm::vec3 defaultVal = glm::vec3(0.f)) const
    {
        auto it = uniforms.find(uniform);
        if (it != uniforms.end() && it->second.type == UniformType::Vec3)
            return it->second.v3;
        return defaultVal;
    }
    void applyStates() const;
    void bindTextures() const;              // uses material's own shader
    int  bindTexturesTo(Shader *sh) const;  // uses given shader — returns # of binds
    void applyUniforms() const;             // uses material's own shader
    void applyUniformsTo(Shader *sh) const; // uses given shader (for pass override)
    void bind() const;
};
