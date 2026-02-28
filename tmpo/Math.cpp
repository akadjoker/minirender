#include "pch.h"
#include "Math.hpp"

// Constantes
constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;

// ==================== Vec2 ====================
Vec2::Vec2() : x(0.0f), y(0.0f) {}

Vec2::Vec2(float x, float y) : x(x), y(y) {}

Vec2::Vec2(float scalar) : x(scalar), y(scalar) {}

// Acesso por índice
float &Vec2::operator[](int index)
{
    SDL_assert(index >= 0 && index < 2 && "Vec2 index out of range");
    return (&x)[index];
}

const float &Vec2::operator[](int index) const
{
    SDL_assert(index >= 0 && index < 2 && "Vec2 index out of range");
    return (&x)[index];
}

// Operadores aritméticos
Vec2 Vec2::operator+(const Vec2 &other) const
{
    return Vec2(x + other.x, y + other.y);
}

Vec2 Vec2::operator-(const Vec2 &other) const
{
    return Vec2(x - other.x, y - other.y);
}

Vec2 Vec2::operator*(const Vec2 &other) const
{
    return Vec2(x * other.x, y * other.y);
}

Vec2 Vec2::operator/(const Vec2 &other) const
{
    return Vec2(x / other.x, y / other.y);
}

Vec2 Vec2::operator*(float scalar) const
{
    return Vec2(x * scalar, y * scalar);
}

Vec2 Vec2::operator/(float scalar) const
{
    return Vec2(x / scalar, y / scalar);
}

// Operadores compostos
Vec2 &Vec2::operator+=(const Vec2 &other)
{
    x += other.x;
    y += other.y;
    return *this;
}

Vec2 &Vec2::operator-=(const Vec2 &other)
{
    x -= other.x;
    y -= other.y;
    return *this;
}

Vec2 &Vec2::operator*=(const Vec2 &other)
{
    x *= other.x;
    y *= other.y;
    return *this;
}

Vec2 &Vec2::operator/=(const Vec2 &other)
{
    x /= other.x;
    y /= other.y;
    return *this;
}

Vec2 &Vec2::operator*=(float scalar)
{
    x *= scalar;
    y *= scalar;
    return *this;
}

Vec2 &Vec2::operator/=(float scalar)
{
    x /= scalar;
    y /= scalar;
    return *this;
}

// Operador unário
Vec2 Vec2::operator-() const
{
    return Vec2(-x, -y);
}

// Comparação
bool Vec2::operator==(const Vec2 &other) const
{
    const float epsilon = 1e-6f;
    return std::fabs(x - other.x) < epsilon &&
           std::fabs(y - other.y) < epsilon;
}

bool Vec2::operator!=(const Vec2 &other) const
{
    return !(*this == other);
}

// Métodos úteis
float Vec2::lengthSquared() const
{
    return x * x + y * y;
}

float Vec2::length() const
{
    return std::sqrt(lengthSquared());
}

Vec2 Vec2::normalized() const
{
    float len = length();
    if (len > 0.0f)
    {
        return *this / len;
    }
    return Vec2(0.0f, 0.0f);
}

void Vec2::normalize()
{
    float len = length();
    if (len > 0.0f)
    {
        x /= len;
        y /= len;
    }
}

float Vec2::dot(const Vec2 &other) const
{
    return x * other.x + y * other.y;
}

float Vec2::cross(const Vec2 &other) const
{
    return x * other.y - y * other.x;
}

// Funções de ângulo
float Vec2::angle() const
{
    return std::atan2(y, x);
}

float Vec2::angleDeg() const
{
    return angle() * RAD_TO_DEG;
}

float Vec2::AngleBetween(const Vec2 &a, const Vec2 &b)
{
    float dot = Dot(a, b);
    float lenProduct = a.length() * b.length();
    if (lenProduct > 0.0f)
    {
        return std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot / lenProduct)));
    }
    return 0.0f;
}

float Vec2::AngleBetweenDeg(const Vec2 &a, const Vec2 &b)
{
    return AngleBetween(a, b) * RAD_TO_DEG;
}

Vec2 Vec2::rotate(float angleRad) const
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);
    return Vec2(x * c - y * s, x * s + y * c);
}

Vec2 Vec2::rotateDeg(float angleDeg) const
{
    return rotate(angleDeg * DEG_TO_RAD);
}

Vec2 Vec2::FromAngle(float angleRad)
{
    return Vec2(std::cos(angleRad), std::sin(angleRad));
}

Vec2 Vec2::FromAngleDeg(float angleDeg)
{
    return FromAngle(angleDeg * DEG_TO_RAD);
}

float Vec2::Dot(const Vec2 &a, const Vec2 &b)
{
    return a.dot(b);
}

float Vec2::Distance(const Vec2 &a, const Vec2 &b)
{
    return (b - a).length();
}

float Vec2::DistanceSquared(const Vec2 &a, const Vec2 &b)
{
    return (b - a).lengthSquared();
}

Vec2 Vec2::Lerp(const Vec2 &a, const Vec2 &b, float t)
{
    return a + (b - a) * t;
}

Vec2 Vec2::Min(const Vec2 &a, const Vec2 &b)
{
    return Vec2(std::fmin(a.x, b.x), std::fmin(a.y, b.y));
}

Vec2 Vec2::Max(const Vec2 &a, const Vec2 &b)
{
    return Vec2(std::fmax(a.x, b.x), std::fmax(a.y, b.y));
}

// Scalar * Vec2
Vec2 operator*(float scalar, const Vec2 &vec)
{
    return vec * scalar;
}

// ==================== Vec3 ====================

Vec3 Vec3::Zero = Vec3(0.0f, 0.0f, 0.0f);
Vec3 Vec3::One = Vec3(1.0f, 1.0f, 1.0f);

Vec3::Vec3() : x(0.0f), y(0.0f), z(0.0f) {}

Vec3::Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

Vec3::Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}

Vec3::Vec3(const Vec2 &xy, float z) : x(xy.x), y(xy.y), z(z) {}

float &Vec3::operator[](int index)
{
    SDL_assert(index >= 0 && index < 3 && "Vec3 index out of range");
    return (&x)[index];
}

const float &Vec3::operator[](int index) const
{
    SDL_assert(index >= 0 && index < 3 && "Vec3 index out of range");
    return (&x)[index];
}

Vec3 Vec3::operator+(const Vec3 &other) const
{
    return Vec3(x + other.x, y + other.y, z + other.z);
}

Vec3 Vec3::operator-(const Vec3 &other) const
{
    return Vec3(x - other.x, y - other.y, z - other.z);
}

Vec3 Vec3::operator*(const Vec3 &other) const
{
    return Vec3(x * other.x, y * other.y, z * other.z);
}

Vec3 Vec3::operator/(const Vec3 &other) const
{
    return Vec3(x / other.x, y / other.y, z / other.z);
}

Vec3 Vec3::operator*(float scalar) const
{
    return Vec3(x * scalar, y * scalar, z * scalar);
}

Vec3 Vec3::operator/(float scalar) const
{
    return Vec3(x / scalar, y / scalar, z / scalar);
}

Vec3 &Vec3::operator+=(const Vec3 &other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
}

Vec3 &Vec3::operator-=(const Vec3 &other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
}

Vec3 &Vec3::operator*=(const Vec3 &other)
{
    x *= other.x;
    y *= other.y;
    z *= other.z;
    return *this;
}

Vec3 &Vec3::operator/=(const Vec3 &other)
{
    x /= other.x;
    y /= other.y;
    z /= other.z;
    return *this;
}

Vec3 &Vec3::operator*=(float scalar)
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
}

Vec3 &Vec3::operator/=(float scalar)
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
}

Vec3 Vec3::operator-() const
{
    return Vec3(-x, -y, -z);
}

bool Vec3::operator==(const Vec3 &other) const
{
    const float epsilon = 1e-6f;
    return std::fabs(x - other.x) < epsilon &&
           std::fabs(y - other.y) < epsilon &&
           std::fabs(z - other.z) < epsilon;
}

bool Vec3::operator!=(const Vec3 &other) const
{
    return !(*this == other);
}

float Vec3::lengthSquared() const
{
    return x * x + y * y + z * z;
}

float Vec3::length() const
{
    return std::sqrt(lengthSquared());
}

Vec3 Vec3::normalized() const
{
    float len = length();
    if (len > 0.0f)
    {
        return *this / len;
    }
    return Vec3(0.0f, 0.0f, 0.0f);
}

void Vec3::normalize()
{
    float len = length();
    if (len > 0.0f)
    {
        x /= len;
        y /= len;
        z /= len;
    }
}

void Vec3::inverse()
{
    x = -x;
    y = -y;
    z = -z;
}

float Vec3::dot(const Vec3 &other) const
{
    return x * other.x + y * other.y + z * other.z;
}

Vec3 Vec3::cross(const Vec3 &other) const
{
    return Vec3(
        y * other.z - z * other.y,
        z * other.x - x * other.z,
        x * other.y - y * other.x);
}

float Vec3::AngleBetween(const Vec3 &a, const Vec3 &b)
{
    float dot = Dot(a, b);
    float lenProduct = a.length() * b.length();
    if (lenProduct > 0.0f)
    {
        return std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot / lenProduct)));
    }
    return 0.0f;
}

float Vec3::AngleBetweenDeg(const Vec3 &a, const Vec3 &b)
{
    return AngleBetween(a, b) * RAD_TO_DEG;
}

Vec3 Vec3::rotateX(float angleRad) const
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);
    return Vec3(x, y * c - z * s, y * s + z * c);
}

Vec3 Vec3::rotateY(float angleRad) const
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);
    return Vec3(x * c + z * s, y, -x * s + z * c);
}

Vec3 Vec3::rotateZ(float angleRad) const
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);
    return Vec3(x * c - y * s, x * s + y * c, z);
}

Vec3 Vec3::rotateXDeg(float angleDeg) const
{
    return rotateX(angleDeg * DEG_TO_RAD);
}

Vec3 Vec3::rotateYDeg(float angleDeg) const
{
    return rotateY(angleDeg * DEG_TO_RAD);
}

Vec3 Vec3::rotateZDeg(float angleDeg) const
{
    return rotateZ(angleDeg * DEG_TO_RAD);
}

Vec3 Vec3::Normalize(const Vec3 &a)
{
    return a.normalized();
}

float Vec3::Dot(const Vec3 &a, const Vec3 &b)
{
    return a.dot(b);
}

Vec3 Vec3::Cross(const Vec3 &a, const Vec3 &b)
{
    return a.cross(b);
}

float Vec3::Distance(const Vec3 &a, const Vec3 &b)
{
    return (b - a).length();
}

float Vec3::DistanceSquared(const Vec3 &a, const Vec3 &b)
{
    return (b - a).lengthSquared();
}

Vec3 Vec3::Lerp(const Vec3 &a, const Vec3 &b, float t)
{
    return a + (b - a) * t;
}

Vec3 Vec3::Min(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z));
}

Vec3 Vec3::Max(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z));
}

// Scalar * Vec3
Vec3 operator*(float scalar, const Vec3 &vec)
{
    return vec * scalar;
}

// ==================== Vec4 ====================

Vec4::Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}

Vec4::Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

Vec4::Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}

Vec4::Vec4(const Vec2 &xy, float z, float w) : x(xy.x), y(xy.y), z(z), w(w) {}

Vec4::Vec4(const Vec3 &xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

// Acesso por índice
float &Vec4::operator[](int index)
{
    SDL_assert(index >= 0 && index < 4);
    if (index == 0)
        return x;
    if (index == 1)
        return y;
    if (index == 2)
        return z;
    if (index == 3)
        return w;
    SDL_assert(false && "Vec4 index out of range");
    return x;
}

const float &Vec4::operator[](int index) const
{
    SDL_assert(index >= 0 && index < 4);
    if (index == 0)
        return x;
    if (index == 1)
        return y;
    if (index == 2)
        return z;
    if (index == 3)
        return w;

    SDL_assert(false && "Vec4 index out of range");
    return x;
}

// Operadores aritméticos
Vec4 Vec4::operator+(const Vec4 &other) const
{
    return Vec4(x + other.x, y + other.y, z + other.z, w + other.w);
}

Vec4 Vec4::operator-(const Vec4 &other) const
{
    return Vec4(x - other.x, y - other.y, z - other.z, w - other.w);
}

Vec4 Vec4::operator*(const Vec4 &other) const
{
    return Vec4(x * other.x, y * other.y, z * other.z, w * other.w);
}

Vec4 Vec4::operator/(const Vec4 &other) const
{
    return Vec4(x / other.x, y / other.y, z / other.z, w / other.w);
}

Vec4 Vec4::operator*(float scalar) const
{
    return Vec4(x * scalar, y * scalar, z * scalar, w * scalar);
}

Vec4 Vec4::operator/(float scalar) const
{
    return Vec4(x / scalar, y / scalar, z / scalar, w / scalar);
}

Vec4 &Vec4::operator+=(const Vec4 &other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
}

Vec4 &Vec4::operator-=(const Vec4 &other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
}

Vec4 &Vec4::operator*=(const Vec4 &other)
{
    x *= other.x;
    y *= other.y;
    z *= other.z;
    w *= other.w;
    return *this;
}

Vec4 &Vec4::operator/=(const Vec4 &other)
{
    x /= other.x;
    y /= other.y;
    z /= other.z;
    w /= other.w;
    return *this;
}

Vec4 &Vec4::operator*=(float scalar)
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

Vec4 &Vec4::operator/=(float scalar)
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

// Operador unário
Vec4 Vec4::operator-() const
{
    return Vec4(-x, -y, -z, -w);
}

// Comparação
bool Vec4::operator==(const Vec4 &other) const
{
    const float epsilon = 1e-6f;
    return std::fabs(x - other.x) < epsilon &&
           std::fabs(y - other.y) < epsilon &&
           std::fabs(z - other.z) < epsilon &&
           std::fabs(w - other.w) < epsilon;
}

bool Vec4::operator!=(const Vec4 &other) const
{
    return !(*this == other);
}

// Métodos úteis
float Vec4::lengthSquared() const
{
    return x * x + y * y + z * z + w * w;
}

float Vec4::length() const
{
    return std::sqrt(lengthSquared());
}

Vec4 Vec4::normalized() const
{
    float len = length();
    if (len > 0.0f)
    {
        return *this / len;
    }
    return Vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

void Vec4::normalize()
{
    float len = length();
    if (len > 0.0f)
    {
        x /= len;
        y /= len;
        z /= len;
        w /= len;
    }
}

float Vec4::dot(const Vec4 &other) const
{
    return x * other.x + y * other.y + z * other.z + w * other.w;
}

// Funções estáticas
float Vec4::Dot(const Vec4 &a, const Vec4 &b)
{
    return a.dot(b);
}

float Vec4::Distance(const Vec4 &a, const Vec4 &b)
{
    return (b - a).length();
}

float Vec4::DistanceSquared(const Vec4 &a, const Vec4 &b)
{
    return (b - a).lengthSquared();
}

Vec4 Vec4::Lerp(const Vec4 &a, const Vec4 &b, float t)
{
    return a + (b - a) * t;
}

Vec4 Vec4::Min(const Vec4 &a, const Vec4 &b)
{
    return Vec4(std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z), std::fmin(a.w, b.w));
}

Vec4 Vec4::Max(const Vec4 &a, const Vec4 &b)
{
    return Vec4(std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z), std::fmax(a.w, b.w));
}

// Scalar * Vec4
Vec4 operator*(float scalar, const Vec4 &vec)
{
    return vec * scalar;
}

// ==================== Mat3 ====================

// Construtores
Mat3::Mat3()
{
    // Identidade
    m[0] = 1.0f;
    m[3] = 0.0f;
    m[6] = 0.0f;
    m[1] = 0.0f;
    m[4] = 1.0f;
    m[7] = 0.0f;
    m[2] = 0.0f;
    m[5] = 0.0f;
    m[8] = 1.0f;
}

Mat3::Mat3(float diagonal)
{
    m[0] = diagonal;
    m[3] = 0.0f;
    m[6] = 0.0f;
    m[1] = 0.0f;
    m[4] = diagonal;
    m[7] = 0.0f;
    m[2] = 0.0f;
    m[5] = 0.0f;
    m[8] = diagonal;
}

Mat3::Mat3(float m0, float m1, float m2,
           float m3, float m4, float m5,
           float m6, float m7, float m8)
{
    // Row-major input, armazena em column-major
    m[0] = m0;
    m[3] = m1;
    m[6] = m2;
    m[1] = m3;
    m[4] = m4;
    m[7] = m5;
    m[2] = m6;
    m[5] = m7;
    m[8] = m8;
}

// Acesso por índice
float &Mat3::operator[](int index)
{
    return m[index];
}

const float &Mat3::operator[](int index) const
{
    return m[index];
}

float &Mat3::operator()(int row, int col)
{
    return m[col * 3 + row]; // Column-major
}

const float &Mat3::operator()(int row, int col) const
{
    return m[col * 3 + row];
}

// Operadores aritméticos
Mat3 Mat3::operator+(const Mat3 &other) const
{
    Mat3 result;
    for (int i = 0; i < 9; i++)
    {
        result.m[i] = m[i] + other.m[i];
    }
    return result;
}

Mat3 Mat3::operator-(const Mat3 &other) const
{
    Mat3 result;
    for (int i = 0; i < 9; i++)
    {
        result.m[i] = m[i] - other.m[i];
    }
    return result;
}

Mat3 Mat3::operator*(const Mat3 &other) const
{
    Mat3 result(0.0f);
    for (int col = 0; col < 3; col++)
    {
        for (int row = 0; row < 3; row++)
        {
            float sum = 0.0f;
            for (int k = 0; k < 3; k++)
            {
                sum += (*this)(row, k) * other(k, col);
            }
            result(row, col) = sum;
        }
    }
    return result;
}

Mat3 Mat3::operator*(float scalar) const
{
    Mat3 result;
    for (int i = 0; i < 9; i++)
    {
        result.m[i] = m[i] * scalar;
    }
    return result;
}

Mat3 Mat3::operator/(float scalar) const
{
    Mat3 result;
    for (int i = 0; i < 9; i++)
    {
        result.m[i] = m[i] / scalar;
    }
    return result;
}

// Transformação de vetores
Vec3 Mat3::operator*(const Vec3 &vec) const
{
    return Vec3(
        m[0] * vec.x + m[3] * vec.y + m[6] * vec.z,
        m[1] * vec.x + m[4] * vec.y + m[7] * vec.z,
        m[2] * vec.x + m[5] * vec.y + m[8] * vec.z);
}

Vec2 Mat3::operator*(const Vec2 &vec) const
{
    // Trata Vec2 como (x, y, 1) em coordenadas homogêneas
    Vec3 result = (*this) * Vec3(vec.x, vec.y, 1.0f);
    return Vec2(result.x, result.y);
}

// Operadores compostos
Mat3 &Mat3::operator+=(const Mat3 &other)
{
    for (int i = 0; i < 9; i++)
    {
        m[i] += other.m[i];
    }
    return *this;
}

Mat3 &Mat3::operator-=(const Mat3 &other)
{
    for (int i = 0; i < 9; i++)
    {
        m[i] -= other.m[i];
    }
    return *this;
}

Mat3 &Mat3::operator*=(const Mat3 &other)
{
    *this = (*this) * other;
    return *this;
}

Mat3 &Mat3::operator*=(float scalar)
{
    for (int i = 0; i < 9; i++)
    {
        m[i] *= scalar;
    }
    return *this;
}

Mat3 &Mat3::operator/=(float scalar)
{
    for (int i = 0; i < 9; i++)
    {
        m[i] /= scalar;
    }
    return *this;
}

// Comparação
bool Mat3::operator==(const Mat3 &other) const
{
    const float epsilon = 1e-6f;
    for (int i = 0; i < 9; i++)
    {
        if (std::fabs(m[i] - other.m[i]) >= epsilon)
        {
            return false;
        }
    }
    return true;
}

bool Mat3::operator!=(const Mat3 &other) const
{
    return !(*this == other);
}

// Operações de matriz
Mat3 Mat3::transposed() const
{
    Mat3 result;
    for (int row = 0; row < 3; row++)
    {
        for (int col = 0; col < 3; col++)
        {
            result(row, col) = (*this)(col, row);
        }
    }
    return result;
}

void Mat3::transpose()
{
    *this = transposed();
}

float Mat3::determinant() const
{
    return m[0] * (m[4] * m[8] - m[5] * m[7]) -
           m[3] * (m[1] * m[8] - m[2] * m[7]) +
           m[6] * (m[1] * m[5] - m[2] * m[4]);
}

Mat3 Mat3::inverse() const
{
    float det = determinant();
    if (std::fabs(det) < 1e-6f)
    {
        return Mat3(); // Retorna identidade se não inversível
    }

    Mat3 inv;
    float invDet = 1.0f / det;

    inv.m[0] = (m[4] * m[8] - m[5] * m[7]) * invDet;
    inv.m[1] = (m[2] * m[7] - m[1] * m[8]) * invDet;
    inv.m[2] = (m[1] * m[5] - m[2] * m[4]) * invDet;

    inv.m[3] = (m[5] * m[6] - m[3] * m[8]) * invDet;
    inv.m[4] = (m[0] * m[8] - m[2] * m[6]) * invDet;
    inv.m[5] = (m[2] * m[3] - m[0] * m[5]) * invDet;

    inv.m[6] = (m[3] * m[7] - m[4] * m[6]) * invDet;
    inv.m[7] = (m[1] * m[6] - m[0] * m[7]) * invDet;
    inv.m[8] = (m[0] * m[4] - m[1] * m[3]) * invDet;

    return inv;
}

Mat3 Mat3::Identity()
{
    return Mat3();
}
Mat3 Mat3::FromAxes(const Vec3& right, const Vec3& up, const Vec3& forward)
{
    Mat3 result;

    // Column-major:
    // coluna 0 = right
    result.m[0] = right.x;
    result.m[1] = right.y;
    result.m[2] = right.z;

    // coluna 1 = up
    result.m[3] = up.x;
    result.m[4] = up.y;
    result.m[5] = up.z;

    // coluna 2 = forward
    result.m[6] = forward.x;
    result.m[7] = forward.y;
    result.m[8] = forward.z;

    return result;
}

Mat3 Mat3::LookAtDirection(const Vec3& forwardDir, const Vec3& upHint)
{
    Vec3 f = forwardDir.normalized();
    Vec3 u = upHint.normalized();

    // Evitar up quase paralelo ao forward
    if (fabs(Vec3::Dot(f, u)) > 0.999f)
    {
 
        u = Vec3(0, 0, 1);
    }
 
    Vec3 right = Vec3::Cross(f, u).normalized();
    Vec3 newUp = Vec3::Cross(right, f).normalized();

 
    return FromAxes(right, newUp, -f);
}


Mat3 Mat3::Scale(float sx, float sy, float sz)
{
    Mat3 result(0.0f);
    result.m[0] = sx;
    result.m[4] = sy;
    result.m[8] = sz;
    return result;
}

Mat3 Mat3::Scale(const Vec3 &scale)
{
    return Scale(scale.x, scale.y, scale.z);
}

Mat3 Mat3::RotationX(float angle)
{
    float c = std::cos(angle);
    float s = std::sin(angle);
    Mat3 result;
    result.m[0] = 1.0f;
    result.m[3] = 0.0f;
    result.m[6] = 0.0f;
    result.m[1] = 0.0f;
    result.m[4] = c;
    result.m[7] = -s;
    result.m[2] = 0.0f;
    result.m[5] = s;
    result.m[8] = c;
    return result;
}

Mat3 Mat3::RotationY(float angle)
{
    float c = std::cos(angle);
    float s = std::sin(angle);
    Mat3 result;
    result.m[0] = c;
    result.m[3] = 0.0f;
    result.m[6] = s;
    result.m[1] = 0.0f;
    result.m[4] = 1.0f;
    result.m[7] = 0.0f;
    result.m[2] = -s;
    result.m[5] = 0.0f;
    result.m[8] = c;
    return result;
}

Mat3 Mat3::RotationZ(float angle)
{
    float c = std::cos(angle);
    float s = std::sin(angle);
    Mat3 result;
    result.m[0] = c;
    result.m[3] = -s;
    result.m[6] = 0.0f;
    result.m[1] = s;
    result.m[4] = c;
    result.m[7] = 0.0f;
    result.m[2] = 0.0f;
    result.m[5] = 0.0f;
    result.m[8] = 1.0f;
    return result;
}

Mat3 Mat3::Rotation(float angle, const Vec3 &axis)
{
    Vec3 a = axis.normalized();
    float c = std::cos(angle);
    float s = std::sin(angle);
    float t = 1.0f - c;

    Mat3 result;
    result.m[0] = t * a.x * a.x + c;
    result.m[1] = t * a.x * a.y + s * a.z;
    result.m[2] = t * a.x * a.z - s * a.y;

    result.m[3] = t * a.x * a.y - s * a.z;
    result.m[4] = t * a.y * a.y + c;
    result.m[5] = t * a.y * a.z + s * a.x;

    result.m[6] = t * a.x * a.z + s * a.y;
    result.m[7] = t * a.y * a.z - s * a.x;
    result.m[8] = t * a.z * a.z + c;

    return result;
}

// Scalar * Mat3
Mat3 operator*(float scalar, const Mat3 &mat)
{
    return mat * scalar;
}

// Output
// std::ostream &operator<<(std::ostream &os, const Mat3 &mat)
// {
//     os << "Mat3(\n";
//     for (int row = 0; row < 3; row++)
//     {
//         os << "  ";
//         for (int col = 0; col < 3; col++)
//         {
//             os << mat(row, col);
//             if (col < 2)
//                 os << ", ";
//         }
//         os << "\n";
//     }
//     os << ")";
//     return os;
// }

// ==================== Mat4 ====================

Mat4::Mat4()
{
    // Identidade
    for (int i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

Mat4::Mat4(float diagonal)
{
    for (int i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = diagonal;
}

Mat4::Mat4(float m0, float m1, float m2, float m3,
           float m4, float m5, float m6, float m7,
           float m8, float m9, float m10, float m11,
           float m12, float m13, float m14, float m15)
{
    // Row-major input, armazena em column-major
    m[0] = m0;
    m[4] = m1;
    m[8] = m2;
    m[12] = m3;
    m[1] = m4;
    m[5] = m5;
    m[9] = m6;
    m[13] = m7;
    m[2] = m8;
    m[6] = m9;
    m[10] = m10;
    m[14] = m11;
    m[3] = m12;
    m[7] = m13;
    m[11] = m14;
    m[15] = m15;
}

Mat4::Mat4(const Mat3 &rotation, const Vec3 &translation)
{
    m[0] = rotation.m[0];
    m[4] = rotation.m[3];
    m[8] = rotation.m[6];
    m[12] = translation.x;
    m[1] = rotation.m[1];
    m[5] = rotation.m[4];
    m[9] = rotation.m[7];
    m[13] = translation.y;
    m[2] = rotation.m[2];
    m[6] = rotation.m[5];
    m[10] = rotation.m[8];
    m[14] = translation.z;
    m[3] = 0.0f;
    m[7] = 0.0f;
    m[11] = 0.0f;
    m[15] = 1.0f;
}

void Mat4::set(const Mat3 &rotation, const Vec3 &translation)
{
     m[0] = rotation.m[0];
    m[4] = rotation.m[3];
    m[8] = rotation.m[6];
    m[12] = translation.x;
    m[1] = rotation.m[1];
    m[5] = rotation.m[4];
    m[9] = rotation.m[7];
    m[13] = translation.y;
    m[2] = rotation.m[2];
    m[6] = rotation.m[5];
    m[10] = rotation.m[8];
    m[14] = translation.z;
    m[3] = 0.0f;
    m[7] = 0.0f;
    m[11] = 0.0f;
    m[15] = 1.0f;
}

// Acesso por índice
float &Mat4::operator[](int index)
{
    return m[index];
}

const float &Mat4::operator[](int index) const
{
    return m[index];
}

float &Mat4::operator()(int row, int col)
{
    return m[col * 4 + row];
}

const float &Mat4::operator()(int row, int col) const
{
    return m[col * 4 + row];
}

// Operadores aritméticos
Mat4 Mat4::operator+(const Mat4 &other) const
{
    Mat4 result;
    for (int i = 0; i < 16; i++)
    {
        result.m[i] = m[i] + other.m[i];
    }
    return result;
}

Mat4 Mat4::operator-(const Mat4 &other) const
{
    Mat4 result;
    for (int i = 0; i < 16; i++)
    {
        result.m[i] = m[i] - other.m[i];
    }
    return result;
}

Mat4 Mat4::operator*(const Mat4 &other) const
{
    Mat4 result(0.0f);
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
            {
                sum += (*this)(row, k) * other(k, col);
            }
            result(row, col) = sum;
        }
    }
    return result;
}

Mat4 Mat4::operator*(float scalar) const
{
    Mat4 result;
    for (int i = 0; i < 16; i++)
    {
        result.m[i] = m[i] * scalar;
    }
    return result;
}

Mat4 Mat4::operator/(float scalar) const
{
    Mat4 result;
    for (int i = 0; i < 16; i++)
    {
        result.m[i] = m[i] / scalar;
    }
    return result;
}

// Transformação de vetores
Vec4 Mat4::operator*(const Vec4 &vec) const
{
    return Vec4(
        m[0] * vec.x + m[4] * vec.y + m[8] * vec.z + m[12] * vec.w,
        m[1] * vec.x + m[5] * vec.y + m[9] * vec.z + m[13] * vec.w,
        m[2] * vec.x + m[6] * vec.y + m[10] * vec.z + m[14] * vec.w,
        m[3] * vec.x + m[7] * vec.y + m[11] * vec.z + m[15] * vec.w);
}

Vec3 Mat4::operator*(const Vec3 &vec) const
{
    // Trata Vec3 como (x, y, z, 1) e divide por w
    Vec4 result = (*this) * Vec4(vec.x, vec.y, vec.z, 1.0f);
    if (std::fabs(result.w) > 1e-6f)
    {
        return Vec3(result.x / result.w, result.y / result.w, result.z / result.w);
    }
    return Vec3(result.x, result.y, result.z);
}

// Operadores compostos
Mat4 &Mat4::operator+=(const Mat4 &other)
{
    for (int i = 0; i < 16; i++)
    {
        m[i] += other.m[i];
    }
    return *this;
}

Mat4 &Mat4::operator-=(const Mat4 &other)
{
    for (int i = 0; i < 16; i++)
    {
        m[i] -= other.m[i];
    }
    return *this;
}

Mat4 &Mat4::operator*=(const Mat4 &other)
{
    *this = (*this) * other;
    return *this;
}

Mat4 &Mat4::operator*=(float scalar)
{
    for (int i = 0; i < 16; i++)
    {
        m[i] *= scalar;
    }
    return *this;
}

Mat4 &Mat4::operator/=(float scalar)
{
    for (int i = 0; i < 16; i++)
    {
        m[i] /= scalar;
    }
    return *this;
}

// Comparação
bool Mat4::operator==(const Mat4 &other) const
{
    const float epsilon = 1e-6f;
    for (int i = 0; i < 16; i++)
    {
        if (std::fabs(m[i] - other.m[i]) >= epsilon)
        {
            return false;
        }
    }
    return true;
}

bool Mat4::operator!=(const Mat4 &other) const
{
    return !(*this == other);
}

// Operações de matriz
Mat4 Mat4::transposed() const
{
    Mat4 result;
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            result(row, col) = (*this)(col, row);
        }
    }
    return result;
}

void Mat4::identity()
{
    for (int i = 0; i < 16; i++)
    {
        m[i] = 0.0f;
    }
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void Mat4::transpose()
{
    *this = transposed();
}

float Mat4::determinant() const
{
    // Expansão de Laplace pela primeira linha
    float det = 0.0f;
    for (int col = 0; col < 4; col++)
    {
        // Calcular cofator
        float subMat[9];
        int subIdx = 0;
        for (int r = 1; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (c != col)
                {
                    subMat[subIdx++] = (*this)(r, c);
                }
            }
        }

        float subDet = subMat[0] * (subMat[4] * subMat[8] - subMat[5] * subMat[7]) -
                       subMat[3] * (subMat[1] * subMat[8] - subMat[2] * subMat[7]) +
                       subMat[6] * (subMat[1] * subMat[5] - subMat[2] * subMat[4]);

        float sign = (col % 2 == 0) ? 1.0f : -1.0f;
        det += sign * (*this)(0, col) * subDet;
    }
    return det;
}

Vec3 Mat4::TransformPoint(const Vec3 &point) const
{
    // Aplica rotação + translação
    float x = m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12];
    float y = m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13];
    float z = m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14];
    float w = m[3] * point.x + m[7] * point.y + m[11] * point.z + m[15];

    return Vec3(x / w, y / w, z / w);
}

Vec3 Mat4::TransformVector(const Vec3 &vec) const
{
    // Só rotação, SEM translação
    float x = m[0] * vec.x + m[4] * vec.y + m[8] * vec.z;
    float y = m[1] * vec.x + m[5] * vec.y + m[9] * vec.z;
    float z = m[2] * vec.x + m[6] * vec.y + m[10] * vec.z;

    return Vec3(x, y, z);
}

void Mat4::TransformBox(BoundingBox &box) const
{

    const float Amin[3] = {box.min.x, box.min.y, box.min.z};
    const float Amax[3] = {box.max.x, box.max.y, box.max.z};

    float Bmin[3];
    float Bmax[3];

    Bmin[0] = Bmax[0] = m[12];
    Bmin[1] = Bmax[1] = m[13];
    Bmin[2] = Bmax[2] = m[14];

    for (unsigned int i = 0; i < 3; ++i)
    {
        for (unsigned int j = 0; j < 3; ++j)
        {
            float t = m[(j << 2) + i];

            const float a = t * Amin[j];
            const float b = t * Amax[j];

            if (a < b)
            {
                Bmin[i] += a;
                Bmax[i] += b;
            }
            else
            {
                Bmin[i] += b;
                Bmax[i] += a;
            }
        }
    }

    box.min.x = Bmin[0];
    box.min.y = Bmin[1];
    box.min.z = Bmin[2];

    box.max.x = Bmax[0];
    box.max.y = Bmax[1];
    box.max.z = Bmax[2];
}

Vec3 Mat4::getRotationDegrees() const
{
    Vec3 scale = getScale();

    if (scale.y < 0 && scale.z < 0)
    {
        scale.y = -scale.y;
        scale.z = -scale.z;
    }
    else if (scale.x < 0 && scale.z < 0)
    {
        scale.x = -scale.x;
        scale.z = -scale.z;
    }
    else if (scale.x < 0 && scale.y < 0)
    {
        scale.x = -scale.x;
        scale.y = -scale.y;
    }

    const Vec3 invScale(Reciprocal(scale.x), Reciprocal(scale.y), Reciprocal(scale.z));

    float Y = -std::asin(Clamp(m[2] * invScale.x, -1.0f, 1.0f));
    const float C = std::cos(Y);
    Y *= RAD2DEG;

    float X, Z;
    if (!isZero(C))
    {
        const float invC = Reciprocal(C);
        float rotx = m[10] * invC * invScale.z;
        float roty = m[6] * invC * invScale.y;
        X = std::atan2(roty, rotx) * RAD2DEG;

        rotx = m[0] * invC * invScale.x;
        roty = m[1] * invC * invScale.x;
        Z = std::atan2(roty, rotx) * RAD2DEG;
    }
    else
    {
        X = 0.0f;
        float rotx = m[5] * invScale.y;
        float roty = -m[4] * invScale.y;
        Z = std::atan2(roty, rotx) * RAD2DEG;
    }

    // normaliza para [0,360)
    if (X < 0.0f)
        X += 360.0f;
    if (Y < 0.0f)
        Y += 360.0f;
    if (Z < 0.0f)
        Z += 360.0f;

    return Vec3(X, Y, Z);
}

Vec3 Mat4::getScale() const
{

    // Deal with the 0 rotation case first
    if (isZero(m[1]) && isZero(m[2]) &&
        isZero(m[4]) && isZero(m[6]) &&
        isZero(m[8]) && isZero(m[9]))
    {
        return Vec3(m[0], m[5], m[10]);
    }

    // Full calculation
    return Vec3(
        std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]),
        std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]),
        std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]));
}

Vec3 Mat4::getTranslation() const
{
    return Vec3(m[12], m[13], m[14]);
}
Mat4 Mat4::inverse() const
{
    // Método de adjunta
    float det = determinant();
    if (std::fabs(det) < 1e-6f)
    {
        return Mat4(); // Retorna identidade se não inversível
    }

    Mat4 inv;
    float invDet = 1.0f / det;

    // Calcular matriz de cofatores e transpor (adjunta)
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            float subMat[9];
            int subIdx = 0;

            for (int r = 0; r < 4; r++)
            {
                if (r == row)
                    continue;
                for (int c = 0; c < 4; c++)
                {
                    if (c == col)
                        continue;
                    subMat[subIdx++] = (*this)(r, c);
                }
            }

            float subDet = subMat[0] * (subMat[4] * subMat[8] - subMat[5] * subMat[7]) -
                           subMat[3] * (subMat[1] * subMat[8] - subMat[2] * subMat[7]) +
                           subMat[6] * (subMat[1] * subMat[5] - subMat[2] * subMat[4]);

            float sign = ((row + col) % 2 == 0) ? 1.0f : -1.0f;
            inv(col, row) = sign * subDet * invDet; // Transposta aqui
        }
    }

    return inv;
}

Vec4 Mat4::transform(const Vec4 &vec) const
{
   return Vec4(
        m[0] * vec.x + m[4] * vec.y + m[8]  * vec.z + m[12] * vec.w,
        m[1] * vec.x + m[5] * vec.y + m[9]  * vec.z + m[13] * vec.w,
        m[2] * vec.x + m[6] * vec.y + m[10] * vec.z + m[14] * vec.w,
        m[3] * vec.x + m[7] * vec.y + m[11] * vec.z + m[15] * vec.w
    );
}

Vec3 Mat4::transform(const Vec3 &vec) const
{
    float x = m[0] * vec.x + m[4] * vec.y + m[8]  * vec.z + m[12];
    float y = m[1] * vec.x + m[5] * vec.y + m[9]  * vec.z + m[13];
    float z = m[2] * vec.x + m[6] * vec.y + m[10] * vec.z + m[14];
    float w = m[3] * vec.x + m[7] * vec.y + m[11] * vec.z + m[15];
    
    
    if (w != 0.0f && w != 1.0f)
    {
        float invW = 1.0f / w;
        return Vec3(x * invW, y * invW, z * invW);
    }
    
    return Vec3(x, y, z);
}
// Ignora translação!
Vec3 Mat4::transformDirection(const Vec3 &vec) const
{
   float x = m[0] * vec.x + m[4] * vec.y + m[8]  * vec.z;
    float y = m[1] * vec.x + m[5] * vec.y + m[9]  * vec.z;
    float z = m[2] * vec.x + m[6] * vec.y + m[10] * vec.z;
    
    return Vec3(x, y, z);
}

// Funções estáticas
Mat4 Mat4::Identity()
{
    return Mat4();
}

Quat MatrixToQuaternion(const Mat4 &m)
{
    Quat q;
    float trace = m.m[0] + m.m[5] + m.m[10];

    if (trace > 0.0f)
    {
        float s = sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.m[6] - m.m[9]) / s;
        q.y = (m.m[8] - m.m[2]) / s;
        q.z = (m.m[1] - m.m[4]) / s;
    }
    else if (m.m[0] > m.m[5] && m.m[0] > m.m[10])
    {
        float s = sqrt(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
        q.w = (m.m[6] - m.m[9]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[1] + m.m[4]) / s;
        q.z = (m.m[8] + m.m[2]) / s;
    }
    else if (m.m[5] > m.m[10])
    {
        float s = sqrt(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
        q.w = (m.m[8] - m.m[2]) / s;
        q.x = (m.m[1] + m.m[4]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[6] + m.m[9]) / s;
    }
    else
    {
        float s = sqrt(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
        q.w = (m.m[1] - m.m[4]) / s;
        q.x = (m.m[8] + m.m[2]) / s;
        q.y = (m.m[6] + m.m[9]) / s;
        q.z = 0.25f * s;
    }

    return q;
}

void Mat4::DecomposeMatrix(const Mat4 &matrix, Vec3 *outPosition, Quat *outRotation)
{
    // Extrai posição (última coluna)
    outPosition->x = matrix.m[12];
    outPosition->y = matrix.m[13];
    outPosition->z = matrix.m[14];

    // Extrai rotação (3x3 superior esquerdo)
    // Remove scale primeiro
    Vec3 scale;
    scale.x = sqrt(matrix.m[0] * matrix.m[0] + matrix.m[1] * matrix.m[1] + matrix.m[2] * matrix.m[2]);
    scale.y = sqrt(matrix.m[4] * matrix.m[4] + matrix.m[5] * matrix.m[5] + matrix.m[6] * matrix.m[6]);
    scale.z = sqrt(matrix.m[8] * matrix.m[8] + matrix.m[9] * matrix.m[9] + matrix.m[10] * matrix.m[10]);

    // Matriz de rotação (sem scale)
    Mat4 rotMatrix = matrix;
    rotMatrix.m[0] /= scale.x;
    rotMatrix.m[1] /= scale.x;
    rotMatrix.m[2] /= scale.x;
    rotMatrix.m[4] /= scale.y;
    rotMatrix.m[5] /= scale.y;
    rotMatrix.m[6] /= scale.y;
    rotMatrix.m[8] /= scale.z;
    rotMatrix.m[9] /= scale.z;
    rotMatrix.m[10] /= scale.z;

    // Converte matriz de rotação → Quaternion
    *outRotation = MatrixToQuaternion(rotMatrix);
}

Mat4 Mat4::Inverse(const Mat4 &mat)
{
    Mat4 inv;

    float det = mat.determinant();
    if (std::fabs(det) < 1e-6f)
    {
        return Mat4(); // Retorna identidade se não inversível
    }

    float invDet = 1.0f / det;

    // Calcular matriz de cofatores e transpor (adjunta)
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            float subMat[9];
            int subIdx = 0;

            for (int r = 0; r < 4; r++)
            {
                if (r == row)
                    continue;
                for (int c = 0; c < 4; c++)
                {
                    if (c == col)
                        continue;
                    subMat[subIdx++] = mat(r, c);
                }
            }

            float subDet = subMat[0] * (subMat[4] * subMat[8] - subMat[5] * subMat[7]) -
                           subMat[3] * (subMat[1] * subMat[8] - subMat[2] * subMat[7]) +
                           subMat[6] * (subMat[1] * subMat[5] - subMat[2] * subMat[4]);

            float sign = ((row + col) % 2 == 0) ? 1.0f : -1.0f;
            inv(col, row) = sign * subDet * invDet; // Transposta aqui
        }
    }

    return inv;
}

Mat4 Mat4::Scale(float sx, float sy, float sz)
{
    Mat4 result(0.0f);
    result.m[0] = sx;
    result.m[5] = sy;
    result.m[10] = sz;
    result.m[15] = 1.0f;
    return result;
}

Mat4 Mat4::Scale(const Vec3 &scale)
{
    return Scale(scale.x, scale.y, scale.z);
}

Mat4 Mat4::Translation(float tx, float ty, float tz)
{
    Mat4 result;
    result.m[12] = tx;
    result.m[13] = ty;
    result.m[14] = tz;
    return result;
}

Mat4 Mat4::Translation(const Vec3 &translation)
{
    return Translation(translation.x, translation.y, translation.z);
}

Mat4 Mat4::RotationX(float angleRad)
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);

    Mat4 result;
    result.m[5] = c;
    result.m[9] = -s;
    result.m[6] = s;
    result.m[10] = c;
    return result;
}

Mat4 Mat4::RotationY(float angleRad)
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);

    Mat4 result;
    result.m[0] = c;
    result.m[8] = s;
    result.m[2] = -s;
    result.m[10] = c;
    return result;
}

Mat4 Mat4::RotationZ(float angleRad)
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);

    Mat4 result;
    result.m[0] = c;
    result.m[4] = -s;
    result.m[1] = s;
    result.m[5] = c;
    return result;
}

Mat4 Mat4::RotationXDeg(float angleDeg)
{
    return RotationX(angleDeg * DEG_TO_RAD);
}

Mat4 Mat4::RotationYDeg(float angleDeg)
{
    return RotationY(angleDeg * DEG_TO_RAD);
}

Mat4 Mat4::RotationZDeg(float angleDeg)
{
    return RotationZ(angleDeg * DEG_TO_RAD);
}

Mat4 Mat4::Rotation(const Vec3 &axis, float angleRad)
{
    Vec3 a = axis.normalized();
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);
    float t = 1.0f - c;

    Mat4 result;
    result.m[0] = t * a.x * a.x + c;
    result.m[1] = t * a.x * a.y + s * a.z;
    result.m[2] = t * a.x * a.z - s * a.y;
    result.m[3] = 0.0f;

    result.m[4] = t * a.x * a.y - s * a.z;
    result.m[5] = t * a.y * a.y + c;
    result.m[6] = t * a.y * a.z + s * a.x;
    result.m[7] = 0.0f;

    result.m[8] = t * a.x * a.z + s * a.y;
    result.m[9] = t * a.y * a.z - s * a.x;
    result.m[10] = t * a.z * a.z + c;
    result.m[11] = 0.0f;

    result.m[12] = 0.0f;
    result.m[13] = 0.0f;
    result.m[14] = 0.0f;
    result.m[15] = 1.0f;

    return result;
}

Mat4 Mat4::RotationDeg(const Vec3 &axis, float angleDeg)
{
    return Rotation(axis, angleDeg * DEG_TO_RAD);
}

Mat4 Mat4::LookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
{
    Vec3 f = (center - eye).normalized();
    Vec3 s = Vec3::Cross(f, up).normalized();
    Vec3 u = Vec3::Cross(s, f);

    Mat4 result;
    result.m[0] = s.x;
    result.m[4] = s.y;
    result.m[8] = s.z;
    result.m[12] = -Vec3::Dot(s, eye);

    result.m[1] = u.x;
    result.m[5] = u.y;
    result.m[9] = u.z;
    result.m[13] = -Vec3::Dot(u, eye);

    result.m[2] = -f.x;
    result.m[6] = -f.y;
    result.m[10] = -f.z;
    result.m[14] = Vec3::Dot(f, eye);

    result.m[3] = 0.0f;
    result.m[7] = 0.0f;
    result.m[11] = 0.0f;
    result.m[15] = 1.0f;

    return result;
}

Mat4 Mat4::Perspective(float fovYRad, float aspect, float near, float far)
{
    float tanHalfFovy = std::tan(fovYRad / 2.0f);

    Mat4 result(0.0f);
    result.m[0] = 1.0f / (aspect * tanHalfFovy);
    result.m[5] = 1.0f / tanHalfFovy;
    result.m[10] = -(far + near) / (far - near);
    result.m[11] = -1.0f;
    result.m[14] = -(2.0f * far * near) / (far - near);

    return result;
}

Mat4 Mat4::PerspectiveDeg(float fovYDeg, float aspect, float near, float far)
{
    return Perspective(fovYDeg * DEG_TO_RAD, aspect, near, far);
}

Mat4 Mat4::Ortho(float left, float right, float bottom, float top, float near, float far)
{
    Mat4 result;
    result.m[0] = 2.0f / (right - left);
    result.m[5] = 2.0f / (top - bottom);
    result.m[10] = -2.0f / (far - near);
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = -(far + near) / (far - near);

    return result;
}

// Funções auxiliares para CSM
void Mat4::getFrustumCorners(Vec3 corners[8]) const
{
    // Inverter a matriz projection para obter os cantos em view space
    Mat4 invProj = this->inverse();

    // Cantos do NDC cube
    Vec4 ndcCorners[8] = {
        Vec4(-1, -1, -1, 1), // near bottom left
        Vec4(1, -1, -1, 1),  // near bottom right
        Vec4(1, 1, -1, 1),   // near top right
        Vec4(-1, 1, -1, 1),  // near top left
        Vec4(-1, -1, 1, 1),  // far bottom left
        Vec4(1, -1, 1, 1),   // far bottom right
        Vec4(1, 1, 1, 1),    // far top right
        Vec4(-1, 1, 1, 1)    // far top left
    };

    // Transformar de NDC para view space e normalizar
    for (int i = 0; i < 8; i++)
    {
        Vec4 viewSpace = invProj * ndcCorners[i];
        corners[i] = Vec3(viewSpace.x / viewSpace.w,
                          viewSpace.y / viewSpace.w,
                          viewSpace.z / viewSpace.w);
    }
}

Mat4 Mat4::OrthoFromCorners(const Vec3 corners[8], const Mat4 &lightView)
{
    // Transformar corners para light space
    Vec3 lightSpaceCorners[8];
    for (int i = 0; i < 8; i++)
    {
        lightSpaceCorners[i] = lightView * corners[i];
    }

    // Calcular BoundingBox em light space
    Vec3 min = lightSpaceCorners[0];
    Vec3 max = lightSpaceCorners[0];

    for (int i = 1; i < 8; i++)
    {
        min.x = std::fmin(min.x, lightSpaceCorners[i].x);
        min.y = std::fmin(min.y, lightSpaceCorners[i].y);
        min.z = std::fmin(min.z, lightSpaceCorners[i].z);

        max.x = std::fmax(max.x, lightSpaceCorners[i].x);
        max.y = std::fmax(max.y, lightSpaceCorners[i].y);
        max.z = std::fmax(max.z, lightSpaceCorners[i].z);
    }

    return Mat4::Ortho(min.x, max.x, min.y, max.y, min.z, max.z);
}

// Scalar * Mat4
Mat4 operator*(float scalar, const Mat4 &mat)
{
    return mat * scalar;
}

// Output
// std::ostream &operator<<(std::ostream &os, const Mat4 &mat)
// {
//     os << "Mat4(\n";
//     for (int row = 0; row < 4; row++)
//     {
//         os << "  ";
//         for (int col = 0; col < 4; col++)
//         {
//             os << mat(row, col);
//             if (col < 3)
//                 os << ", ";
//         }
//         os << "\n";
//     }
//     os << ")";
//     return os;
// }

// ==================== BoundingBox ====================

BoundingBox::BoundingBox() : min(Vec3(MAXFLOAT, MAXFLOAT, MAXFLOAT)), max(Vec3(-MAXFLOAT, -MAXFLOAT, -MAXFLOAT)) {}

BoundingBox::BoundingBox(const Vec3 &min, const Vec3 &max) : min(min), max(max) {}

void BoundingBox::expand(float x, float y, float z)
{
    min.x = std::fmin(min.x, x);
    min.y = std::fmin(min.y, y);
    min.z = std::fmin(min.z, z);

    max.x = std::fmax(max.x, x);
    max.y = std::fmax(max.y, y);
    max.z = std::fmax(max.z, z);
}

void BoundingBox::expand(const Vec3 &point)
{
    min.x = std::fmin(min.x, point.x);
    min.y = std::fmin(min.y, point.y);
    min.z = std::fmin(min.z, point.z);

    max.x = std::fmax(max.x, point.x);
    max.y = std::fmax(max.y, point.y);
    max.z = std::fmax(max.z, point.z);
}

void BoundingBox::expand(const BoundingBox &other)
{
    expand(other.min);
    expand(other.max);
}
void BoundingBox::addPoint(float x, float y, float z)
{
    if (x > max.x)
        max.x = x;
    if (y > max.y)
        max.y = y;
    if (z > max.z)
        max.z = z;
    if (x < min.x)
        min.x = x;
    if (y < min.y)
        min.y = y;
    if (z < min.z)
        min.z = z;
}
void BoundingBox::addPoint(const Vec3 &point)
{
    addPoint(point.x, point.y, point.z);
}
Vec3 BoundingBox::getCenter() const
{
    return (min + max) * 0.5f;
}
Vec3 BoundingBox::getExtent() const
{
    return max - min;
}
void BoundingBox::reset(const Vec3 &point)
{
    min = point;
    max = point;
}
void BoundingBox::copy(const BoundingBox &other)
{
    min = other.min;
    max = other.max;
}
void BoundingBox::clear()
{
    min = Vec3(MAXFLOAT, MAXFLOAT, MAXFLOAT);
    max = Vec3(-MAXFLOAT, -MAXFLOAT, -MAXFLOAT);
}

Vec3 BoundingBox::center() const
{
    return (min + max) * 0.5f;
}

Vec3 BoundingBox::size() const
{
    return max - min;
}

bool BoundingBox::contains(const Vec3 &point) const
{
    return point.x >= min.x && point.x <= max.x &&
           point.y >= min.y && point.y <= max.y &&
           point.z >= min.z && point.z <= max.z;
}

bool BoundingBox::merge(const BoundingBox &b)
{
    bool changed = false;

    // Ignore zero-size boxes
    if (min == max)
    {
        changed = true;
        min = b.min;
        max = b.max;
    }
    else if (b.min != b.max)
    {
        if (b.min.x < min.x)
        {
            changed = true;
            min.x = b.min.x;
        }
        if (b.min.y < min.y)
        {
            changed = true;
            min.y = b.min.y;
        }
        if (b.min.z < min.z)
        {
            changed = true;
            min.z = b.min.z;
        }

        if (b.max.x > max.x)
        {
            changed = true;
            max.x = b.max.x;
        }
        if (b.max.y > max.y)
        {
            changed = true;
            max.y = b.max.y;
        }
        if (b.max.z > max.z)
        {
            changed = true;
            max.z = b.max.z;
        }
    }

    return changed;
}

Vec3 BoundingBox::getCorner(unsigned int index) const
{
    switch (index)
    {
    case 0:
        return Vec3(min.x, min.y, max.z);
    case 1:
        return Vec3(max.x, min.y, max.z);
    case 2:
        return Vec3(max.x, max.y, max.z);
    case 3:
        return Vec3(min.x, max.y, max.z);
    case 4:
        return Vec3(min.x, min.y, min.z);
    case 5:
        return Vec3(max.x, min.y, min.z);
    case 6:
        return Vec3(max.x, max.y, min.z);
    case 7:
        return Vec3(min.x, max.y, min.z);
    default:
        return Vec3();
    }
}

void BoundingBox::transform(const Mat4 &m)
{
    // Efficient algorithm for transforming an AABB, taken from Graphics
    // Gems

    float minA[3] = {min.x, min.y, min.z};
    float maxA[3] = {max.x, max.y, max.z};
    float minB[3], maxB[3];

    for (unsigned i = 0; i < 3; ++i)
    {
        // componente de translação (coluna 3)
        minB[i] = m(i, 3);
        maxB[i] = m(i, 3);

        for (unsigned j = 0; j < 3; ++j)
        {
            float a = minA[j] * m(i, j);
            float b = maxA[j] * m(i, j);
            if (a < b)
            {
                minB[i] += a;
                maxB[i] += b;
            }
            else
            {
                minB[i] += b;
                maxB[i] += a;
            }
        }
    }

    min = Vec3(minB[0], minB[1], minB[2]);
    max = Vec3(maxB[0], maxB[1], maxB[2]);
}

BoundingBox BoundingBox::Transform(const BoundingBox &box, const Mat4 &m)
{
    BoundingBox result(box);
    result.transform(m);
    return result;
}

void BoundingBox::Transform(const BoundingBox &box, const Mat4 &m, BoundingBox &out)
{
    out.merge(box);
    out.transform(m);
}

// ==================== Quat ====================

// Construtores
Quat::Quat() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

Quat::Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

Quat::Quat(const Vec3 &axis, float angleRad)
{
    Vec3 normAxis = axis.normalized();
    float halfAngle = angleRad * 0.5f;
    float s = std::sin(halfAngle);
    x = normAxis.x * s;
    y = normAxis.y * s;
    z = normAxis.z * s;
    w = std::cos(halfAngle);
}

Quat::Quat(const Vec3 &eulerAnglesRad)
{
    *this = FromEulerAngles(eulerAnglesRad);
}

// Acesso por índice
float &Quat::operator[](int index)
{
    if (index == 0)
        return x;
    if (index == 1)
        return y;
    if (index == 2)
        return z;
    if (index == 3)
        return w;
    SDL_assert(false && "Quat index out of range");
    return x;
}

const float &Quat::operator[](int index) const
{
    if (index == 0)
        return x;
    if (index == 1)
        return y;
    if (index == 2)
        return z;
    if (index == 3)
        return w;
    SDL_assert(false && "Quat index out of range");
    return x;
}

// Operadores aritméticos
Quat Quat::operator+(const Quat &other) const
{
    return Quat(x + other.x, y + other.y, z + other.z, w + other.w);
}

Quat Quat::operator-(const Quat &other) const
{
    return Quat(x - other.x, y - other.y, z - other.z, w - other.w);
}

Quat Quat::operator*(const Quat &other) const
{
    // Multiplicação de Hamilton
    return Quat(
        w * other.x + x * other.w + y * other.z - z * other.y,
        w * other.y - x * other.z + y * other.w + z * other.x,
        w * other.z + x * other.y - y * other.x + z * other.w,
        w * other.w - x * other.x - y * other.y - z * other.z);
}

Quat Quat::operator*(float scalar) const
{
    return Quat(x * scalar, y * scalar, z * scalar, w * scalar);
}

Quat Quat::operator/(float scalar) const
{
    return Quat(x / scalar, y / scalar, z / scalar, w / scalar);
}

// Rotação de vetores
Vec3 Quat::operator*(const Vec3 &vec) const
{
    // q * v * q^-1
    Vec3 quatVec(x, y, z);
    Vec3 uv = Vec3::Cross(quatVec, vec);
    Vec3 uuv = Vec3::Cross(quatVec, uv);
    return vec + ((uv * w) + uuv) * 2.0f;
}

// Operadores compostos
Quat &Quat::operator+=(const Quat &other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
}

Quat &Quat::operator-=(const Quat &other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
}

Quat &Quat::operator*=(const Quat &other)
{
    *this = (*this) * other;
    return *this;
}

Quat &Quat::operator*=(float scalar)
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

Quat &Quat::operator/=(float scalar)
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

// Operador unário
Quat Quat::operator-() const
{
    return Quat(-x, -y, -z, -w);
}

// Comparação
bool Quat::operator==(const Quat &other) const
{
    const float epsilon = 1e-6f;
    return std::fabs(x - other.x) < epsilon &&
           std::fabs(y - other.y) < epsilon &&
           std::fabs(z - other.z) < epsilon &&
           std::fabs(w - other.w) < epsilon;
}

bool Quat::operator!=(const Quat &other) const
{
    return !(*this == other);
}

// Operações de quaternion
float Quat::lengthSquared() const
{
    return x * x + y * y + z * z + w * w;
}

float Quat::length() const
{
    return std::sqrt(lengthSquared());
}

Quat Quat::normalized() const
{
    float len = length();
    if (len > 0.0f)
    {
        return *this / len;
    }
    return Quat();
}

void Quat::normalize()
{
    float len = length();
    if (len > 0.0f)
    {
        x /= len;
        y /= len;
        z /= len;
        w /= len;
    }
}

Quat Quat::conjugate() const
{
    return Quat(-x, -y, -z, w);
}

Quat Quat::inverse() const
{
    float lenSq = lengthSquared();
    if (lenSq > 0.0f)
    {
        return conjugate() / lenSq;
    }
    return Quat();
}

float Quat::dot(const Quat &other) const
{
    return x * other.x + y * other.y + z * other.z + w * other.w;
}

// Conversão para matrizes
Mat4 Quat::toMat4() const
{
    Quat q = normalized();

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    Mat4 result;
    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);
    result.m[3] = 0.0f;

    result.m[4] = 2.0f * (xy - wz);
    result.m[5] = 1.0f - 2.0f * (xx + zz);
    result.m[6] = 2.0f * (yz + wx);
    result.m[7] = 0.0f;

    result.m[8] = 2.0f * (xz + wy);
    result.m[9] = 2.0f * (yz - wx);
    result.m[10] = 1.0f - 2.0f * (xx + yy);
    result.m[11] = 0.0f;

    result.m[12] = 0.0f;
    result.m[13] = 0.0f;
    result.m[14] = 0.0f;
    result.m[15] = 1.0f;

    return result;
}

Mat3 Quat::toMat3() const
{
    Quat q = normalized();

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    Mat3 result;
    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);

    result.m[3] = 2.0f * (xy - wz);
    result.m[4] = 1.0f - 2.0f * (xx + zz);
    result.m[5] = 2.0f * (yz + wx);

    result.m[6] = 2.0f * (xz + wy);
    result.m[7] = 2.0f * (yz - wx);
    result.m[8] = 1.0f - 2.0f * (xx + yy);

    return result;
}

Vec3 Quat::toEulerAnglesDeg() const
{
    Vec3 rad = toEulerAngles();
    return Vec3(rad.x * RAD_TO_DEG, rad.y * RAD_TO_DEG, rad.z * RAD_TO_DEG);
}

// Funções de criação
Quat Quat::Identity()
{
    return Quat();
}

Quat Quat::FromAxisAngle(const Vec3 &axis, float angleRad)
{
    return Quat(axis, angleRad);
}

Quat Quat::FromAxisAngleDeg(const Vec3 &axis, float angleDeg)
{
    return Quat(axis, angleDeg * DEG_TO_RAD);
}

Vec3 Quat::toEulerAngles() const
{
    // Inverter a ordem YXZ  FromEulerAngles
    Quat q = normalized();

    // Extrair pitch (rotação em X)
    float sinPitch = 2.0f * (q.w * q.x - q.y * q.z);
    float pitch;

    if (std::fabs(sinPitch) >= 1.0f)
    {
        pitch = std::copysign(PI / 2.0f, sinPitch); // Gimbal lock
    }
    else
    {
        pitch = std::asin(sinPitch);
    }

    // Extrair yaw (rotação em Y)
    float sinYaw = 2.0f * (q.w * q.y + q.x * q.z);
    float cosYaw = 1.0f - 2.0f * (q.y * q.y + q.x * q.x);
    float yaw = std::atan2(sinYaw, cosYaw);

    // Extrair roll (rotação em Z)
    float sinRoll = 2.0f * (q.w * q.z + q.x * q.y);
    float cosRoll = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    float roll = std::atan2(sinRoll, cosRoll);

    return Vec3(pitch, yaw, roll);
}

Quat Quat::FromEulerAngles(float pitch, float yaw, float roll)
{
    // Ordem de aplicação: Y (yaw) → X (pitch) → Z (roll)
    float cy = std::cos(yaw * 0.5f);
    float sy = std::sin(yaw * 0.5f);
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cr = std::cos(roll * 0.5f);
    float sr = std::sin(roll * 0.5f);

    Quat q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = cr * sp * cy + sr * cp * sy;
    q.y = cr * cp * sy - sr * sp * cy;
    q.z = sr * cp * cy - cr * sp * sy;

    return q;
}

Quat Quat::FromEulerAnglesDeg(float pitch, float yaw, float roll)
{
    return FromEulerAngles(pitch * DEG_TO_RAD, yaw * DEG_TO_RAD, roll * DEG_TO_RAD);
}

Quat Quat::FromEulerAngles(const Vec3 &eulerRad)
{
    return FromEulerAngles(eulerRad.x, eulerRad.y, eulerRad.z);
}

Quat Quat::FromEulerAnglesDeg(const Vec3 &eulerDeg)
{
    return FromEulerAnglesDeg(eulerDeg.x, eulerDeg.y, eulerDeg.z);
}

Quat Quat::FromMat4(const Mat4 &mat)
{
    // Extrair quaternion de matriz de rotação
    float trace = mat(0, 0) + mat(1, 1) + mat(2, 2);
    Quat q;

    if (trace > 0.0f)
    {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (mat(2, 1) - mat(1, 2)) / s;
        q.y = (mat(0, 2) - mat(2, 0)) / s;
        q.z = (mat(1, 0) - mat(0, 1)) / s;
    }
    else if ((mat(0, 0) > mat(1, 1)) && (mat(0, 0) > mat(2, 2)))
    {
        float s = std::sqrt(1.0f + mat(0, 0) - mat(1, 1) - mat(2, 2)) * 2.0f;
        q.w = (mat(2, 1) - mat(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (mat(0, 1) + mat(1, 0)) / s;
        q.z = (mat(0, 2) + mat(2, 0)) / s;
    }
    else if (mat(1, 1) > mat(2, 2))
    {
        float s = std::sqrt(1.0f + mat(1, 1) - mat(0, 0) - mat(2, 2)) * 2.0f;
        q.w = (mat(0, 2) - mat(2, 0)) / s;
        q.x = (mat(0, 1) + mat(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (mat(1, 2) + mat(2, 1)) / s;
    }
    else
    {
        float s = std::sqrt(1.0f + mat(2, 2) - mat(0, 0) - mat(1, 1)) * 2.0f;
        q.w = (mat(1, 0) - mat(0, 1)) / s;
        q.x = (mat(0, 2) + mat(2, 0)) / s;
        q.y = (mat(1, 2) + mat(2, 1)) / s;
        q.z = 0.25f * s;
    }

    return q.normalized();
}

Quat Quat::FromMat3(const Mat3 &mat)
{
    float trace = mat(0, 0) + mat(1, 1) + mat(2, 2);
    Quat q;

    if (trace > 0.0f)
    {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (mat(2, 1) - mat(1, 2)) / s;
        q.y = (mat(0, 2) - mat(2, 0)) / s;
        q.z = (mat(1, 0) - mat(0, 1)) / s;
    }
    else if ((mat(0, 0) > mat(1, 1)) && (mat(0, 0) > mat(2, 2)))
    {
        float s = std::sqrt(1.0f + mat(0, 0) - mat(1, 1) - mat(2, 2)) * 2.0f;
        q.w = (mat(2, 1) - mat(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (mat(0, 1) + mat(1, 0)) / s;
        q.z = (mat(0, 2) + mat(2, 0)) / s;
    }
    else if (mat(1, 1) > mat(2, 2))
    {
        float s = std::sqrt(1.0f + mat(1, 1) - mat(0, 0) - mat(2, 2)) * 2.0f;
        q.w = (mat(0, 2) - mat(2, 0)) / s;
        q.x = (mat(0, 1) + mat(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (mat(1, 2) + mat(2, 1)) / s;
    }
    else
    {
        float s = std::sqrt(1.0f + mat(2, 2) - mat(0, 0) - mat(1, 1)) * 2.0f;
        q.w = (mat(1, 0) - mat(0, 1)) / s;
        q.x = (mat(0, 2) + mat(2, 0)) / s;
        q.y = (mat(1, 2) + mat(2, 1)) / s;
        q.z = 0.25f * s;
    }

    return q.normalized();
}

// Interpolação
float Quat::Dot(const Quat &a, const Quat &b)
{
    return a.dot(b);
}

Quat Quat::Lerp(const Quat &a, const Quat &b, float t)
{
    return a * (1.0f - t) + b * t;
}

Quat Quat::Nlerp(const Quat &a, const Quat &b, float t)
{
    // Normalized linear interpolation (mais rápido que Slerp)
    Quat result = Lerp(a, b, t);
    return result.normalized();
}

Quat Quat::Slerp(const Quat &a, const Quat &b, float t)
{
    // Spherical linear interpolation (interpolação suave)
    Quat qa = a.normalized();
    Quat qb = b.normalized();

    float dot = Dot(qa, qb);

    // Se o dot product é negativo, inverter um quaternion para tomar o caminho mais curto
    if (dot < 0.0f)
    {
        qb = -qb;
        dot = -dot;
    }

    // Se muito próximos, usar lerp
    if (dot > 0.9995f)
    {
        return Nlerp(qa, qb, t);
    }

    // Clamp dot
    dot = std::fmax(-1.0f, std::fmin(1.0f, dot));

    float theta = std::acos(dot) * t;

    Quat qc = (qb - qa * dot).normalized();

    return qa * std::cos(theta) + qc * std::sin(theta);
}

// Rotações básicas
Quat Quat::RotationX(float angleRad)
{
    return Quat(Vec3(1.0f, 0.0f, 0.0f), angleRad);
}

Quat Quat::RotationY(float angleRad)
{
    return Quat(Vec3(0.0f, 1.0f, 0.0f), angleRad);
}

Quat Quat::RotationZ(float angleRad)
{
    return Quat(Vec3(0.0f, 0.0f, 1.0f), angleRad);
}

Quat Quat::RotationXDeg(float angleDeg)
{
    return RotationX(angleDeg * DEG_TO_RAD);
}

Quat Quat::RotationYDeg(float angleDeg)
{
    return RotationY(angleDeg * DEG_TO_RAD);
}

Quat Quat::RotationZDeg(float angleDeg)
{
    return RotationZ(angleDeg * DEG_TO_RAD);
}

// Scalar * Quat
Quat operator*(float scalar, const Quat &quat)
{
    return quat * scalar;
}
