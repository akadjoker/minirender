#include "pch.h"
#include "Driver.hpp"
#include "Mesh.hpp"
#include "Texture.hpp"
#include "Stream.hpp"
#include "Animation.hpp"

Animator::Animator()
{
    this->m_mesh = nullptr;
    this->m_active = true;
    m_firstStarted = false;
    meshRenderer = nullptr;
}

Animator::~Animator()
{
    for (AnimationLayer *layer : layers)
        delete layer;

    layers.clear();
    m_mesh = nullptr;
    meshRenderer = nullptr;
    m_started = false;
}

void Animator::attach()
{
    // LogInfo("[Animator] attached to %s", m_owner->getName().c_str());
    if (!m_owner->hasComponent<MeshRenderer>())
    {
        LogError("[Animator] %s has no MeshRenderer component", m_owner->getName().c_str());
        m_active = false;
        return;
    }
    meshRenderer = m_owner->getComponent<MeshRenderer>();
    m_mesh = meshRenderer->getMesh();
}
 

void Animator::update(float deltaTime)
{
    if (m_mesh == nullptr || layers.empty() || !m_active)
        return;

    if (!meshRenderer || !m_mesh || !m_mesh->HasSkeleton())
        return;
    const u32 bonesCount = m_owner->getJointCount();

    if (!m_firstStarted)
    {
        for (size_t i = 0; i < layers.size(); i++)
        {
            layers[i]->Bind(m_owner);
        }
        for (u32 i = 0; i < bonesCount; i++)
        {
            Joint3D *joint = m_owner->getJoint(i);
            joint->Reset();
        }
        m_firstStarted = true;
    }

    auto &boneMats = meshRenderer->boneMatrices;

    for (size_t i = 0; i < layers.size(); i++)
    {
        layers[i]->Update(deltaTime, m_owner->m_joints);
    }

    for (u32 i = 0; i < bonesCount; i++)
    {
        Joint3D *bone = m_owner->getJoint(i);
        boneMats[i] = bone->GetGlobalTransform() * bone->GetBindPose();
    }
    //  for (u32 i = 0; i <  bonesCount; i++)
    // {
    //      Joint3D *joint = m_owner->getJoint(i);
    //      joint->Reset();
    // }
}

AnimationLayer *Animator::AddLayer()
{

    AnimationLayer *layer = new AnimationLayer();
    layers.push_back(layer);
    return layer;
}

AnimationLayer *Animator::GetLayer(u32 index)
{
    if (index >= layers.size())
    {
        LogWarning("[Animator] Invalid layer index: %d", index);
        return nullptr;
    }
    return layers[index];
}

AnimationLayer::AnimationLayer()
    : m_currentAnim(nullptr), m_previousAnim(nullptr),
     m_playTo(nullptr), m_currentTime(0.0f), m_currentTimeBlend(0.0f), m_globalSpeed(1.0f), m_isPaused(false), m_isBlending(false), m_blendTime(0.0f), m_blendDuration(0.9f), m_shouldReturn(false), m_currentMode(PlayMode::Loop), 
     m_defaultBlendTime(0.3f), m_isPingPongReverse(false),m_freezePoseBlend(false),m_poseBlendTime(0.15f)  

{
}

AnimationLayer::~AnimationLayer()
{
    for (auto &pair : m_animations)
    {
        delete pair.second;
    }
    m_animations.clear();
}

 
void AnimationLayer::AddAnimation(const std::string &name, Animation *anim)
{
    m_animations[name] = anim;
    //   if (anim)
    //     anim->BindToMesh(m_mesh);
}

Animation *AnimationLayer::GetAnimation(const std::string &name)
{
    auto it = m_animations.find(name);
    if (it != m_animations.end())
        return it->second;
    return nullptr;
}

Animation *AnimationLayer::LoadAnimation(const std::string &name, const std::string &filename)
{
    if (GetAnimation(name))
        return nullptr;

    Animation *anim = new Animation();
    if (!anim->Load(filename))
    {
        delete anim;
        return nullptr;
    }
    anim->m_name = name;

    AddAnimation(name, anim);
    return anim;
}


void AnimationLayer::CrossFade(const std::string &toAnim, float duration)
{
    Play(toAnim, m_currentMode, duration);
}



void AnimationLayer::Pause()
{
    m_isPaused = true;
}

void AnimationLayer::Resume()
{
    m_isPaused = false;
}

 
 
void AnimationLayer::Bind(Node3D *parent)
{
    for (auto &Animation : m_animations)
    {
        Animation.second->Bind(parent);
    }
}
 
bool AnimationLayer::CheckAnimationEnd()
{
    if (!m_currentAnim)
        return false;

    float duration = m_currentAnim->GetDuration();

    switch (m_currentMode)
    {
        case PlayMode::Once:
            if (m_currentTime >= duration)
            {
                m_currentTime = duration;
                m_isPaused = true;
                return true;
            }
            break;

        case PlayMode::Backward:   
            if (m_currentTime <= 0.0f)   
            {
                m_currentTime = 0.0f;
                m_isPaused = true;
                return true;   
            }
            break;

        case PlayMode::Loop:
            if (m_currentTime >= duration)
            {
                m_currentTime = fmod(m_currentTime, duration);
            }
            break;

        case PlayMode::PingPong:
            // PingPong nunca "termina", então não retorna true
            if (m_currentTime >= duration)
            {
                m_currentTime = duration;
            }
            else if (m_currentTime < 0.0f)
            {
                m_currentTime = 0.0f;
            }
            break;
    }

    return false;
}
 
void AnimationLayer::Play(const std::string &animName, PlayMode mode, float blendTime)
{
    Animation *anim = GetAnimation(animName);
    if (!anim)
        return;

 
    if (m_currentAnimName == animName && m_currentAnim == anim)
    {
        // Se está pausada (animação terminou), reinicia
        if (m_isPaused)
        {
            if (mode == PlayMode::Backward)
                m_currentTime = anim->GetDuration();
            else
                m_currentTime = 0.0f;
            
            m_isPaused = false;
            m_isPingPongReverse = false;
            m_currentMode = mode;
            m_freezePoseBlend = false; // ✅ RESET!
            return;
        }
        return;
    }

    //  Só faz blend se temos animação diferente a decorrer
    bool needsBlend = (m_currentAnim != nullptr && 
                       m_currentAnimName != animName && 
                       blendTime > 0.0f &&
                       !m_isPaused);

    if (needsBlend)
    {
        // Guarda animação atual para blend
        m_previousAnim = m_currentAnim;
        m_previousAnimName = m_currentAnimName;
        m_currentTimeBlend = m_currentTime; // ✅ CONGELA frame atual!
        
        m_isBlending = true;
        m_blendTime = 0.0f;
        
        //   Usa tempo específico para pose blend (mais rápido!)
        m_blendDuration = m_poseBlendTime; // Rápido (0.15s default)
        m_freezePoseBlend = true;
    }
    else
    {
        m_isBlending = false;
        m_previousAnim = nullptr;
        m_freezePoseBlend = false;  
    }

    // Define nova animação
    m_currentAnim = anim;
    m_currentAnimName = animName;
    m_currentMode = mode;
    m_shouldReturn = false;
    m_isPingPongReverse = false;
    m_isPaused = false;
 
    if (mode == PlayMode::Backward)
        m_currentTime = anim->GetDuration();
    else
        m_currentTime = 0.0f;
}


void AnimationLayer::PlayOneShot(const std::string &animName, const std::string &returnTo, 
                                  float blendTime, PlayMode toMode)
{
    m_returnToAnim = returnTo;
    m_toReturnMode = toMode;
    m_shouldReturn = true;
    Play(animName, PlayMode::Once, blendTime);
}

 
void AnimationLayer::PlayAction(const std::string &actionName, float blendTime)
{
    // Guarda animação atual para voltar depois
    if (!m_currentAnimName.empty() && !m_isPaused)
    {
        PlayOneShot(actionName, m_currentAnimName, blendTime, m_currentMode);
    }
    else
    {
        // Sem animação atual, só toca a ação
        Play(actionName, PlayMode::Once, blendTime);
    }
}

// ✅  (útil para IK, ragdoll, etc)
bool AnimationLayer::GetAnimationFrame(const std::string &animName, float time, 
                                       const std::string &boneName,
                                       Vec3 &outPos, Quat &outRot)
{
    Animation *anim = GetAnimation(animName);
    if (!anim)
        return false;

    AnimationChannel *channel = anim->FindChannel(boneName);
    if (!channel)
        return false;

    outPos = anim->InterpolatePosition(*channel, time);
    outRot = anim->InterpolateRotation(*channel, time);
    return true;
}

//  Devolve progresso da animação atual (0.0 a 1.0)
float AnimationLayer::GetNormalizedTime() const
{
    if (!m_currentAnim)
        return 0.0f;
    
    float duration = m_currentAnim->GetDuration();
    if (duration <= 0.0f)
        return 0.0f;
    
    return m_currentTime / duration;
}

 
bool AnimationLayer::HasFinished() const
{
    if (!m_currentAnim || m_currentMode != PlayMode::Once)
        return false;
    
    return m_isPaused; // Once pausa quando termina
}

void AnimationLayer::Update(float deltaTime, const std::vector<Joint3D *> &bones)
{
    if (m_isPaused || !m_currentAnim)
        return;

    float dt = deltaTime * m_globalSpeed;

    // ========================================================================
    // FASE 1: BLENDING
    // ========================================================================
    if (m_isBlending && m_previousAnim)
    {
        m_blendTime += dt;
        float blend = Min(m_blendTime / m_blendDuration, 1.0f);

 
        if (m_freezePoseBlend)
        {
            
            
            for (const auto &channel : m_currentAnim->m_channels)
            {
                if (channel.boneIndex == (u32)-1)
                    continue;

                // Pose da animação nova (frame inicial, congelado)
                Vec3 pos2 = m_currentAnim->InterpolatePosition(channel, m_currentTime);
                Quat rot2 = m_currentAnim->InterpolateRotation(channel, m_currentTime);

                // Pose da animação anterior (frame onde parou, congelado)
                AnimationChannel *prevCh = m_previousAnim->FindChannel(channel.boneName);
                
                if (prevCh)
                {
                    Vec3 pos1 = m_previousAnim->InterpolatePosition(*prevCh, m_currentTimeBlend);
                    Quat rot1 = m_previousAnim->InterpolateRotation(*prevCh, m_currentTimeBlend);

                    // ✅ Interpolação PURA entre duas poses estáticas
                    Vec3 finalPos = Vec3::Lerp(pos1, pos2, blend);
                    Quat finalRot = Quat::Slerp(rot1, rot2, blend);

                    bones[channel.boneIndex]->SetAnimationFrame(finalPos, finalRot);
                }
                else
                {
                    bones[channel.boneIndex]->SetAnimationFrame(pos2, rot2);
                }
            }

          
            if (blend >= 1.0f)
            {
                m_isBlending = false;
                m_previousAnim = nullptr;
                m_blendTime = 0.0f;
                m_freezePoseBlend = false;  
                // Agora m_currentAnim começa a animar normalmente
            }

            return; // Durante pose blend, não faz mais nada
        }
        
     
        m_currentTime += dt * m_currentAnim->GetTicksPerSecond();
        m_currentTimeBlend += dt * m_previousAnim->GetTicksPerSecond();
        
        float duration = m_currentAnim->GetDuration();
        if (m_currentMode == PlayMode::Loop && m_currentTime >= duration)
            m_currentTime = fmod(m_currentTime, duration);
        else if (m_currentTime >= duration)
            m_currentTime = duration;

        float prevDuration = m_previousAnim->GetDuration();
        if (m_currentTimeBlend >= prevDuration)
            m_currentTimeBlend = fmod(m_currentTimeBlend, prevDuration);

        for (const auto &channel : m_currentAnim->m_channels)
        {
            if (channel.boneIndex == (u32)-1)
                continue;

            Vec3 pos2 = m_currentAnim->InterpolatePosition(channel, m_currentTime);
            Quat rot2 = m_currentAnim->InterpolateRotation(channel, m_currentTime);

            AnimationChannel *prevCh = m_previousAnim->FindChannel(channel.boneName);
            
            if (prevCh)
            {
                Vec3 pos1 = m_previousAnim->InterpolatePosition(*prevCh, m_currentTimeBlend);
                Quat rot1 = m_previousAnim->InterpolateRotation(*prevCh, m_currentTimeBlend);

                Vec3 finalPos = Vec3::Lerp(pos1, pos2, blend);
                Quat finalRot = Quat::Slerp(rot1, rot2, blend);

                bones[channel.boneIndex]->SetAnimationFrame(finalPos, finalRot);
            }
            else
            {
                bones[channel.boneIndex]->SetAnimationFrame(pos2, rot2);
            }
        }

        if (blend >= 1.0f)
        {
            m_isBlending = false;
            m_previousAnim = nullptr;
            m_blendTime = 0.0f;
        }

        return;
    }

    // ========================================================================
    // FASE 2: PLAYBACK NORMAL (sem blend)
    // ========================================================================
    
    float duration = m_currentAnim->GetDuration();
    bool animationEnded = false;

    // Update do tempo baseado no modo
    switch (m_currentMode)
    {
        case PlayMode::Once:
        {
            m_currentTime += dt * m_currentAnim->GetTicksPerSecond();
            if (m_currentTime >= duration)
            {
                m_currentTime = duration;
                animationEnded = true;
                m_isPaused = true;  
            }
            break;
        }

        case PlayMode::Backward:   
        {
            m_currentTime -= dt * m_currentAnim->GetTicksPerSecond();
            if (m_currentTime <= 0.0f)
            {
                m_currentTime = 0.0f;
                animationEnded = true;
                m_isPaused = true;
            }
            break;
        }

        case PlayMode::Loop:
        {
            m_currentTime += dt * m_currentAnim->GetTicksPerSecond();
            while (m_currentTime >= duration)
                m_currentTime -= duration;
            break;
        }

        case PlayMode::PingPong:
        {
            if (!m_isPingPongReverse)
            {
                m_currentTime += dt * m_currentAnim->GetTicksPerSecond();
                if (m_currentTime >= duration)
                {
                    m_currentTime = duration;
                    m_isPingPongReverse = true;
                }
            }
            else
            {
                m_currentTime -= dt * m_currentAnim->GetTicksPerSecond();
                if (m_currentTime <= 0.0f)
                {
                    m_currentTime = 0.0f;
                    m_isPingPongReverse = false;
                }
            }
            break;
        }
    }

   
    for (const auto &channel : m_currentAnim->m_channels)
    {
        if (channel.boneIndex == (u32)-1)
            continue;

        Vec3 pos = m_currentAnim->InterpolatePosition(channel, m_currentTime);
        Quat rot = m_currentAnim->InterpolateRotation(channel, m_currentTime);
        bones[channel.boneIndex]->SetAnimationFrame(pos, rot);
    }

    // ========================================================================
    // FASE 3: HANDLE ANIMATION END (OneShot return)
    // ========================================================================
    
    if (animationEnded && m_shouldReturn && !m_returnToAnim.empty())
    {
        m_shouldReturn = false;
        Play(m_returnToAnim, m_toReturnMode, m_defaultBlendTime);
    }
}

// ============================================================================
// UTILITIES
// ============================================================================

void AnimationLayer::Stop(float blendOutTime)
{
    if (blendOutTime > 0.0f && m_currentAnim)
    {
        m_previousAnim = m_currentAnim;
        m_previousAnimName = m_currentAnimName;
        m_currentTimeBlend = m_currentTime;
        
        m_currentAnim = nullptr;
        m_currentAnimName = "";
        m_currentTime = 0.0f;

        m_isBlending = true;
        m_blendTime = 0.0f;
        m_blendDuration = blendOutTime;
        m_freezePoseBlend = false;  
    }
    else
    {
        m_currentAnim = nullptr;
        m_currentAnimName = "";
        m_currentTime = 0.0f;
        m_isPaused = false;
        m_isBlending = false;
        m_freezePoseBlend = false;  
    }
}

bool AnimationLayer::IsPlaying(const std::string &animName) const
{
    return (m_currentAnimName == animName && m_currentAnim != nullptr && !m_isPaused);
}