#pragma once
#include "Animation.hpp"
#include "Mesh.hpp"
#include <string>
#include <vector>
#include <unordered_map>

class AnimatedMesh;

// ============================================================
//  AnimationLayer — uma "pista" de animação
//  Múltiplos layers permitem blending de partes do corpo
//  (ex: torso layer + legs layer)
// ============================================================
class AnimationLayer
{
public:
    AnimationLayer();
    ~AnimationLayer();

    // ── Carregar / adicionar animações ───────────────────────
    Animation *loadAnimation(const std::string &name, const std::string &path);
    void       addAnimation (const std::string &name, Animation *anim);
    Animation *getAnimation (const std::string &name) const;

    // ── Playback ─────────────────────────────────────────────
    void play      (const std::string &name, PlayMode mode = PlayMode::Loop, float blendTime = 0.3f);
    void crossFade (const std::string &name, float duration = 0.3f);
    void playOneShot(const std::string &name, const std::string &returnTo,
                     float blendIn = 0.2f, PlayMode returnMode = PlayMode::Loop);
    void triggerAction(const std::string &name, float blendTime = 0.2f);
    void stop      (float blendOutTime = 0.2f);
    void pause     ();
    void resume    ();

    // ── Queries ──────────────────────────────────────────────
    bool        isPlaying(const std::string &name) const;
    bool        hasFinished() const;
    float       getNormalizedTime() const;
    PlayMode    currentMode()       const { return mode_; }
    const std::string &currentName() const { return currentName_; }

    // ── Controlo de velocidade ───────────────────────────────
    void  setSpeed(float s) { speed_ = s; }
    float getSpeed()  const { return speed_; }

    // ── Update — chamado pelo Animator ───────────────────────
    // bones: vector de Bone do AnimatedMesh (para resolver índices)
    // finalMatrices: output — preenchido com as matrizes finais
    void bind  (const std::vector<Bone> &bones);
    void update(float dt, std::vector<glm::mat4> &finalMatrices,
                const std::vector<Bone> &bones);

    // Ler frame de um bone específico (útil para IK)
    bool getFrame(const std::string &animName, float time,
                  const std::string &boneName,
                  glm::vec3 &outPos, glm::quat &outRot) const;

    // Blend time default para crossfade
    float defaultBlendTime = 0.3f;
    float poseBlendTime    = 0.15f;

private:
    std::unordered_map<std::string, Animation *> anims_; // owned

    Animation  *current_  = nullptr;
    Animation  *previous_ = nullptr;
    std::string currentName_;
    std::string previousName_;

    PlayMode   mode_      = PlayMode::Loop;
    float      time_      = 0.f;  // tempo corrente (ticks)
    float      timeBlend_ = 0.f;  // tempo da animação anterior durante blend

    float speed_          = 1.f;
    bool  paused_         = false;

    // Blend state
    bool  blending_       = false;
    float blendTime_      = 0.f;
    float blendDuration_  = 0.3f;
    bool  freezeBlend_    = false; // blend estático entre duas poses

    // PingPong
    bool pingPongReverse_ = false;

    // OneShot return
    bool        shouldReturn_ = false;
    std::string returnToAnim_;
    PlayMode    returnMode_   = PlayMode::Loop;

    void updateTime(float dt);
    void applyChannel(const AnimationChannel &ch, float t,
                      std::vector<glm::mat4> &out,
                      const std::vector<Bone> &bones) const;
};

// ============================================================
//  Animator — gere layers, actualiza finalMatrices no AnimatedMesh
// ============================================================
class Animator
{
public:
    explicit Animator(AnimatedMesh *mesh);
    ~Animator();

    // ── Layers ───────────────────────────────────────────────
    AnimationLayer *addLayer();
    AnimationLayer *getLayer(int index) const;
    int             layerCount() const { return (int)layers_.size(); }

    // ── Per-frame ────────────────────────────────────────────
    void update(float dt);

    bool active = true;

private:
    AnimatedMesh                  *mesh_ = nullptr; // non-owning
    std::vector<AnimationLayer *>  layers_;         // owned
    bool                           initialized_ = false;

    void initialize();
};