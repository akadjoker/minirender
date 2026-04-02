#include "ParticleNode.hpp"
#include "Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cassert>

static constexpr float PI    = 3.14159265358979323846f;
static constexpr float TWO_PI = PI * 2.f;

// ============================================================
//  Affector implementations
// ============================================================

void GravityAffector::apply(Particle &p, float /*dt*/)
{
    if (enabled)
        p.acceleration += gravity;
}

void DragAffector::apply(Particle &p, float dt)
{
    if (enabled)
        p.velocity *= (1.f - drag * dt);
}

void VortexAffector::apply(Particle &p, float /*dt*/)
{
    if (!enabled) return;
    glm::vec3 toCenter = center - p.position;
    float dist = glm::length(toCenter);
    if (dist < radius && dist > 0.001f)
    {
        glm::vec3 dir     = toCenter / dist;
        glm::vec3 tangent = {-dir.z, 0.f, dir.x};
        float falloff     = 1.f - dist / radius;
        p.acceleration   += tangent * strength * falloff;
    }
}

void AttractorAffector::apply(Particle &p, float /*dt*/)
{
    if (!enabled) return;
    glm::vec3 toAtt = position - p.position;
    float dist = glm::length(toAtt);
    if (dist < radius && dist > 0.001f)
    {
        glm::vec3 dir = toAtt / dist;
        float falloff = 1.f - dist / radius;
        float force   = strength * falloff * (repulse ? -1.f : 1.f);
        p.acceleration += dir * force;
    }
}

void TurbulenceAffector::apply(Particle &p, float dt)
{
    if (!enabled) return;
    time += dt;
    float nx = sinf(p.position.x * frequency + time)       * strength;
    float ny = sinf(p.position.y * frequency + time * 1.3f)* strength;
    float nz = cosf(p.position.z * frequency + time * 0.7f)* strength;
    p.acceleration += glm::vec3(nx, ny, nz);
}

void ColorOverLifetimeAffector::apply(Particle &p, float /*dt*/)
{
    if (!enabled) return;
    float t = p.timeAlive / p.lifetime;
    p.color = glm::mix(startColor, endColor, t);
}

void SizeOverLifetimeAffector::apply(Particle &p, float /*dt*/)
{
    if (!enabled) return;
    float t  = p.timeAlive / p.lifetime;
    p.size   = glm::mix(startSize, endSize, t);
}

// ============================================================
//  ParticleSystemNode — construction / destruction
// ============================================================

ParticleSystemNode::ParticleSystemNode(const std::string &nodeName, int maxPart)
    : m_maxParticles(maxPart)
    , m_rng(std::random_device{}())
{
    name = nodeName;

    m_particles.resize(maxPart);
    for (auto &p : m_particles) p.active = false;

    m_buffer.allocate(maxPart);
}

ParticleSystemNode::~ParticleSystemNode()
{
    clearAffectors();
}

// ============================================================
//  Playback control
// ============================================================

void ParticleSystemNode::play()
{
    m_state = EmitterState::Playing;
    if (m_emissionMode == EmissionMode::OneShot && !m_hasEmittedOneShot)
    {
        emitBurst(m_oneShotCount);
        m_hasEmittedOneShot = true;
    }
}

void ParticleSystemNode::stop()
{
    m_state             = EmitterState::Stopped;
    m_emissionTimer     = 0.f;
    m_timeAlive         = 0.f;
    m_burstTimer        = 0.f;
    m_pulseTimer        = 0.f;
    m_hasEmittedOneShot = false;
}

void ParticleSystemNode::pause()  { m_state = EmitterState::Paused; }

void ParticleSystemNode::reset()
{
    stop();
    for (auto &p : m_particles) p.active = false;
    m_activeCount = 0;
}

void ParticleSystemNode::fire(int count)
{
    emitBurst(count < 0 ? m_oneShotCount : count);
}

void ParticleSystemNode::emitBurst(int count)
{
    for (int i = 0; i < count; ++i)
    {
        Particle *p = getFreeParticle();
        if (p) initParticle(p);
    }
}

// ============================================================
//  Node overrides
// ============================================================

void ParticleSystemNode::update(float dt)
{
    if (m_state == EmitterState::Paused) return;

    m_timeAlive += dt;

    // Emit new particles
    if (m_state == EmitterState::Playing)
    {
        switch (m_emissionMode)
        {
            case EmissionMode::Continuous: emitContinuous(dt); break;
            case EmissionMode::Burst:      emitBurstMode(dt);  break;
            case EmissionMode::Pulse:      emitPulseMode(dt);  break;
            case EmissionMode::OneShot:    break; // emitted in play()
        }

        // Duration check
        if (m_duration > 0.f && m_timeAlive >= m_duration)
        {
            if (m_loop)
            {
                m_timeAlive         = 0.f;
                m_hasEmittedOneShot = false;
            }
            else
            {
                stop();
            }
        }
    }

    updateParticles(dt);
    applyAffectors(dt);

    // Count active
    m_activeCount = 0;
    for (auto &p : m_particles)
        if (p.active) ++m_activeCount;
}

void ParticleSystemNode::gatherRenderItems(RenderQueue &queue, const FrameContext &ctx)
{
    if (!m_material || !ctx.camera || m_activeCount == 0) return;

    // Extract camera right/up from view matrix rows (column-major GLM: [col][row])
    const glm::mat4 &view = ctx.camera->view;
    glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    glm::vec3 camUp   (view[0][1], view[1][1], view[2][1]);

    buildBillboards(camRight, camUp);
    m_buffer.uploadVertices(m_activeCount);

    RenderItem item;
    item.drawable   = &m_buffer;
    item.material   = m_material;
    item.model      = glm::mat4(1.f); // particles are already in world space
    item.indexStart = 0;
    item.indexCount = m_activeCount * 6;
    item.passMask   = RenderPassMask::Transparent;

    queue.add(item);
}

// ============================================================
//  Emission
// ============================================================

void ParticleSystemNode::emitContinuous(float dt)
{
    m_emissionTimer += dt;
    float interval   = 1.f / m_emissionRate;
    int   count      = 0;

    while (m_emissionTimer >= interval)
    {
        ++count;
        m_emissionTimer -= interval;
        if (count > 100) { m_emissionTimer = 0.f; break; } // spike guard
    }
    for (int i = 0; i < count; ++i)
    {
        Particle *p = getFreeParticle();
        if (p) initParticle(p);
    }
}

void ParticleSystemNode::emitBurstMode(float dt)
{
    m_burstTimer += dt;
    if (m_burstTimer >= m_burstInterval)
    {
        emitBurst(m_burstCount);
        m_burstTimer = 0.f;
        if (!m_loop) stop();
    }
}

void ParticleSystemNode::emitPulseMode(float dt)
{
    m_pulseTimer += dt;
    float interval = 1.f / m_pulseRate;
    if (m_pulseTimer >= interval)
    {
        emitBurst(m_particlesPerPulse);
        m_pulseTimer = 0.f;
    }
}

// ============================================================
//  Particle management
// ============================================================

Particle *ParticleSystemNode::getFreeParticle()
{
    // First pass: find inactive slot
    for (auto &p : m_particles)
        if (!p.active) return &p;

    // Second pass: recycle particles that have lived ≥90%
    Particle *oldest   = nullptr;
    float      maxProg = 0.9f;
    for (auto &p : m_particles)
    {
        float prog = p.timeAlive / p.lifetime;
        if (prog > maxProg) { maxProg = prog; oldest = &p; }
    }
    return oldest; // may be nullptr if pool is saturated
}

void ParticleSystemNode::initParticle(Particle *p)
{
    if (!p) return;

    p->position     = calcEmissionPosition();
    p->velocity     = calcEmissionVelocity();
    p->acceleration = m_gravity;
    p->lifetime     = rnd(m_lifetimeMin, m_lifetimeMax);
    p->timeAlive    = 0.f;
    p->sizeStart    = m_sizeStart;
    p->sizeEnd      = m_sizeEnd;
    p->size         = m_sizeStart;
    p->colorStart   = m_colorStart;
    p->colorEnd     = m_colorEnd;
    p->color        = m_colorStart;
    p->rotation     = rnd(0.f, TWO_PI);
    p->rotationSpeed= rnd(m_rotSpeedMin, m_rotSpeedMax);

    if (m_useAtlas && !m_atlasFrames.empty())
        p->texRect = m_atlasFrames[m_currentFrame % (int)m_atlasFrames.size()];
    else
        p->texRect = {0.f, 0.f, 1.f, 1.f};

    p->active = true;
}

// ============================================================
//  Emission position / velocity
// ============================================================

glm::vec3 ParticleSystemNode::calcEmissionPosition()
{
    const glm::mat4 world = worldMatrix();
    glm::vec3 wp = glm::vec3(world * glm::vec4(m_emissionOffset, 1.f));
    glm::vec3 off = {0, 0, 0};

    switch (m_emitterShape)
    {
        case EmitterShape::Point:
            break;

        case EmitterShape::Sphere:
        {
            glm::vec3 dir = randomUnitVector();
            float r = std::cbrt(m_dist01(m_rng)) * m_radius;
            off = dir * r;
            break;
        }

        case EmitterShape::Box:
        {
            off = {rnd(-m_boxSize.x * .5f, m_boxSize.x * .5f),
                   rnd(-m_boxSize.y * .5f, m_boxSize.y * .5f),
                   rnd(-m_boxSize.z * .5f, m_boxSize.z * .5f)};
            break;
        }

        case EmitterShape::Cone:
        {
            if (m_radius > 0.f)
            {
                float a = rnd(0.f, TWO_PI);
                float r = std::sqrt(m_dist01(m_rng)) * m_radius;
                glm::vec3 r3 = right(), u3 = up();
                off = r3 * (std::cos(a) * r) + u3 * (std::sin(a) * r);
            }
            break;
        }

        case EmitterShape::Circle:
        {
            float a = rnd(0.f, TWO_PI);
            float r = std::sqrt(m_dist01(m_rng)) * m_radius;
            glm::vec3 r3 = right(), u3 = up();
            off = r3 * (std::cos(a) * r) + u3 * (std::sin(a) * r);
            break;
        }

        case EmitterShape::Ring:
        {
            float a = rnd(0.f, TWO_PI);
            float r = rnd(m_innerRadius, m_radius);
            glm::vec3 r3 = right(), u3 = up();
            off = r3 * (std::cos(a) * r) + u3 * (std::sin(a) * r);
            break;
        }
    }

    if (glm::dot(off, off) > 0.0001f)
        wp += glm::vec3(world * glm::vec4(off, 0.f));

    return wp;
}

glm::vec3 ParticleSystemNode::calcEmissionVelocity()
{
    float speed = rnd(m_speedMin, m_speedMax);
    glm::vec3 dir = getWorldEmissionDirection();

    if (m_spreadAngle > 0.f)
    {
        float theta = rnd(0.f, TWO_PI);
        float phi   = rnd(0.f, m_spreadAngle);

        glm::vec3 u = {0, 1, 0};
        if (std::abs(glm::dot(dir, u)) > 0.99f) u = {1, 0, 0};
        glm::vec3 r2 = glm::normalize(glm::cross(dir, u));
        u            = glm::normalize(glm::cross(r2, dir));

        dir = dir * std::cos(phi)
            + (r2 * std::cos(theta) + u * std::sin(theta)) * std::sin(phi);
        dir = glm::normalize(dir);
    }

    return dir * speed;
}

// ============================================================
//  Update / Affectors
// ============================================================

void ParticleSystemNode::updateParticles(float dt)
{
    for (auto &p : m_particles)
    {
        if (!p.active) continue;

        p.timeAlive += dt;
        if (p.timeAlive >= p.lifetime) { p.active = false; continue; }

        p.velocity   += p.acceleration * dt;
        if (m_drag > 0.f) p.velocity *= (1.f - m_drag * dt);
        p.position   += p.velocity * dt;
        p.rotation   += p.rotationSpeed * dt;

        float t = p.timeAlive / p.lifetime;
        p.color = glm::mix(p.colorStart, p.colorEnd, t);
        p.size  = glm::mix(p.sizeStart,  p.sizeEnd,  t);

        p.acceleration = m_gravity; // reset; affectors add on top each frame
    }
}

void ParticleSystemNode::applyAffectors(float dt)
{
    if (m_affectors.empty()) return;
    for (auto &p : m_particles)
    {
        if (!p.active) continue;
        for (auto *aff : m_affectors)
            if (aff && aff->enabled) aff->apply(p, dt);
    }
}

// ============================================================
//  Billboard geometry building
// ============================================================

void ParticleSystemNode::buildBillboards(const glm::vec3 &camRight,
                                          const glm::vec3 &camUp)
{
    int quadIdx = 0;
    auto *verts = m_buffer.vertices.data();

    for (const auto &p : m_particles)
    {
        if (!p.active) continue;

        glm::vec3 pRight = camRight;
        glm::vec3 pUp    = camUp;

        if (p.rotation != 0.f)
        {
            float c = std::cos(p.rotation);
            float s = std::sin(p.rotation);
            glm::vec3 nr = pRight * c - pUp * s;
            glm::vec3 nu = pRight * s + pUp * c;
            pRight = nr; pUp = nu;
        }

        glm::vec3 hr = pRight * (p.size.x * .5f);
        glm::vec3 hu = pUp    * (p.size.y * .5f);

        float u0 = p.texRect.x;
        float v0 = p.texRect.y;
        float u1 = p.texRect.x + p.texRect.z;
        float v1 = p.texRect.y + p.texRect.w;

        int base = quadIdx * 4;
        verts[base + 0] = {p.position - hr - hu, {u0, v0}, p.color};
        verts[base + 1] = {p.position + hr - hu, {u1, v0}, p.color};
        verts[base + 2] = {p.position + hr + hu, {u1, v1}, p.color};
        verts[base + 3] = {p.position - hr + hu, {u0, v1}, p.color};
        ++quadIdx;
    }
}

// ============================================================
//  Helpers
// ============================================================

float ParticleSystemNode::rnd(float minV, float maxV)
{
    return minV + m_dist01(m_rng) * (maxV - minV);
}

glm::vec3 ParticleSystemNode::randomUnitVector()
{
    float z = rnd(-1.f, 1.f);
    float a = rnd(0.f, TWO_PI);
    float r = std::sqrt(1.f - z * z);
    return {r * std::cos(a), r * std::sin(a), z};
}

glm::vec3 ParticleSystemNode::getWorldEmissionPosition() const
{
    return glm::vec3(worldMatrix() * glm::vec4(m_emissionOffset, 1.f));
}

glm::vec3 ParticleSystemNode::getWorldEmissionDirection() const
{
    return glm::normalize(glm::vec3(worldMatrix() * glm::vec4(m_emissionDirection, 0.f)));
}

// ============================================================
//  Setters — emission mode
// ============================================================

ParticleSystemNode *ParticleSystemNode::setEmissionMode(EmissionMode mode)
    { m_emissionMode = mode; return this; }

ParticleSystemNode *ParticleSystemNode::setContinuous(float pps)
    { m_emissionMode = EmissionMode::Continuous; m_emissionRate = pps; return this; }

ParticleSystemNode *ParticleSystemNode::setBurst(int count, float interval)
    { m_emissionMode = EmissionMode::Burst; m_burstCount = count; m_burstInterval = interval; return this; }

ParticleSystemNode *ParticleSystemNode::setOneShot(int count)
    { m_emissionMode = EmissionMode::OneShot; m_oneShotCount = count; return this; }

ParticleSystemNode *ParticleSystemNode::setPulse(float pps, int perPulse)
    { m_emissionMode = EmissionMode::Pulse; m_pulseRate = pps; m_particlesPerPulse = perPulse; return this; }

// ============================================================
//  Setters — emitter shape
// ============================================================

ParticleSystemNode *ParticleSystemNode::setShapePoint()
    { m_emitterShape = EmitterShape::Point; return this; }

ParticleSystemNode *ParticleSystemNode::setShapeSphere(float radius)
    { m_emitterShape = EmitterShape::Sphere; m_radius = radius; return this; }

ParticleSystemNode *ParticleSystemNode::setShapeBox(const glm::vec3 &sz)
    { m_emitterShape = EmitterShape::Box; m_boxSize = sz; return this; }

ParticleSystemNode *ParticleSystemNode::setShapeCone(float deg, float baseRadius)
{
    m_emitterShape = EmitterShape::Cone;
    m_coneAngle    = deg * (PI / 180.f);
    m_radius       = baseRadius;
    return this;
}

ParticleSystemNode *ParticleSystemNode::setShapeCircle(float radius)
    { m_emitterShape = EmitterShape::Circle; m_radius = radius; return this; }

ParticleSystemNode *ParticleSystemNode::setShapeRing(float outer, float inner)
    { m_emitterShape = EmitterShape::Ring; m_radius = outer; m_innerRadius = inner; return this; }

// ============================================================
//  Setters — offset / direction
// ============================================================

ParticleSystemNode *ParticleSystemNode::setEmissionOffset(const glm::vec3 &o)
    { m_emissionOffset = o; return this; }

ParticleSystemNode *ParticleSystemNode::setEmissionDirection(const glm::vec3 &d)
    { m_emissionDirection = glm::normalize(d); return this; }

ParticleSystemNode *ParticleSystemNode::setSpreadAngle(float deg)
    { m_spreadAngle = deg * (PI / 180.f); return this; }

// ============================================================
//  Setters — particle properties
// ============================================================

ParticleSystemNode *ParticleSystemNode::setLifetime(float mn, float mx)
    { m_lifetimeMin = mn; m_lifetimeMax = mx; return this; }

ParticleSystemNode *ParticleSystemNode::setLifetime(float t)
    { m_lifetimeMin = m_lifetimeMax = t; return this; }

ParticleSystemNode *ParticleSystemNode::setSpeed(float mn, float mx)
    { m_speedMin = mn; m_speedMax = mx; return this; }

ParticleSystemNode *ParticleSystemNode::setSpeed(float s)
    { m_speedMin = m_speedMax = s; return this; }

ParticleSystemNode *ParticleSystemNode::setSize(const glm::vec2 &s, const glm::vec2 &e)
    { m_sizeStart = s; m_sizeEnd = e; return this; }

ParticleSystemNode *ParticleSystemNode::setSize(float s)
    { m_sizeStart = m_sizeEnd = {s, s}; return this; }

ParticleSystemNode *ParticleSystemNode::setSize(float sS, float eS)
    { m_sizeStart = {sS, sS}; m_sizeEnd = {eS, eS}; return this; }

ParticleSystemNode *ParticleSystemNode::setColor(const glm::vec4 &s, const glm::vec4 &e)
    { m_colorStart = s; m_colorEnd = e; return this; }

ParticleSystemNode *ParticleSystemNode::setColor(const glm::vec4 &c)
    { m_colorStart = m_colorEnd = c; return this; }

ParticleSystemNode *ParticleSystemNode::setRotationSpeed(float mn, float mx)
    { m_rotSpeedMin = mn; m_rotSpeedMax = mx; return this; }

ParticleSystemNode *ParticleSystemNode::setRotationSpeed(float s)
    { m_rotSpeedMin = m_rotSpeedMax = s; return this; }

// ============================================================
//  Setters — physics / control
// ============================================================

ParticleSystemNode *ParticleSystemNode::setGravity(const glm::vec3 &g)
    { m_gravity = g; return this; }

ParticleSystemNode *ParticleSystemNode::setDrag(float d)
    { m_drag = d; return this; }

ParticleSystemNode *ParticleSystemNode::setDuration(float s)
    { m_duration = s; return this; }

ParticleSystemNode *ParticleSystemNode::setLoop(bool l)
    { m_loop = l; return this; }

ParticleSystemNode *ParticleSystemNode::setAutoPlay(bool a)
{
    m_autoPlay = a;
    if (a) play();
    return this;
}

// ============================================================
//  Setters — atlas
// ============================================================

ParticleSystemNode *ParticleSystemNode::setAtlasFrames(const std::vector<glm::vec4> &frames)
    { m_atlasFrames = frames; m_useAtlas = !frames.empty(); return this; }

ParticleSystemNode *ParticleSystemNode::addAtlasFrame(const glm::vec4 &frame)
    { m_atlasFrames.push_back(frame); m_useAtlas = true; return this; }

ParticleSystemNode *ParticleSystemNode::addAtlasFrame(float x, float y, float w, float h)
    { return addAtlasFrame({x, y, w, h}); }

ParticleSystemNode *ParticleSystemNode::setAtlasGrid(int cols, int rows)
{
    m_atlasFrames.clear();
    float cw = 1.f / cols, ch = 1.f / rows;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            m_atlasFrames.push_back({col * cw, row * ch, cw, ch});
    m_useAtlas = true;
    return this;
}

ParticleSystemNode *ParticleSystemNode::setFrame(int f)
    { m_currentFrame = f; return this; }

void ParticleSystemNode::clearAtlas()
    { m_atlasFrames.clear(); m_useAtlas = false; }

// ============================================================
//  Affectors
// ============================================================

ParticleSystemNode *ParticleSystemNode::addGravity(const glm::vec3 &g)
    { m_affectors.push_back(new GravityAffector(g)); return this; }

ParticleSystemNode *ParticleSystemNode::addDrag(float s)
    { m_affectors.push_back(new DragAffector(s)); return this; }

ParticleSystemNode *ParticleSystemNode::addVortex(const glm::vec3 &c, float s, float r)
    { m_affectors.push_back(new VortexAffector(c, s, r)); return this; }

ParticleSystemNode *ParticleSystemNode::addAttractor(const glm::vec3 &pos, float s,
                                                       float r, bool rep)
    { m_affectors.push_back(new AttractorAffector(pos, s, r, rep)); return this; }

ParticleSystemNode *ParticleSystemNode::addTurbulence(float s, float f)
    { m_affectors.push_back(new TurbulenceAffector(s, f)); return this; }

ParticleSystemNode *ParticleSystemNode::addColorOverLifetime(const glm::vec4 &s,
                                                              const glm::vec4 &e)
    { m_affectors.push_back(new ColorOverLifetimeAffector(s, e)); return this; }

ParticleSystemNode *ParticleSystemNode::addSizeOverLifetime(const glm::vec2 &s,
                                                             const glm::vec2 &e)
    { m_affectors.push_back(new SizeOverLifetimeAffector(s, e)); return this; }

void ParticleSystemNode::clearAffectors()
{
    for (auto *a : m_affectors) delete a;
    m_affectors.clear();
}
