#pragma once
#include "Animator.hpp"
#include <string>
#include <unordered_map>

// Minimal runtime state machine for AnimationLayer.
// Designed for gameplay-level state switches (idle/run/dance/etc.)
// without replacing the existing Animator/Layer architecture.
class AnimationStateMachine
{
public:
    struct State
    {
        std::string anim;
        PlayMode    mode         = PlayMode::Loop;
        float       speed        = 1.f;
        float       defaultBlend = -1.f; // <0 => use layer defaults/profiles

        State() = default;
        State(const std::string &a, PlayMode m, float s, float b);
    };

    void bind(AnimationLayer *layer);

    void addState(const std::string &name, const State &state);

    bool setInitialState(const std::string &name);

    bool requestState(const std::string &name);

    void update();

    const std::string &currentState() const;
    std::string currentAnim() const;

private:
    AnimationLayer *layer_ = nullptr; // non-owning
    std::unordered_map<std::string, State> states_;
    std::string currentState_;
    std::string requestedState_;
};
