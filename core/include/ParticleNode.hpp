#pragma once
#include "Node.hpp"
#include "Mesh.hpp"
#include "RenderPipeline.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <random>

// ============================================================
//  Enums
// ============================================================

enum class EmissionMode
{
    Continuous, // Constant stream (fire, smoke, engine)
    Burst,      // Periodic burst (explosion)
    OneShot,    // Fires once then stops (sparks, hit)
    Pulse       // Periodic pulses (flicker, intermittent)
};

enum class EmitterShape
{
    Point,
    Sphere,
    Box,
    Cone,
    Circle,
    Ring
};

enum class EmitterState
{
    Stopped,
    Playing,
    Paused
};

// ============================================================
//  Particle
// ============================================================

struct Particle
{
    glm::vec3 position    = {0, 0, 0};
    glm::vec3 velocity    = {0, 0, 0};
    glm::vec3 acceleration= {0, 0, 0};
    glm::vec4 color       = {1, 1, 1, 1};
    glm::vec2 size        = {1, 1};
    float     rotation    = 0.f;
    float     rotationSpeed = 0.f;
    glm::vec4 texRect     = {0, 0, 1, 1}; // atlas region (x, y, w, h)
    float     lifetime    = 1.f;
    float     timeAlive   = 0.f;
    bool      active      = false;
    glm::vec4 colorStart  = {1, 1, 1, 1};
    glm::vec4 colorEnd    = {1, 1, 1, 0};
    glm::vec2 sizeStart   = {1, 1};
    glm::vec2 sizeEnd     = {0.5f, 0.5f};
};

// ============================================================
//  ParticleAffector base + concrete affectors
// ============================================================

class ParticleAffector
{
public:
    bool enabled = true;
    virtual ~ParticleAffector() = default;
    virtual void apply(Particle &p, float dt) = 0;
};

class GravityAffector : public ParticleAffector
{
public:
    glm::vec3 gravity;
    explicit GravityAffector(const glm::vec3 &g) : gravity(g) {}
    void apply(Particle &p, float dt) override;
};

class DragAffector : public ParticleAffector
{
public:
    float drag;
    explicit DragAffector(float d) : drag(d) {}
    void apply(Particle &p, float dt) override;
};

class VortexAffector : public ParticleAffector
{
public:
    glm::vec3 center;
    float strength, radius;
    VortexAffector(const glm::vec3 &c, float s, float r)
        : center(c), strength(s), radius(r) {}
    void apply(Particle &p, float dt) override;
};

class AttractorAffector : public ParticleAffector
{
public:
    glm::vec3 position;
    float strength, radius;
    bool  repulse;
    AttractorAffector(const glm::vec3 &pos, float s, float r, bool rep = false)
        : position(pos), strength(s), radius(r), repulse(rep) {}
    void apply(Particle &p, float dt) override;
};

class TurbulenceAffector : public ParticleAffector
{
public:
    float strength, frequency, time = 0.f;
    TurbulenceAffector(float s, float f) : strength(s), frequency(f) {}
    void apply(Particle &p, float dt) override;
};

class ColorOverLifetimeAffector : public ParticleAffector
{
public:
    glm::vec4 startColor, endColor;
    ColorOverLifetimeAffector(const glm::vec4 &s, const glm::vec4 &e)
        : startColor(s), endColor(e) {}
    void apply(Particle &p, float dt) override;
};

class SizeOverLifetimeAffector : public ParticleAffector
{
public:
    glm::vec2 startSize, endSize;
    SizeOverLifetimeAffector(const glm::vec2 &s, const glm::vec2 &e)
        : startSize(s), endSize(e) {}
    void apply(Particle &p, float dt) override;
};

// ============================================================
//  ParticleSystemNode
// ============================================================

class ParticleSystemNode : public Node3D
{
public:
    // ── Construction ──────────────────────────────────────────
    explicit ParticleSystemNode(const std::string &name, int maxParticles = 1000);
    ~ParticleSystemNode() override;

    // ── Node overrides ────────────────────────────────────────
    void update(float dt)                                           override;
    void gatherRenderItems(RenderQueue &queue, const FrameContext &ctx) override;

    // ── Material ─────────────────────────────────────────────
    void      setMaterial(Material *mat) { m_material = mat; }
    Material *getMaterial() const        { return m_material; }

    // ── Emission mode ─────────────────────────────────────────
    ParticleSystemNode *setEmissionMode(EmissionMode mode);
    ParticleSystemNode *setContinuous(float particlesPerSecond);
    ParticleSystemNode *setBurst(int count, float interval = 1.f);
    ParticleSystemNode *setOneShot(int count);
    ParticleSystemNode *setPulse(float pulsesPerSecond, int particlesPerPulse);

    // ── Emitter shape ─────────────────────────────────────────
    ParticleSystemNode *setShapePoint();
    ParticleSystemNode *setShapeSphere(float radius);
    ParticleSystemNode *setShapeBox(const glm::vec3 &size);
    ParticleSystemNode *setShapeCone(float angleDegrees, float baseRadius = 0.f);
    ParticleSystemNode *setShapeCircle(float radius);
    ParticleSystemNode *setShapeRing(float outerRadius, float innerRadius);

    // ── Offset / direction ────────────────────────────────────
    ParticleSystemNode *setEmissionOffset(const glm::vec3 &offset);
    ParticleSystemNode *setEmissionDirection(const glm::vec3 &dir);
    ParticleSystemNode *setSpreadAngle(float degrees);

    glm::vec3 getWorldEmissionPosition() const;
    glm::vec3 getWorldEmissionDirection() const;

    // ── Particle properties ───────────────────────────────────
    ParticleSystemNode *setLifetime(float min, float max);
    ParticleSystemNode *setLifetime(float t);
    ParticleSystemNode *setSpeed(float min, float max);
    ParticleSystemNode *setSpeed(float s);
    ParticleSystemNode *setSize(const glm::vec2 &start, const glm::vec2 &end);
    ParticleSystemNode *setSize(float s);
    ParticleSystemNode *setSize(float startS, float endS);
    ParticleSystemNode *setColor(const glm::vec4 &start, const glm::vec4 &end);
    ParticleSystemNode *setColor(const glm::vec4 &c);
    ParticleSystemNode *setRotationSpeed(float min, float max);
    ParticleSystemNode *setRotationSpeed(float s);

    // ── Physics ───────────────────────────────────────────────
    ParticleSystemNode *setGravity(const glm::vec3 &g);
    ParticleSystemNode *setDrag(float d);

    // ── Duration / loop ───────────────────────────────────────
    ParticleSystemNode *setDuration(float seconds);
    ParticleSystemNode *setLoop(bool loop);
    ParticleSystemNode *setAutoPlay(bool autoPlay);

    // ── Texture atlas (glm::vec4 = x,y,width,height in UV space) ──
    ParticleSystemNode *setAtlasFrames(const std::vector<glm::vec4> &frames);
    ParticleSystemNode *addAtlasFrame(const glm::vec4 &frame);
    ParticleSystemNode *addAtlasFrame(float x, float y, float w, float h);
    ParticleSystemNode *setAtlasGrid(int columns, int rows);
    ParticleSystemNode *setFrame(int frameIndex);
    void                clearAtlas();

    // ── Playback control ──────────────────────────────────────
    void play();
    void stop();
    void pause();
    void reset();
    void fire(int count = -1);
    void emitBurst(int count);

    EmitterState getState()    const { return m_state; }
    bool         isPlaying()   const { return m_state == EmitterState::Playing; }
    int          activeCount() const { return m_activeCount; }
    int          maxParticles()const { return m_maxParticles; }

    // ── Affectors ─────────────────────────────────────────────
    ParticleSystemNode *addGravity(const glm::vec3 &g);
    ParticleSystemNode *addDrag(float strength);
    ParticleSystemNode *addVortex(const glm::vec3 &center, float strength, float radius);
    ParticleSystemNode *addAttractor(const glm::vec3 &pos, float strength, float radius,
                                      bool repulse = false);
    ParticleSystemNode *addTurbulence(float strength, float frequency);
    ParticleSystemNode *addColorOverLifetime(const glm::vec4 &start, const glm::vec4 &end);
    ParticleSystemNode *addSizeOverLifetime(const glm::vec2 &start, const glm::vec2 &end);
    void                clearAffectors();

private:
    // ── Simulation helpers ────────────────────────────────────
    void      emitContinuous(float dt);
    void      emitBurstMode(float dt);
    void      emitPulseMode(float dt);
    Particle *getFreeParticle();
    void      initParticle(Particle *p);
    glm::vec3 calcEmissionPosition();
    glm::vec3 calcEmissionVelocity();
    void      updateParticles(float dt);
    void      applyAffectors(float dt);
    void      buildBillboards(const glm::vec3 &camRight, const glm::vec3 &camUp);

    // ── Helpers ───────────────────────────────────────────────
    float     rnd(float minV, float maxV);
    glm::vec3 randomUnitVector();

    // ── State ─────────────────────────────────────────────────
    std::vector<Particle>         m_particles;
    std::vector<ParticleAffector*>m_affectors;
    ParticleBuffer                m_buffer;
    Material                     *m_material    = nullptr;

    EmissionMode  m_emissionMode  = EmissionMode::Continuous;
    EmitterShape  m_emitterShape  = EmitterShape::Point;
    EmitterState  m_state         = EmitterState::Stopped;

    glm::vec3 m_emissionOffset    = {0, 0, 0};
    glm::vec3 m_emissionDirection = {0, 1, 0};

    float m_emissionRate          = 10.f;
    float m_burstInterval         = 1.f;
    int   m_burstCount            = 10;
    int   m_oneShotCount          = 10;
    float m_pulseRate             = 1.f;
    int   m_particlesPerPulse     = 5;

    std::vector<glm::vec4> m_atlasFrames; // (x, y, w, h) in UV space
    bool  m_useAtlas               = false;
    int   m_currentFrame           = 0;

    // Shape params
    float     m_radius      = 1.f;
    float     m_innerRadius = 0.5f;
    float     m_coneAngle   = 0.785398f; // 45 deg in rad
    glm::vec3 m_boxSize     = {1, 1, 1};

    // Particle properties
    float     m_lifetimeMin        = 1.f,  m_lifetimeMax        = 3.f;
    float     m_speedMin           = 1.f,  m_speedMax           = 5.f;
    glm::vec2 m_sizeStart          = {0.5f, 0.5f};
    glm::vec2 m_sizeEnd            = {0.1f, 0.1f};
    glm::vec4 m_colorStart         = {1, 1, 1, 1};
    glm::vec4 m_colorEnd           = {1, 1, 1, 0};
    float     m_rotSpeedMin        = 0.f,  m_rotSpeedMax        = 0.f;
    float     m_spreadAngle        = 0.f;

    // Physics
    glm::vec3 m_gravity  = {0, 0, 0};
    float     m_drag     = 0.f;

    // Control
    int   m_maxParticles       = 1000;
    int   m_activeCount        = 0;
    float m_duration           = -1.f;
    bool  m_loop               = true;
    bool  m_autoPlay           = false;

    // Timers
    float m_emissionTimer      = 0.f;
    float m_timeAlive          = 0.f;
    float m_burstTimer         = 0.f;
    float m_pulseTimer         = 0.f;
    bool  m_hasEmittedOneShot  = false;

    // RNG
    std::mt19937                          m_rng;
    std::uniform_real_distribution<float> m_dist01{0.f, 1.f};
};
