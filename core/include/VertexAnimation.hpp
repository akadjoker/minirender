#pragma once

#include "Animation.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

class Node3D;
class Mesh;

struct VertexAnimClip
{
    std::string name;
    int first = 0;
    int last = 0;
    float fps = 8.f;
    bool loop = true;

    VertexAnimClip() = default;
    VertexAnimClip(const std::string &n, int f0, int f1, float f, bool l);
};

struct VertexTagFrame
{
    glm::vec3 position = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 scale = glm::vec3(1.f);
};

struct VertexTagTrack
{
    std::string name;
    std::vector<VertexTagFrame> frames;
};

// Snapshot used by render/evaluation code to blend both frame-interpolation
// and clip-to-clip transition (crossfade).
struct VertexAnimSample
{
    int currentFrame = 0;
    int nextFrame = 0;
    float currentInterp = 0.f;

    int previousFrame = 0;
    int previousNextFrame = 0;
    float previousInterp = 0.f;

    bool hasPrevious = false;
    float clipBlend = 0.f; // 0=current only, 1=previous only
};

// Shared immutable animation asset for vertex-animated meshes.
// Data is reusable across many nodes; each node keeps its own controller/state.
struct VertexAnimAsset
{
    typedef void (*ApplySampleFn)(Mesh *mesh, const VertexAnimSample &sample, const void *userData);

    Mesh *templateMesh = nullptr; // non-owning
    std::vector<VertexAnimClip> clips;
    std::vector<VertexTagTrack> tags;
    ApplySampleFn applySample = nullptr;
    const void *applyUserData = nullptr;

    bool valid() const;
    void apply(Mesh *mesh, const VertexAnimSample &sample) const;
};

class VertexAnimController
{
public:
    void addClip(const VertexAnimClip &clip);
    const VertexAnimClip *findClip(const std::string &name) const;

    bool play(const std::string &name, float blendTime = 0.12f, bool restartSame = false);
    void stop();
    void update(float dt);

    bool hasCurrentClip() const;
    const VertexAnimClip *currentClip() const;
    const VertexAnimClip *previousClip() const;

    bool isTransitioning() const;
    VertexAnimSample sample() const;

private:
    struct ClipState
    {
        int first = 0;
        int last = 0;
        float fps = 8.f;
        bool loop = true;

        int current = 0;
        int next = 0;
        float interp = 0.f;
        float accum = 0.f;
        bool playing = true;
    };

    std::vector<VertexAnimClip> clips_;

    const VertexAnimClip *currentClip_ = nullptr;
    const VertexAnimClip *previousClip_ = nullptr;
    ClipState currentState_;
    ClipState previousState_;

    bool transitionActive_ = false;
    float transitionTime_ = 0.f;
    float transitionDuration_ = 0.12f;

    static void resetStateFromClip(ClipState &state, const VertexAnimClip &clip);
    static void stepState(ClipState &state, float dt);
};

struct VertexTagSocket
{
    std::string tagName;
    Node3D *node = nullptr;
    glm::mat4 localOffset = glm::mat4(1.f);
};

class VertexTagBinder
{
public:
    VertexTagSocket *addSocket(const std::string &tagName, Node3D *node,
                               const glm::mat4 &localOffset = glm::mat4(1.f));
    void removeSocket(const std::string &tagName);
    void clear();

    void updateSockets(const VertexAnimController &anim,
                       const std::vector<VertexTagTrack> &tags);

private:
    std::vector<VertexTagSocket> sockets_;

    static const VertexTagTrack *findTag(const std::vector<VertexTagTrack> &tags,
                                         const std::string &name);
};
