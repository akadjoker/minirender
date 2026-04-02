#include "AnimationStateMachine.hpp"

AnimationStateMachine::State::State(const std::string &a, PlayMode m, float s, float b)
    : anim(a), mode(m), speed(s), defaultBlend(b) {}

void AnimationStateMachine::bind(AnimationLayer *layer)
{
    layer_ = layer;
}

void AnimationStateMachine::addState(const std::string &name, const State &state)
{
    states_[name] = state;
}

bool AnimationStateMachine::setInitialState(const std::string &name)
{
    if (!layer_) return false;

    auto it = states_.find(name);
    if (it == states_.end()) return false;

    currentState_ = name;
    requestedState_.clear();

    const State &s = it->second;
    layer_->setSpeed(s.speed);
    layer_->play(s.anim, s.mode, 0.f);
    return true;
}

bool AnimationStateMachine::requestState(const std::string &name)
{
    if (states_.find(name) == states_.end()) return false;
    requestedState_ = name;
    return true;
}

void AnimationStateMachine::update()
{
    if (!layer_ || requestedState_.empty()) return;

    if (requestedState_ == currentState_)
    {
        requestedState_.clear();
        return;
    }

    auto it = states_.find(requestedState_);
    if (it == states_.end())
    {
        requestedState_.clear();
        return;
    }

    const State &s = it->second;
    layer_->setSpeed(s.speed);
    layer_->play(s.anim, s.mode, s.defaultBlend);
    currentState_ = requestedState_;
    requestedState_.clear();
}

const std::string &AnimationStateMachine::currentState() const
{
    return currentState_;
}

std::string AnimationStateMachine::currentAnim() const
{
    auto it = states_.find(currentState_);
    return (it != states_.end()) ? it->second.anim : std::string();
}
