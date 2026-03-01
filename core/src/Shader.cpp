#include "Material.hpp"
#include <SDL2/SDL.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cinttypes>

// ─── File IO ──────────────────────────────────────────────────────────────────

bool Shader::readFile(const std::string &path, std::string &out)
{
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "r");
    if (!rw)
    {
        SDL_Log("[Shader] Cannot open: %s — %s", path.c_str(), SDL_GetError());
        return false;
    }

    Sint64 size = SDL_RWsize(rw);
    if (size <= 0)
    {
        SDL_Log("[Shader] Empty file: %s", path.c_str());
        SDL_RWclose(rw);
        return false;
    }

    out.resize(size);
    Sint64 read = SDL_RWread(rw, out.data(), 1, size);
    SDL_RWclose(rw);

    if (read != size)
    {
        SDL_Log("[Shader] Read error: %s — expected %" PRId64 " got %" PRId64, path.c_str(), size, read);
        return false;
    }

    return true;
}

// ─── Compile ──────────────────────────────────────────────────────────────────

GLuint Shader::compileSource(const std::string &src, GLenum type) const
{
    const char *c = src.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &c, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        SDL_Log("[Shader] Compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// ─── Resolve Attribs ──────────────────────────────────────────────────────────

void Shader::resolveAttribs()
{
    attribs = 0;

    GLint count = 0;
    glGetProgramiv(id, GL_ACTIVE_ATTRIBUTES, &count);

    for (GLint i = 0; i < count; i++)
    {
        char attrName[64];
        GLint size;
        GLenum type;
        glGetActiveAttrib(id, i, sizeof(attrName), nullptr, &size, &type, attrName);

        SDL_Log("[Shader] Found attrib: %s  ", attrName);
   
    }

    attribs = 0;

    count = 0;

    glGetProgramiv(id, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; i++)
    {
        char uniName[64];
        GLint size;
        GLenum type;
        glGetActiveUniform(id, i, sizeof(uniName), nullptr, &size, &type, uniName);

        SDL_Log("[Shader] Found uniform: %s  ", uniName);
    }
}

// ─── Load ─────────────────────────────────────────────────────────────────────

bool Shader::load(const std::string &vertPath, const std::string &fragPath)
{
    std::string vertSrc, fragSrc;
    if (!readFile(vertPath, vertSrc))
        return false;
    if (!readFile(fragPath, fragSrc))
        return false;
    return loadFromSource(vertSrc, fragSrc);
}

bool Shader::loadFromSource(const std::string &vertSrc, const std::string &fragSrc)
{
    if (vertSrc.empty() || fragSrc.empty())
    {
        SDL_Log("[Shader] Empty source — aborting");
        return false;
    }

    GLuint vert = compileSource(vertSrc, GL_VERTEX_SHADER);
    if (!vert)
        return false;

    GLuint frag = compileSource(fragSrc, GL_FRAGMENT_SHADER);
    if (!frag)
    {
        glDeleteShader(vert);
        return false;
    }

    GLuint program = glCreateProgram();
    // for (int i = 0; i < ATTRIB_MAP_SIZE; ++i)
    //     glBindAttribLocation(program, ATTRIB_MAP[i].location, ATTRIB_MAP[i].name);

    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        SDL_Log("[Shader] Link error: %s", log);
        glDeleteProgram(program);
        return false;
    }

    if (id)
    {
        glDeleteProgram(id);
        uniformCache.clear(); // limpa cache do shader anterior
    }

    id = program;
    resolveAttribs();
    return true;
}

GLint Shader::getLoc(const std::string &u) const
{
    auto it = uniformCache.find(u);
    if (it != uniformCache.end())
        return it->second;

    GLint loc = glGetUniformLocation(id, u.c_str());
    if (loc == -1)
        SDL_Log("[Shader] Uniform not found: %s", u.c_str());

    uniformCache[u] = loc;
    return loc;
}

void Shader::setInt(const std::string &u, int v) const { glUniform1i(getLoc(u), v); }
void Shader::setFloat(const std::string &u, float v) const { glUniform1f(getLoc(u), v); }
void Shader::setVec2(const std::string &u, const glm::vec2 &v) const { glUniform2fv(getLoc(u), 1, glm::value_ptr(v)); }
void Shader::setVec3(const std::string &u, const glm::vec3 &v) const { glUniform3fv(getLoc(u), 1, glm::value_ptr(v)); }
void Shader::setVec4(const std::string &u, const glm::vec4 &v) const { glUniform4fv(getLoc(u), 1, glm::value_ptr(v)); }
void Shader::setMat3(const std::string &u, const glm::mat3 &v) const { glUniformMatrix3fv(getLoc(u), 1, GL_FALSE, glm::value_ptr(v)); }
void Shader::setMat4(const std::string &u, const glm::mat4 &v) const { glUniformMatrix4fv(getLoc(u), 1, GL_FALSE, glm::value_ptr(v)); }

void Shader::bindBlock(const std::string &blockName, GLuint bindingPoint) const
{
    GLuint idx = glGetUniformBlockIndex(id, blockName.c_str());
    if (idx != GL_INVALID_INDEX)
        glUniformBlockBinding(id, idx, bindingPoint);
}

Shader::~Shader()
{
    if (id)
        glDeleteProgram(id);
}
