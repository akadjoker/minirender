#pragma once

#include <string>
#include <vector>

class VertexAnimatedMesh;

struct FrameAnimation
{
    std::string name;
    int startFrame = 0;
    int endFrame = 0;
    float fps = 8.0f;
    bool loop = true;
};

class FrameAnimator
{
public:
    void setMesh(VertexAnimatedMesh *mesh) { mesh_ = mesh; }

    void clearAnimations();

    
    int addAnimation(const std::string &name, int startFrame, int endFrame, float fps = 8.0f, bool loop = true);
    
    
    const FrameAnimation *getAnimation(int index) const;
    const FrameAnimation *getAnimation(const std::string &name) const;
    const FrameAnimation *currentAnimation() const;
    const FrameAnimation *previousAnimation() const;
    
    bool play(int index, bool restart = true, float blendDuration = 0.15f);
    bool play(const std::string &name, bool restart = true, float blendDuration = 0.15f);
    void stop();
    void pause();
    void resume();

    void setFrame(float frame);
    float currentFrame() const { return frame_; }
    float previousFrame() const { return previousFrame_; }
 
    bool playing() const { return playing_; }
    bool blending() const { return blending_ && previousIndex_ >= 0; }
    float blendAlpha() const { return blendAlpha_; }
    
    bool hasAnimations() const { return !animations_.empty(); }
    bool hasCurrentAnimation() const { return currentIndex_ >= 0; }
    
    void update(float dt );
    
private:
    int addAnimation(const FrameAnimation &animation);
    VertexAnimatedMesh *mesh_ = nullptr;
    std::vector<FrameAnimation> animations_;
    int currentIndex_ = -1;
    int previousIndex_ = -1;
    float frame_ = 0.0f;
    float previousFrame_ = 0.0f;
    float fps_ = 8.0f;
    bool playing_ = true;
    bool loop_ = true;
    bool blending_ = false;
    float blendTime_ = 0.0f;
    float blendDuration_ = 0.0f;
    float blendAlpha_ = 1.0f;
};
