#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// ============================================================
//  Keyframes
// ============================================================
struct PosKey  { float time; glm::vec3 value; };
struct RotKey  { float time; glm::quat value; };
struct ScaleKey{ float time; glm::vec3 value; };

// ============================================================
//  AnimationChannel — dados de um bone ao longo do tempo
// ============================================================
struct AnimationChannel
{
    std::string          boneName;
    int                  boneIndex = -1; // resolvido no Bind()

    std::vector<PosKey>   posKeys;
    std::vector<RotKey>   rotKeys;
    std::vector<ScaleKey> scaleKeys;
};

// ============================================================
//  Animation — conjunto de channels + metadados
// ============================================================
class Animation
{
public:
    std::string name;

    float duration        = 0.f; // em ticks
    float ticksPerSecond  = 24.f;

    std::vector<AnimationChannel> channels;

    float getDuration()       const { return duration; }
    float getTicksPerSecond() const { return ticksPerSecond; }

    // Resolve boneIndex para cada channel — chama depois de carregar
    void bind(const std::vector<struct Bone> &bones);

    AnimationChannel *findChannel(const std::string &boneName);

    // Interpolação
    glm::vec3 interpolatePosition(const AnimationChannel &ch, float time) const;
    glm::quat interpolateRotation(const AnimationChannel &ch, float time) const;
    glm::vec3 interpolateScale   (const AnimationChannel &ch, float time) const;

 

private:
    template<typename TKey>
    static int findKeyIndex(const std::vector<TKey> &keys, float time);
};

// ============================================================
//  PlayMode
// ============================================================
enum class PlayMode { Loop, Once, Backward, PingPong };