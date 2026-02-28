#include "Animator.hpp"
#include "MeshLoader.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <SDL2/SDL.h>
#include <algorithm>

// ============================================================
//  helpers
// ============================================================
static glm::mat4 trsMatrix(const glm::vec3 &pos, const glm::quat &rot, const glm::vec3 &scl)
{
    glm::mat4 m = glm::translate(glm::mat4(1.f), pos);
    m = m * glm::mat4_cast(rot);
    m = glm::scale(m, scl);
    return m;
}

// ============================================================
//  AnimationLayer
// ============================================================
AnimationLayer::AnimationLayer() = default;

AnimationLayer::~AnimationLayer()
{
    for (auto &[_, anim] : anims_)
        delete anim;
}

Animation *AnimationLayer::loadAnimation(const std::string &name, const std::string &path)
{
    if (anims_.count(name)) return anims_[name];

    auto *anim = new Animation();
    AnimReader reader;
    if (!reader.load(path, anim))
    {
        delete anim;
        return nullptr;
    }
    anim->name = name;
    anims_[name] = anim;
    return anim;
}

void AnimationLayer::addAnimation(const std::string &name, Animation *anim)
{
    anims_[name] = anim;
}

Animation *AnimationLayer::getAnimation(const std::string &name) const
{
    auto it = anims_.find(name);
    return it != anims_.end() ? it->second : nullptr;
}

void AnimationLayer::bind(const std::vector<Bone> &bones)
{
    for (auto &[_, anim] : anims_)
        anim->bind(bones);
}

// ─── play ────────────────────────────────────────────────────
void AnimationLayer::play(const std::string &name, PlayMode mode, float blendTime)
{
    Animation *anim = getAnimation(name);
    if (!anim) return;

    // Mesma animação — apenas reinicia se estava pausada
    if (currentName_ == name && current_ == anim)
    {
        if (paused_)
        {
            time_   = (mode == PlayMode::Backward) ? anim->getDuration() : 0.f;
            paused_ = false;
            pingPongReverse_ = false;
            freezeBlend_     = false;
            mode_            = mode;
        }
        return;
    }

    bool needsBlend = current_ && currentName_ != name
                      && blendTime > 0.f && !paused_;

    if (needsBlend)
    {
        previous_     = current_;
        previousName_ = currentName_;
        timeBlend_    = time_;
        blending_     = true;
        blendTime_    = 0.f;
        blendDuration_ = poseBlendTime;
        freezeBlend_  = true;
    }
    else
    {
        blending_    = false;
        previous_    = nullptr;
        freezeBlend_ = false;
    }

    current_     = anim;
    currentName_ = name;
    mode_        = mode;
    paused_      = false;
    pingPongReverse_ = false;
    shouldReturn_    = false;

    time_ = (mode == PlayMode::Backward) ? anim->getDuration() : 0.f;
}

void AnimationLayer::crossFade(const std::string &name, float duration)
{
    play(name, mode_, duration);
}

void AnimationLayer::playOneShot(const std::string &name, const std::string &returnTo,
                                 float blendIn, PlayMode returnMode)
{
    play(name, PlayMode::Once, blendIn);
    shouldReturn_ = true;
    returnToAnim_ = returnTo;
    returnMode_   = returnMode;
}

void AnimationLayer::triggerAction(const std::string &name, float blendTime)
{
    if (!currentName_.empty() && !paused_)
        playOneShot(name, currentName_, blendTime, mode_);
    else
        play(name, PlayMode::Once, blendTime);
}

void AnimationLayer::stop(float blendOutTime)
{
    if (blendOutTime > 0.f && current_)
    {
        previous_     = current_;
        previousName_ = currentName_;
        timeBlend_    = time_;
        current_      = nullptr;
        currentName_  = "";
        time_         = 0.f;
        blending_     = true;
        blendTime_    = 0.f;
        blendDuration_ = blendOutTime;
        freezeBlend_  = false;
    }
    else
    {
        current_     = nullptr;
        currentName_ = "";
        time_        = 0.f;
        paused_      = false;
        blending_    = false;
        freezeBlend_ = false;
    }
}

void AnimationLayer::pause()  { paused_ = true;  }
void AnimationLayer::resume() { paused_ = false; }

bool AnimationLayer::isPlaying(const std::string &name) const
{
    return currentName_ == name && current_ && !paused_;
}

bool AnimationLayer::hasFinished() const
{
    return current_ && mode_ == PlayMode::Once && paused_;
}

float AnimationLayer::getNormalizedTime() const
{
    if (!current_ || current_->getDuration() <= 0.f) return 0.f;
    return time_ / current_->getDuration();
}

// ─── update ─────────────────────────────────────────────────
void AnimationLayer::update(float dt, std::vector<glm::mat4> &finalMatrices,
                             const std::vector<Bone> &bones)
{
    if (paused_ || !current_) return;

    float tick = dt * speed_;

    // ── BLENDING ────────────────────────────────────────────
    if (blending_ && previous_)
    {
        blendTime_ += tick;
        float blend = glm::clamp(blendTime_ / blendDuration_, 0.f, 1.f);

        if (freezeBlend_)
        {
            // Blend estático entre duas poses congeladas
            for (const auto &ch : current_->channels)
            {
                if (ch.boneIndex < 0) continue;
                if (!boneMask_.empty() && !boneMask_.count(ch.boneIndex)) continue;

                glm::vec3 pos2 = current_->interpolatePosition(ch, time_);
                glm::quat rot2 = current_->interpolateRotation(ch, time_);
                glm::vec3 scl2 = current_->interpolateScale   (ch, time_);

                AnimationChannel *prev = previous_->findChannel(ch.boneName);
                if (prev)
                {
                    glm::vec3 pos1 = previous_->interpolatePosition(*prev, timeBlend_);
                    glm::quat rot1 = previous_->interpolateRotation(*prev, timeBlend_);
                    glm::vec3 scl1 = previous_->interpolateScale   (*prev, timeBlend_);

                    glm::vec3 p = glm::mix(pos1, pos2, blend);
                    glm::quat r = glm::normalize(glm::slerp(rot1, rot2, blend));
                    glm::vec3 s = glm::mix(scl1, scl2, blend);

                    finalMatrices[ch.boneIndex] = trsMatrix(p, r, s);
                }
                else
                {
                    finalMatrices[ch.boneIndex] = trsMatrix(pos2, rot2, scl2);
                }
            }

            if (blend >= 1.f)
            {
                blending_    = false;
                previous_    = nullptr;
                blendTime_   = 0.f;
                freezeBlend_ = false;
            }
            return;
        }

        // Blend dinâmico — ambas as animações continuam a correr
        time_      += tick * current_->getTicksPerSecond();
        timeBlend_ += tick * previous_->getTicksPerSecond();

        float dur = current_->getDuration();
        if (mode_ == PlayMode::Loop && time_ >= dur)
            time_ = fmod(time_, dur);
        else
            time_ = glm::min(time_, dur);

        float prevDur = previous_->getDuration();
        if (timeBlend_ >= prevDur)
            timeBlend_ = fmod(timeBlend_, prevDur);

        for (const auto &ch : current_->channels)
        {
            if (ch.boneIndex < 0) continue;
            if (!boneMask_.empty() && !boneMask_.count(ch.boneIndex)) continue;

            glm::vec3 pos2 = current_->interpolatePosition(ch, time_);
            glm::quat rot2 = current_->interpolateRotation(ch, time_);
            glm::vec3 scl2 = current_->interpolateScale   (ch, time_);

            AnimationChannel *prev = previous_->findChannel(ch.boneName);
            if (prev)
            {
                glm::vec3 pos1 = previous_->interpolatePosition(*prev, timeBlend_);
                glm::quat rot1 = previous_->interpolateRotation(*prev, timeBlend_);
                glm::vec3 scl1 = previous_->interpolateScale   (*prev, timeBlend_);

                glm::vec3 p = glm::mix(pos1, pos2, blend);
                glm::quat r = glm::normalize(glm::slerp(rot1, rot2, blend));
                glm::vec3 s = glm::mix(scl1, scl2, blend);

                finalMatrices[ch.boneIndex] = trsMatrix(p, r, s);
            }
            else
            {
                finalMatrices[ch.boneIndex] = trsMatrix(pos2, rot2, scl2);
            }
        }

        if (blend >= 1.f)
        {
            blending_  = false;
            previous_  = nullptr;
            blendTime_ = 0.f;
        }
        return;
    }

    // ── PLAYBACK NORMAL ─────────────────────────────────────
    float dur  = current_->getDuration();
    bool  ended = false;

    switch (mode_)
    {
        case PlayMode::Loop:
            time_ += tick * current_->getTicksPerSecond();
            while (time_ >= dur) time_ -= dur;
            break;

        case PlayMode::Once:
            time_ += tick * current_->getTicksPerSecond();
            if (time_ >= dur) { time_ = dur; ended = true; paused_ = true; }
            break;

        case PlayMode::Backward:
            time_ -= tick * current_->getTicksPerSecond();
            if (time_ <= 0.f) { time_ = 0.f; ended = true; paused_ = true; }
            break;

        case PlayMode::PingPong:
            if (!pingPongReverse_)
            {
                time_ += tick * current_->getTicksPerSecond();
                if (time_ >= dur) { time_ = dur; pingPongReverse_ = true; }
            }
            else
            {
                time_ -= tick * current_->getTicksPerSecond();
                if (time_ <= 0.f) { time_ = 0.f; pingPongReverse_ = false; }
            }
            break;
    }

    // Aplicar channels
    for (const auto &ch : current_->channels)
    {
        if (ch.boneIndex < 0 || ch.boneIndex >= (int)finalMatrices.size()) continue;
        if (!boneMask_.empty() && !boneMask_.count(ch.boneIndex)) continue;

        glm::vec3 pos = current_->interpolatePosition(ch, time_);
        glm::quat rot = current_->interpolateRotation(ch, time_);
        glm::vec3 scl = current_->interpolateScale   (ch, time_);

        finalMatrices[ch.boneIndex] = trsMatrix(pos, rot, scl);
    }

    // OneShot return
    if (ended && shouldReturn_ && !returnToAnim_.empty())
    {
        shouldReturn_ = false;
        play(returnToAnim_, returnMode_, defaultBlendTime);
    }
}

bool AnimationLayer::getFrame(const std::string &animName, float time,
                               const std::string &boneName,
                               glm::vec3 &outPos, glm::quat &outRot) const
{
    Animation *anim = getAnimation(animName);
    if (!anim) return false;
    AnimationChannel *ch = anim->findChannel(boneName);
    if (!ch) return false;
    outPos = anim->interpolatePosition(*ch, time);
    outRot = anim->interpolateRotation(*ch, time);
    return true;
}

// ============================================================
//  Animator
// ============================================================
Animator::Animator(AnimatedMesh *mesh) : mesh_(mesh) {}

Animator::~Animator()
{
    for (auto *l : layers_) delete l;
}

AnimationLayer *Animator::addLayer()
{
    auto *layer = new AnimationLayer();
    layers_.push_back(layer);
    return layer;
}

AnimationLayer *Animator::getLayer(int index) const
{
    if (index < 0 || index >= (int)layers_.size()) return nullptr;
    return layers_[index];
}

void Animator::setLayerMaskFromBone(int layerIndex, const std::string &rootBoneName)
{
    AnimationLayer *layer = getLayer(layerIndex);
    if (!layer || !mesh_) return;

    const auto &bones = mesh_->bones;
    const int   N     = (int)bones.size();

    // Find root bone index by name
    int rootIdx = -1;
    for (int i = 0; i < N; i++)
        if (bones[i].name == rootBoneName) { rootIdx = i; break; }

    if (rootIdx < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[Animator] setLayerMaskFromBone: bone '%s' not found", rootBoneName.c_str());
        return;
    }

    // BFS from rootIdx — collect all descendants (including root itself)
    std::unordered_set<int> mask;
    std::vector<int> queue = { rootIdx };
    for (int qi = 0; qi < (int)queue.size(); qi++)
    {
        int p = queue[qi];
        mask.insert(p);
        for (int i = 0; i < N; i++)
            if (bones[i].parent == p) queue.push_back(i);
    }

    SDL_Log("[Animator] Layer %d mask: %zu bones rooted at '%s'",
            layerIndex, mask.size(), rootBoneName.c_str());

    layer->setBoneMask(mask);
}

void Animator::initialize()
{
    if (!mesh_) return;
    for (auto *l : layers_)
        l->bind(mesh_->bones);

    const auto &bones = mesh_->bones;
    const int   N     = (int)bones.size();

    // Use localPose stored directly from the file — rest pose in parent-local space.
    restLocal_.resize(N);
    for (int i = 0; i < N; i++)
        restLocal_[i] = bones[i].localPose;

    // Compute BFS traversal order so parents are always processed before children.
    // Bones in sinbad.h3d are NOT sorted topologically (e.g. bone[0].parent == 16).
    boneOrder_.clear();
    boneOrder_.reserve(N);
    std::vector<bool> visited(N, false);
    for (int i = 0; i < N; i++)
        if (bones[i].parent < 0) { boneOrder_.push_back(i); visited[i] = true; }
    for (int qi = 0; qi < (int)boneOrder_.size(); qi++) {
        int p = boneOrder_[qi];
        for (int i = 0; i < N; i++)
            if (bones[i].parent == p && !visited[i]) {
                boneOrder_.push_back(i);
                visited[i] = true;
            }
    }
    // Any bone not reachable from a root (broken data) — append as-is
    for (int i = 0; i < N; i++)
        if (!visited[i]) boneOrder_.push_back(i);

    localMatrices_.resize(N);
    mesh_->finalMatrices.resize(N, glm::mat4(1.f));
    initialized_ = true;
}

void Animator::update(float dt)
{
    if (!active || !mesh_ || layers_.empty()) return;

    if (!initialized_) initialize();

    const auto &bones = mesh_->bones;
    const int   N     = (int)bones.size();
    auto       &final = mesh_->finalMatrices;

    // 1. Reset local matrices to rest pose each frame
    localMatrices_.assign(restLocal_.begin(), restLocal_.end());

    // 2. Each layer overwrites animated channels with their local TRS
    for (auto *l : layers_)
        l->update(dt, localMatrices_, bones);

    // 3. Walk bone hierarchy in BFS order (guarantees parent processed before child)
    for (int i : boneOrder_)
    {
        if (bones[i].parent < 0)
            final[i] = localMatrices_[i];
        else
            final[i] = final[bones[i].parent] * localMatrices_[i];
    }

    // 4. Apply inverse bind pose to convert from bone-local space to mesh space
    for (int i = 0; i < N; i++)
        final[i] *= bones[i].offset;
}