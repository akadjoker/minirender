#pragma once
#include <cstdint>
#include <glm/glm.hpp>



enum class IndexType
{
    None,
    UInt16,
    UInt32
};

enum AttribFlags : uint32_t
{
    ATTRIB_POSITION = (1 << 0),
    ATTRIB_NORMAL = (1 << 1),
    ATTRIB_TANGENT = (1 << 2),
    ATTRIB_COLOR3 = (1 << 3),
    ATTRIB_COLOR4 = (1 << 4),
    ATTRIB_UV0 = (1 << 5),
    ATTRIB_UV1 = (1 << 6),
    ATTRIB_UV2 = (1 << 7),
    ATTRIB_UV3 = (1 << 8),
    ATTRIB_UV4 = (1 << 9),
    ATTRIB_UV5 = (1 << 10),
    ATTRIB_UV6 = (1 << 11),
    ATTRIB_UV7 = (1 << 12),
    ATTRIB_JOINTS = (1 << 13),
    ATTRIB_WEIGHTS = (1 << 14),
};

enum AttribLocation : uint32_t
{
    ATTR_LOC_POSITION = 0,
    ATTR_LOC_NORMAL = 1,
    ATTR_LOC_TANGENT = 2,
    ATTR_LOC_COLOR3 = 3, // not in Vertex struct; reserved
    ATTR_LOC_COLOR4 = 4, // not in Vertex struct; reserved
    ATTR_LOC_UV0 = 3,    // a_uv at location 3 in current VAO layout
    ATTR_LOC_UV1 = 6,
    ATTR_LOC_UV2 = 7,
    ATTR_LOC_UV3 = 8,
    ATTR_LOC_UV4 = 9,
    ATTR_LOC_UV5 = 10,
    ATTR_LOC_UV6 = 11,
    ATTR_LOC_UV7 = 12,
    ATTR_LOC_JOINTS = 13,
    ATTR_LOC_WEIGHTS = 14
};

enum class UniformType
{
    Int,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Mat3,
    Mat4
};

struct UniformValue
{
    UniformType type;
    union
    {
        int i;
        float f;
        glm::vec2 v2;
        glm::vec3 v3;
        glm::vec4 v4;
        glm::mat3 m3;
        glm::mat4 m4;
    };
};

enum class PixelType : std::uint8_t
{
    R = 0,
    RG = 1,
    RGB = 2,
    RGBA = 3,
    RGB24 = 4,
    BGR24 = 5,
    RGBA32 = 6,
    BGRA32 = 7,
    RGB565 = 8,
    RGBA4444 = 9,
    RGBA5551 = 10
};

namespace RenderPassMask
{
    constexpr uint32_t Opaque = 1u << 0;
    constexpr uint32_t Transparent = 1u << 1;
}
