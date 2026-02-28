#include "Animation.hpp"
#include "Mesh.hpp"
#include <glm/gtc/quaternion.hpp>
#include <SDL2/SDL.h>
#include <fstream>
#include <sstream>
#include <algorithm>

// ============================================================
//  Bind — resolve boneIndex por nome
// ============================================================
void Animation::bind(const std::vector<Bone> &bones)
{
    for (auto &ch : channels)
    {
        ch.boneIndex = -1;
        for (int i = 0; i < (int)bones.size(); i++)
        {
            if (bones[i].name == ch.boneName)
            {
                ch.boneIndex = i;
                break;
            }
        }
        if (ch.boneIndex == -1)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[Animation] Bone '%s' not found in mesh '%s'",
                        ch.boneName.c_str(), name.c_str());
    }
}

AnimationChannel *Animation::findChannel(const std::string &boneName)
{
    for (auto &ch : channels)
        if (ch.boneName == boneName)
            return &ch;
    return nullptr;
}

// ============================================================
//  Interpolação
// ============================================================
template <typename TKey>
int Animation::findKeyIndex(const std::vector<TKey> &keys, float time)
{
    for (int i = 0; i < (int)keys.size() - 1; i++)
        if (time < keys[i + 1].time)
            return i;
    return (int)keys.size() - 2;
}

glm::vec3 Animation::interpolatePosition(const AnimationChannel &ch, float time) const
{
    if (ch.posKeys.empty())
        return glm::vec3(0.f);
    if (ch.posKeys.size() == 1)
        return ch.posKeys[0].value;

    int i = findKeyIndex(ch.posKeys, time);
    if (i < 0)
        i = 0;
    int j = i + 1;
    if (j >= (int)ch.posKeys.size())
        return ch.posKeys.back().value;

    float t0 = ch.posKeys[i].time;
    float t1 = ch.posKeys[j].time;
    float t = (t1 - t0 > 1e-6f) ? (time - t0) / (t1 - t0) : 0.f;
    t = glm::clamp(t, 0.f, 1.f);

    return glm::mix(ch.posKeys[i].value, ch.posKeys[j].value, t);
}

glm::quat Animation::interpolateRotation(const AnimationChannel &ch, float time) const
{
    if (ch.rotKeys.empty())
        return glm::quat(1, 0, 0, 0);
    if (ch.rotKeys.size() == 1)
        return ch.rotKeys[0].value;

    int i = findKeyIndex(ch.rotKeys, time);
    if (i < 0)
        i = 0;
    int j = i + 1;
    if (j >= (int)ch.rotKeys.size())
        return ch.rotKeys.back().value;

    float t0 = ch.rotKeys[i].time;
    float t1 = ch.rotKeys[j].time;
    float t = (t1 - t0 > 1e-6f) ? (time - t0) / (t1 - t0) : 0.f;
    t = glm::clamp(t, 0.f, 1.f);

    return glm::normalize(glm::slerp(ch.rotKeys[i].value, ch.rotKeys[j].value, t));
}

glm::vec3 Animation::interpolateScale(const AnimationChannel &ch, float time) const
{
    if (ch.scaleKeys.empty())
        return glm::vec3(1.f);
    if (ch.scaleKeys.size() == 1)
        return ch.scaleKeys[0].value;

    int i = findKeyIndex(ch.scaleKeys, time);
    if (i < 0)
        i = 0;
    int j = i + 1;
    if (j >= (int)ch.scaleKeys.size())
        return ch.scaleKeys.back().value;

    float t0 = ch.scaleKeys[i].time;
    float t1 = ch.scaleKeys[j].time;
    float t = (t1 - t0 > 1e-6f) ? (time - t0) / (t1 - t0) : 0.f;
    t = glm::clamp(t, 0.f, 1.f);

    return glm::mix(ch.scaleKeys[i].value, ch.scaleKeys[j].value, t);
}
 