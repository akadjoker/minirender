#include "VertexAnimation.hpp"
#include "Node.hpp"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace
{
static glm::mat4 trsMatrix(const glm::vec3 &pos, const glm::quat &rot, const glm::vec3 &scl)
{
    glm::mat4 m = glm::translate(glm::mat4(1.f), pos);
    m = m * glm::mat4_cast(rot);
    m = glm::scale(m, scl);
    return m;
}

static VertexTagFrame sampleTagFrame(const VertexTagTrack &tag, int frameA, int frameB, float t)
{
    VertexTagFrame out;
    if (tag.frames.empty())
        return out;

    const int maxIdx = static_cast<int>(tag.frames.size()) - 1;
    frameA = glm::clamp(frameA, 0, maxIdx);
    frameB = glm::clamp(frameB, 0, maxIdx);

    const VertexTagFrame &a = tag.frames[frameA];
    const VertexTagFrame &b = tag.frames[frameB];

    const float tt = glm::clamp(t, 0.f, 1.f);
    out.position = glm::mix(a.position, b.position, tt);
    out.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, tt));
    out.scale = glm::mix(a.scale, b.scale, tt);
    return out;
}
}

bool VertexAnimAsset::valid() const
{
    return templateMesh != nullptr &&
           applySample != nullptr &&
           !clips.empty();
}

void VertexAnimAsset::apply(Mesh *mesh, const VertexAnimSample &sample) const
{
    if (!mesh || !applySample)
        return;
    applySample(mesh, sample, applyUserData);
}

VertexAnimClip::VertexAnimClip(const std::string &n, int f0, int f1, float f, bool l)
    : name(n), first(f0), last(f1), fps(f), loop(l)
{
}

void VertexAnimController::addClip(const VertexAnimClip &clip)
{
    auto it = std::find_if(clips_.begin(), clips_.end(), [&clip](const VertexAnimClip &c) {
        return c.name == clip.name;
    });

    if (it != clips_.end())
    {
        *it = clip;
        return;
    }

    clips_.push_back(clip);
}

const VertexAnimClip *VertexAnimController::findClip(const std::string &name) const
{
    auto it = std::find_if(clips_.begin(), clips_.end(), [&name](const VertexAnimClip &c) {
        return c.name == name;
    });
    return (it != clips_.end()) ? &(*it) : nullptr;
}

bool VertexAnimController::play(const std::string &name, float blendTime, bool restartSame)
{
    const VertexAnimClip *next = findClip(name);
    if (!next) return false;

    const bool same = (currentClip_ && currentClip_->name == next->name);
    if (same && !restartSame) return true;

    if (!same && currentClip_ && blendTime > 0.f)
    {
        previousClip_ = currentClip_;
        previousState_ = currentState_;
        transitionActive_ = true;
        transitionTime_ = 0.f;
        transitionDuration_ = glm::max(blendTime, 1e-4f);
    }
    else
    {
        previousClip_ = nullptr;
        transitionActive_ = false;
        transitionTime_ = 0.f;
    }

    currentClip_ = next;
    resetStateFromClip(currentState_, *currentClip_);
    return true;
}

void VertexAnimController::stop()
{
    currentClip_ = nullptr;
    previousClip_ = nullptr;
    transitionActive_ = false;
    transitionTime_ = 0.f;
}

void VertexAnimController::update(float dt)
{
    if (!currentClip_) return;

    stepState(currentState_, dt);

    if (transitionActive_ && previousClip_)
    {
        stepState(previousState_, dt);
        transitionTime_ += dt;
        if (transitionTime_ >= transitionDuration_)
        {
            transitionActive_ = false;
            previousClip_ = nullptr;
            transitionTime_ = 0.f;
        }
    }
}

bool VertexAnimController::hasCurrentClip() const
{
    return currentClip_ != nullptr;
}

const VertexAnimClip *VertexAnimController::currentClip() const
{
    return currentClip_;
}

const VertexAnimClip *VertexAnimController::previousClip() const
{
    return previousClip_;
}

bool VertexAnimController::isTransitioning() const
{
    return transitionActive_ && previousClip_ != nullptr;
}

VertexAnimSample VertexAnimController::sample() const
{
    VertexAnimSample s;
    s.currentFrame = currentState_.current;
    s.nextFrame = currentState_.next;
    s.currentInterp = currentState_.interp;

    if (isTransitioning())
    {
        const float x = glm::clamp(transitionTime_ / glm::max(transitionDuration_, 1e-4f), 0.f, 1.f);
        s.clipBlend = 1.f - glm::smoothstep(0.f, 1.f, x);
        s.hasPrevious = true;
        s.previousFrame = previousState_.current;
        s.previousNextFrame = previousState_.next;
        s.previousInterp = previousState_.interp;
    }

    return s;
}

void VertexAnimController::resetStateFromClip(ClipState &state, const VertexAnimClip &clip)
{
    state.first = std::max(clip.first, 0);
    state.last = std::max(clip.last, state.first);
    state.fps = glm::max(clip.fps, 0.01f);
    state.loop = clip.loop;

    state.current = state.first;
    state.next = state.first + 1;
    if (state.next > state.last) state.next = state.first;

    state.interp = 0.f;
    state.accum = 0.f;
    state.playing = true;
}

void VertexAnimController::stepState(ClipState &state, float dt)
{
    if (!state.playing) return;

    const float frameTime = 1.f / glm::max(state.fps, 0.01f);
    state.accum += dt;

    while (state.accum >= frameTime)
    {
        state.accum -= frameTime;
        state.current = state.next;
        state.next = state.current + 1;

        if (state.next > state.last)
        {
            if (state.loop)
            {
                state.current = state.first;
                state.next = state.first + 1;
                if (state.next > state.last) state.next = state.first;
            }
            else
            {
                state.current = state.last;
                state.next = state.last;
                state.playing = false;
                state.accum = 0.f;
                break;
            }
        }
    }

    state.interp = glm::clamp(state.accum / frameTime, 0.f, 1.f);
    if (!state.playing) state.interp = 0.f;
}

VertexTagSocket *VertexTagBinder::addSocket(const std::string &tagName, Node3D *node,
                                             const glm::mat4 &localOffset)
{
    for (auto &s : sockets_)
    {
        if (s.tagName == tagName)
        {
            s.node = node;
            s.localOffset = localOffset;
            return &s;
        }
    }

    VertexTagSocket s;
    s.tagName = tagName;
    s.node = node;
    s.localOffset = localOffset;
    sockets_.push_back(s);
    return &sockets_.back();
}

void VertexTagBinder::removeSocket(const std::string &tagName)
{
    auto it = std::remove_if(sockets_.begin(), sockets_.end(), [&tagName](const VertexTagSocket &s) {
        return s.tagName == tagName;
    });
    sockets_.erase(it, sockets_.end());
}

void VertexTagBinder::clear()
{
    sockets_.clear();
}

void VertexTagBinder::updateSockets(const VertexAnimController &anim,
                                    const std::vector<VertexTagTrack> &tags)
{
    if (!anim.hasCurrentClip()) return;

    const VertexAnimSample sample = anim.sample();

    for (auto &s : sockets_)
    {
        if (!s.node) continue;

        const VertexTagTrack *track = findTag(tags, s.tagName);
        if (!track || track->frames.empty()) continue;

        VertexTagFrame cur = sampleTagFrame(*track, sample.currentFrame, sample.nextFrame, sample.currentInterp);

        if (sample.hasPrevious && sample.clipBlend > 0.f)
        {
            VertexTagFrame prev = sampleTagFrame(*track, sample.previousFrame, sample.previousNextFrame, sample.previousInterp);
            const float w = glm::clamp(sample.clipBlend, 0.f, 1.f);
            cur.position = glm::mix(cur.position, prev.position, w);
            cur.rotation = glm::normalize(glm::slerp(cur.rotation, prev.rotation, w));
            cur.scale = glm::mix(cur.scale, prev.scale, w);
        }

        glm::mat4 tagMat = trsMatrix(cur.position, cur.rotation, cur.scale) * s.localOffset;

        glm::vec3 scl, pos, skew;
        glm::vec4 persp;
        glm::quat rot;
        if (glm::decompose(tagMat, scl, rot, pos, skew, persp))
        {
            s.node->position = pos;
            s.node->rotation = glm::conjugate(rot);
            s.node->scale = scl;
        }
    }
}

const VertexTagTrack *VertexTagBinder::findTag(const std::vector<VertexTagTrack> &tags,
                                               const std::string &name)
{
    auto it = std::find_if(tags.begin(), tags.end(), [&name](const VertexTagTrack &t) {
        return t.name == name;
    });
    return (it != tags.end()) ? &(*it) : nullptr;
}
