#include "FrameAnimator.hpp"
#include "Mesh.hpp"

#include <algorithm>
#include <cmath>

void FrameAnimator::clearAnimations()
{
    animations_.clear();
    currentIndex_ = -1;
    previousIndex_ = -1;
    blending_ = false;
    blendTime_ = 0.0f;
    blendDuration_ = 0.0f;
    blendAlpha_ = 1.0f;
}

int FrameAnimator::addAnimation(const FrameAnimation &animation)
{
    animations_.push_back(animation);
    return (int)animations_.size() - 1;
}

int FrameAnimator::addAnimation(const std::string &name, int startFrame, int endFrame, float fps, bool loop)
{
    FrameAnimation anim;
    anim.name = name;
    anim.startFrame = startFrame;
    anim.endFrame = endFrame;
    anim.fps = fps;
    anim.loop = loop;
    return addAnimation(anim);
}

const FrameAnimation *FrameAnimator::getAnimation(int index) const
{
    if (index < 0 || index >= (int)animations_.size())
        return nullptr;
    return &animations_[index];
}

const FrameAnimation *FrameAnimator::getAnimation(const std::string &name) const
{
    for (const FrameAnimation &animation : animations_)
        if (animation.name == name)
            return &animation;
    return nullptr;
}

const FrameAnimation *FrameAnimator::currentAnimation() const
{
    return getAnimation(currentIndex_);
}

const FrameAnimation *FrameAnimator::previousAnimation() const
{
    return getAnimation(previousIndex_);
}

bool FrameAnimator::play(int index, bool restart, float blendDuration)
{
    const FrameAnimation *animation = getAnimation(index);
    if (!animation)
        return false;

    const int oldIndex = currentIndex_;
    const bool changingAnimation = oldIndex >= 0 && oldIndex != index;
    if (changingAnimation && blendDuration > 0.0f)
    {
        previousIndex_ = oldIndex;
        previousFrame_ = frame_;
        blending_ = true;
        blendTime_ = 0.0f;
        blendDuration_ = blendDuration;
        blendAlpha_ = 0.0f;
    }
    else
    {
        previousIndex_ = -1;
        blending_ = false;
        blendTime_ = 0.0f;
        blendDuration_ = 0.0f;
        blendAlpha_ = 1.0f;
    }

    currentIndex_ = index;
    fps_ = animation->fps;
    loop_ = animation->loop;
    playing_ = true;

    if (restart || changingAnimation)
        frame_ = (float)animation->startFrame;

    if (mesh_)
    {
        if (blending_ && previousAnimation())
        {
            const FrameAnimation *previous = previousAnimation();
            mesh_->setFrameBlended(previousFrame_, previous->startFrame, previous->endFrame,
                                   frame_, animation->startFrame, animation->endFrame, blendAlpha_);
        }
        else
        {
            mesh_->setFrame(frame_, animation->startFrame, animation->endFrame);
        }
    }

    return true;
}

bool FrameAnimator::play(const std::string &name, bool restart, float blendDuration)
{
    for (int i = 0; i < (int)animations_.size(); ++i)
        if (animations_[i].name == name)
            return play(i, restart, blendDuration);
    return false;
}

void FrameAnimator::stop()
{
    playing_ = false;
}

void FrameAnimator::pause()
{
    playing_ = false;
}

void FrameAnimator::resume()
{
    playing_ = true;
}

void FrameAnimator::setFrame(float frame)
{
    frame_ = frame;
    previousIndex_ = -1;
    blending_ = false;
    blendTime_ = 0.0f;
    blendDuration_ = 0.0f;
    blendAlpha_ = 1.0f;
    if (mesh_)
    {
        if (const FrameAnimation *animation = currentAnimation())
            mesh_->setFrame(frame_, animation->startFrame, animation->endFrame);
        else
            mesh_->setFrame(frame_);
    }
}

 

void FrameAnimator::update(float dt)
{
    if (!playing_ || !mesh_)
        return;

    if (const FrameAnimation *animation = currentAnimation())
    {
        const float start = (float)std::min(animation->startFrame, animation->endFrame);
        const float end = (float)std::max(animation->startFrame, animation->endFrame);
        const float span = end - start + 1.0f;
        frame_ += dt * fps_;

        if (loop_ && span > 0.0f)
        {
            float local = std::fmod(frame_ - start, span);
            if (local < 0.0f)
                local += span;
            frame_ = start + local;
        }
        else if (frame_ > end)
        {
            frame_ = end;
            playing_ = false;
        }

        if (blending_ && previousAnimation())
        {
            blendTime_ += dt;
            blendAlpha_ = std::clamp(blendTime_ / std::max(blendDuration_, 1e-4f), 0.0f, 1.0f);
            const FrameAnimation *previous = previousAnimation();
            mesh_->setFrameBlended(previousFrame_, previous->startFrame, previous->endFrame,
                                   frame_, animation->startFrame, animation->endFrame, blendAlpha_);

            if (blendAlpha_ >= 1.0f)
            {
                previousIndex_ = -1;
                blending_ = false;
                blendTime_ = 0.0f;
                blendDuration_ = 0.0f;
                blendAlpha_ = 1.0f;
            }
        }
        else
        {
            mesh_->setFrame(frame_, animation->startFrame, animation->endFrame);
        }
        return;
    }

    const int totalFrames = mesh_->frameCount();
    if (totalFrames <= 0)
        return;

    frame_ += dt * fps_;

    if (loop_)
    {
        while (frame_ >= (float)totalFrames)
            frame_ -= (float)totalFrames;
        while (frame_ < 0.0f)
            frame_ += (float)totalFrames;
    }
    else if (frame_ > (float)(totalFrames - 1))
    {
        frame_ = (float)(totalFrames - 1);
        playing_ = false;
    }

    mesh_->setFrame(frame_);
}
