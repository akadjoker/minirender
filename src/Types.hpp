#pragma once
#include <cstdint>
#include <glm/glm.hpp>

typedef uint8_t u8;   // 8-bit  unsigned (0 to 255)
typedef uint16_t u16; // 16-bit unsigned (0 to 65,535)
typedef uint32_t u32; // 32-bit unsigned (0 to 4,294,967,295)
typedef uint64_t u64; // 64-bit unsigned (0 to 18,446,744,073,709,551,615)
typedef int8_t s8;   // 8-bit  signed (-128 to 127)
typedef int16_t s16; // 16-bit signed (-32,768 to 32,767)
typedef int32_t s32; // 32-bit signed (-2,147,483,648 to 2,147,483,647)
typedef int64_t s64; // 64-bit signed

typedef char c8; // Character (8-bit)

typedef float f32;  // 32-bit float (IEEE 754)
typedef double f64; // 64-bit float (IEEE 754)

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
    ATTR_LOC_COLOR3 = 3,   // not in Vertex struct; reserved
    ATTR_LOC_COLOR4 = 4,   // not in Vertex struct; reserved
    ATTR_LOC_UV0    = 3,   // a_uv at location 3 in current VAO layout
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

inline constexpr int attribLocationForFlag(AttribFlags flag)
{
    switch (flag)
    {
    case ATTRIB_POSITION:
        return ATTR_LOC_POSITION;
    case ATTRIB_NORMAL:
        return ATTR_LOC_NORMAL;
    case ATTRIB_TANGENT:
        return ATTR_LOC_TANGENT;
    case ATTRIB_COLOR3:
        return ATTR_LOC_COLOR3;
    case ATTRIB_COLOR4:
        return ATTR_LOC_COLOR4;
    case ATTRIB_UV0:
        return ATTR_LOC_UV0;
    case ATTRIB_UV1:
        return ATTR_LOC_UV1;
    case ATTRIB_UV2:
        return ATTR_LOC_UV2;
    case ATTRIB_UV3:
        return ATTR_LOC_UV3;
    case ATTRIB_UV4:
        return ATTR_LOC_UV4;
    case ATTRIB_UV5:
        return ATTR_LOC_UV5;
    case ATTRIB_UV6:
        return ATTR_LOC_UV6;
    case ATTRIB_UV7:
        return ATTR_LOC_UV7;
    case ATTRIB_JOINTS:
        return ATTR_LOC_JOINTS;
    case ATTRIB_WEIGHTS:
        return ATTR_LOC_WEIGHTS;
    default:
        return -1;
    }
}

inline constexpr AttribFlags attribFlagForUVChannel(uint32_t uvChannel)
{
    switch (uvChannel)
    {
    case 0:
        return ATTRIB_UV0;
    case 1:
        return ATTRIB_UV1;
    case 2:
        return ATTRIB_UV2;
    case 3:
        return ATTRIB_UV3;
    case 4:
        return ATTRIB_UV4;
    case 5:
        return ATTRIB_UV5;
    case 6:
        return ATTRIB_UV6;
    case 7:
        return ATTRIB_UV7;
    default:
        return static_cast<AttribFlags>(0);
    }
}

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
inline constexpr uint32_t Opaque      = 1u << 0;
inline constexpr uint32_t Transparent = 1u << 1;
}
