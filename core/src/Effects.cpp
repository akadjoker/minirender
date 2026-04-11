#include "Effects.hpp"
 
#include "Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
//  Helpers
// ============================================================
namespace {

/// Build a tangent frame from a normal vector.
/// Returns [right, up] in world space, rotated by `rotRad` around normal.
inline void makeTangentFrame(const glm::vec3& n, float rotRad,
                              glm::vec3& outRight, glm::vec3& outUp)
{
    glm::vec3 ref = (std::fabs(n.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    outRight = glm::normalize(glm::cross(ref, n));
    outUp    = glm::cross(n, outRight);

    if (rotRad != 0.f)
    {
        float c = std::cos(rotRad), s = std::sin(rotRad);
        glm::vec3 r = outRight;
        glm::vec3 u = outUp;
        outRight = r * c - u * s;
        outUp    = r * s + u * c;
    }
}

} // namespace

// ============================================================
//  EffectBuffer
// ============================================================
void EffectBuffer::allocate(int maxQuads)
{
    vertices.clear();
    indices.clear();
    vertices.reserve(maxQuads * 4);
    indices.reserve(maxQuads * 6);

    if (!vao)
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, maxQuads * 4 * sizeof(EffectVertex), nullptr, GL_DYNAMIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EffectVertex),
                          (void*)offsetof(EffectVertex, position));
    // uv
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(EffectVertex),
                          (void*)offsetof(EffectVertex, uv));
    // color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(EffectVertex),
                          (void*)offsetof(EffectVertex, color));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, maxQuads * 6 * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

    glBindVertexArray(0);
}

void EffectBuffer::upload()
{
    if (!vao) return;
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(vertices.size() * sizeof(EffectVertex)),
                    vertices.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(indices.size() * sizeof(uint32_t)),
                    indices.data());
    glBindVertexArray(0);
}

void EffectBuffer::draw() const
{
    if (!vao || indices.empty()) return;
    glBindVertexArray(vao);
    glDrawElements(mode, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void EffectBuffer::drawRange(uint32_t start, uint32_t count) const
{
    if (!vao || count == 0) return;
    glBindVertexArray(vao);
    glDrawElements(mode, (GLsizei)count, GL_UNSIGNED_INT,
                   (void*)(uintptr_t)(start * sizeof(uint32_t)));
    glBindVertexArray(0);
}

void EffectBuffer::free()
{
    if (ibo) { glDeleteBuffers(1, &ibo); ibo = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    vertices.clear();
    indices.clear();
}

void RibbonStripBuilder::build(const std::vector<RibbonStripVertex> &samples, EffectBuffer &buffer)
{
    buffer.vertices.clear();
    buffer.indices.clear();
    buffer.aabb = BoundingBox{};

    if (samples.size() < 2)
    {
        buffer.upload();
        return;
    }

    bool firstPoint = true;
    const uint32_t base = (uint32_t)buffer.vertices.size();
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const RibbonStripVertex &sample = samples[i];
        buffer.vertices.push_back({sample.left, {sample.u, 1.0f}, sample.color});
        buffer.vertices.push_back({sample.right, {sample.u, 0.0f}, sample.color});

        if (firstPoint)
        {
            buffer.aabb.min = sample.left;
            buffer.aabb.max = sample.left;
            firstPoint = false;
        }

        buffer.aabb.expand(sample.left);
        buffer.aabb.expand(sample.right);
    }

    for (size_t i = 0; i + 1 < samples.size(); ++i)
    {
        const uint32_t i0 = base + (uint32_t)(i * 2);
        const uint32_t i1 = i0 + 1;
        const uint32_t i2 = i0 + 2;
        const uint32_t i3 = i0 + 3;
        buffer.indices.insert(buffer.indices.end(), {i0, i1, i3, i3, i2, i0});
    }

    buffer.upload();
}

// ============================================================
//  DecalNode
// ============================================================
DecalNode::DecalNode(int maxDecals)
    : maxDecals_(maxDecals)
{
    type = NodeType::Decal;
    renderType = RenderType::Transparent;
    buffer_.allocate(maxDecals);
}

DecalNode::~DecalNode() { buffer_.free(); }

int DecalNode::addDecal(const glm::vec3& pos, const glm::vec3& normal,
                         const glm::vec2& size, const glm::vec4& color,
                         float lifetime)
{
    // Recycle a dead slot if possible
    for (int i = 0; i < (int)decals_.size(); ++i)
    {
        if (!decals_[i].active)
        {
            decals_[i] = {pos, glm::normalize(normal), size, color, 0.f,
                          lifetime < 0.f ? defaultLifetime_ : lifetime,
                          0.f, defaultFadeStart_, true};
            dirty_ = true;
            return i;
        }
    }

    if ((int)decals_.size() >= maxDecals_)
    {
        // Remove oldest active decal first
        removeDecal(0);
    }

    Decal d;
    d.position  = pos;
    d.normal    = glm::normalize(normal);
    d.size      = size;
    d.color     = color;
    d.lifetime  = lifetime < 0.f ? defaultLifetime_ : lifetime;
    d.fadeStart = defaultFadeStart_;
    d.active    = true;
    decals_.push_back(d);
    dirty_ = true;
    return (int)decals_.size() - 1;
}

int DecalNode::addDecal(const glm::vec3& pos, const glm::vec3& normal, float lifetime)
{
    return addDecal(pos, normal, defaultSize_, {1.f,1.f,1.f,1.f}, lifetime);
}

void DecalNode::removeDecal(int idx)
{
    if (idx < 0 || idx >= (int)decals_.size()) return;
    decals_[idx].active = false;
    dirty_ = true;
}

void DecalNode::removeAll()
{
    decals_.clear();
    dirty_ = true;
}

DecalNode::Decal* DecalNode::getDecal(int idx)
{
    if (idx < 0 || idx >= (int)decals_.size()) return nullptr;
    return &decals_[idx];
}

int DecalNode::activeCount() const
{
    int n = 0;
    for (const auto& d : decals_) if (d.active) ++n;
    return n;
}

void DecalNode::update(float dt)
{
    bool changed = false;
    for (auto& d : decals_)
    {
        if (!d.active) continue;
        if (d.lifetime > 0.f)
        {
            d.timeAlive += dt;
            if (d.timeAlive >= d.lifetime) { d.active = false; changed = true; continue; }
            changed = true;  // alpha might change each frame
        }
    }
    if (changed) dirty_ = true;
}

void DecalNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !material)
        return;

    if (dirty_)
        rebuild();

    if (buffer_.indices.empty())
        return;

    shader->setMat4("u_model", worldMatrix());
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(worldMatrix()))));

    material->applyStates();
    material->applyUniformsTo(shader);
    material->bindTexturesTo(shader);
    buffer_.draw();
}

void DecalNode::rebuild()
{
    buffer_.vertices.clear();
    buffer_.indices.clear();

    for (const auto& d : decals_)
    {
        if (!d.active) continue;

        glm::vec3 right, up;
        makeTangentFrame(d.normal, d.rotation, right, up);

        float hw = d.size.x * 0.5f;
        float hh = d.size.y * 0.5f;

        float alpha = d.color.a;
        if (d.lifetime > 0.f && d.timeAlive / d.lifetime > d.fadeStart)
        {
            float t = (d.timeAlive / d.lifetime - d.fadeStart) / (1.f - d.fadeStart);
            alpha *= 1.f - glm::clamp(t, 0.f, 1.f);
        }
        glm::vec4 col = {d.color.r, d.color.g, d.color.b, alpha};

        auto base = (uint32_t)buffer_.vertices.size();

        // Four corners:  BL, BR, TR, TL
        buffer_.vertices.push_back({d.position - right*hw - up*hh, {0,1}, col});
        buffer_.vertices.push_back({d.position + right*hw - up*hh, {1,1}, col});
        buffer_.vertices.push_back({d.position + right*hw + up*hh, {1,0}, col});
        buffer_.vertices.push_back({d.position - right*hw + up*hh, {0,0}, col});

        buffer_.indices.insert(buffer_.indices.end(),
            { base, base+1, base+2, base+2, base+3, base });
    }

    buffer_.upload();
    dirty_ = false;
}

// ============================================================
//  LensFlareNode
// ============================================================
LensFlareNode::LensFlareNode()
{
    type = NodeType::LensFlare;
    renderType = RenderType::Overlay;
}

LensFlareNode::~LensFlareNode()
{
    buffer_.free();
}

void LensFlareNode::initDefaultFlares()
{
    flares_.clear();
    loggedOnce_ = false;  // re-log whenever clips change

    // Atlas 256x256  
    struct Clip { float x, y, w, h; };
    // static const Clip clips[] = {
    //     {  0,   0, 128, 128},  // 0 — estrela branca (sun burst)
    //     {128,   0, 128, 128},  // 1 — círculo suave / halo
    //     {  0, 128, 128, 128},  // 2 — estrela laranja (warm glow)
    //     {128, 128,  64,  64},  // 3 — círculo pequeno topo
    //     {128, 192,  64,  64},  // 4 — círculo anel
    //     {  0, 192,  64,  64},  // 5 — hexágono
    // };


 static const Clip clips[] = {
       {  1,   1, 127, 127},   //estrela 0
       {  1,   128, 127, 127}, //sol 1 
       
       {  128,   1, 62, 62},  // halo 2 
       
       {  128+64,   1, 62, 62},// hallo filled 3 

       {  128,  64,  63 , 63}, // cirlce 4 b

       {  128,  66*2, 63,  60}, //small cicle filled 5 
       {  128,  64*3, 63,  63}, // hexa 6
   

    };

    
        
    //indexes = {0, 4, 1, 2, 2, 3, 1, 5, 3, 2, 4, 1, 2, 3};

    struct FlareDef { int clip; float pos; float size; glm::vec3 color; float alpha; bool isSun; };
static const FlareDef defs[] = {
  //  {5,  1.00f, 0.10f, {1.0f, 1.0f, 1.0f},  0.2f, false },  // sun burst
  //  {4,  0.90f, 0.12f, {1.0f, 0.95f,0.8f},  0.5f, false},  // halo
  //  {4,  0.85f, 0.14f, {1.0f, 0.7f, 0.3f},  0.3f, false},  // warm glow
    {2,  0.60f, 0.08f, {0.8f, 0.8f, 1.0f},  0.7f, false},  // ← era 0.04
    {4,  0.45f, 0.09f, {1.0f, 0.9f, 1.0f},  0.6f, false},  // ← era 0.05
    {3,  0.25f, 0.06f, {0.6f, 1.0f, 0.6f},  0.5f, true},  // ← era 0.03
    {1,  0.00f, 0.10f, {0.8f, 0.8f, 1.0f},  1.0f, false},  // ← era 0.07
    {5, -0.20f, 0.08f, {1.0f, 0.8f, 0.6f},  0.5f, false},  // ← era 0.04
    {6, -0.40f, 0.10f, {0.7f, 0.9f, 1.0f},  0.4f, false},  // ← era 0.06
    {2, -0.60f, 0.15f, {0.7f, 0.9f, 1.0f},  0.4f, false},  // ← era 0.06
};

    for (const auto& d : defs)
    {
        const Clip& c = clips[d.clip];
        FlareElement fe;
        fe.position = d.pos;
        fe.size     = d.size;
        fe.color    = d.color;
        fe.alpha    = d.alpha;
        fe.isSun    = d.isSun;
        fe.setPixelRect(c.x, c.y, c.w, c.h);
        flares_.push_back(fe);
    }
}

glm::vec2 LensFlareNode::toNDC(const glm::vec4& clip) const
{
    if (clip.w <= 0.f) return {2.f, 2.f}; // off screen
    return glm::vec2(clip.x / clip.w, clip.y / clip.w);
}

float LensFlareNode::computeFade(const glm::vec2& ndc) const
{
    float mx = std::max(std::fabs(ndc.x), std::fabs(ndc.y));
    float edge = 1.f - edgeFade_;
    if (mx > 1.f) return 0.f;
    if (mx < edge) return 1.f;
    return 1.f - (mx - edge) / edgeFade_;
}

void LensFlareNode::buildGeometry(const glm::vec2& sunNDC, float fade,
                                   int vpW, int vpH)
{
    buffer_.vertices.clear();
    buffer_.indices.clear();

    float aspect = (vpH > 0) ? (float)vpW / (float)vpH : 1.f;

    // Resolve atlas texture size from the material at render time
    float texW = 256.f, texH = 256.f;
    if (material)
    {
        Texture *tex = material->getTexture("u_albedo");
        if (tex && tex->width > 0 && tex->height > 0)
        {
            texW = (float)tex->width;
            texH = (float)tex->height;
        }
        else
        {
            SDL_Log("[LensFlare] WARNING: atlas texture not found or size=0, using fallback 256x256");
        }
    }
    if (!loggedOnce_)
    {
        SDL_Log("[LensFlare] atlas texW=%.0f texH=%.0f  flares=%d", texW, texH, (int)flares_.size());
        for (int _i = 0; _i < (int)flares_.size() && _i < 3; ++_i)
        {
            glm::vec4 _uv = flares_[_i].uvRect(texW, texH);
            SDL_Log("[LensFlare]  flare[%d] pixelRect=(%.0f,%.0f %.0fx%.0f) -> uv=(%.3f,%.3f %.3f,%.3f)",
                _i,
                flares_[_i].pixelRect.x, flares_[_i].pixelRect.y,
                flares_[_i].pixelRect.z, flares_[_i].pixelRect.w,
                _uv.x, _uv.y, _uv.z, _uv.w);
        }
        loggedOnce_ = true;
    }

    // Screen centre in NDC = (0, 0)
    // Each flare at:  flare.position * sunNDC  (0 = centre, 1 = sun, -1 = mirror)

    for (const auto& fe : flares_)
    {
        glm::vec2 centre = sunNDC * fe.position;

        // Half-size in NDC (size is fraction of screen height, NDC height = 2)
        float hH = fe.size;
        float hW = hH / aspect;

        glm::vec4 col = {fe.color, fe.alpha * fade};
        // Convert pixel rect → UV using actual texture dimensions
        glm::vec4 uv  = fe.uvRect(texW, texH);
        float u0 = uv.x, v0 = uv.y, u1 = uv.z, v1 = uv.w;

        auto base = (uint32_t)buffer_.vertices.size();
        // Build quad in NDC space (z = 0.999 so it's drawn at far plane)
        // NDC y increases upward; engine convention: bottom vertex = v1, top vertex = v0
        buffer_.vertices.push_back({{centre.x - hW, centre.y - hH, 0.999f}, {u0, v1}, col}); // BL
        buffer_.vertices.push_back({{centre.x + hW, centre.y - hH, 0.999f}, {u1, v1}, col}); // BR
        buffer_.vertices.push_back({{centre.x + hW, centre.y + hH, 0.999f}, {u1, v0}, col}); // TR
        buffer_.vertices.push_back({{centre.x - hW, centre.y + hH, 0.999f}, {u0, v0}, col}); // TL
        buffer_.indices.insert(buffer_.indices.end(),
            {base, base+1, base+2, base+2, base+3, base});
    }

    if (!allocated_)
    {
        buffer_.allocate((int)flares_.size() + 4);
        allocated_ = true;
    }
    buffer_.upload();
}

void LensFlareNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !enabled_ || !material)
        return;

    const glm::vec3 cameraPos = camera->worldPosition();
    const glm::vec3 sunWorld = cameraPos - sunDirection_ * 5000.0f;
    const glm::vec4 clip = camera->projection * camera->view * glm::vec4(sunWorld, 1.0f);
    const glm::vec2 sunNdc = toNDC(clip);
    const float fade = computeFade(sunNdc);
    if (fade <= 0.0f)
        return;

    buildGeometry(sunNdc, fade, camera->viewport.z, camera->viewport.w);
    if (buffer_.indices.empty())
        return;

    material->applyStates();
    material->applyUniformsTo(shader);
    material->bindTexturesTo(shader);
    buffer_.draw();
}

// ============================================================
//  GrassNode
// ============================================================
GrassNode::GrassNode(GrassType type)
    : type_(type)
{
    this->type = NodeType::Grass;
    renderType = RenderType::Transparent;
    buffer_.dynamic = false;
    buffer_.mode    = GL_TRIANGLES;
}

GrassNode::~GrassNode()
{
    buffer_.free();
}

void GrassNode::addClump(const glm::vec3& position, const glm::vec3& normal,
                          const glm::vec2& size, const glm::vec4& color)
{
    clumps_.push_back({position, glm::normalize(normal), size, color});
    dirty_ = true;
    built_ = false;
}

void GrassNode::fillArea(const glm::vec3& center, float width, float depth,
                          int count, float minSize, float maxSize, unsigned int seed)
{
    // Simple LCG
    uint32_t rng = seed;
    auto rnd = [&]() -> float {
        rng = rng * 1664525u + 1013904223u;
        return (float)(rng >> 1) / (float)(1u << 31);
    };

    for (int i = 0; i < count; ++i)
    {
        glm::vec3 pos = center + glm::vec3((rnd() - 0.5f) * width,
                                           0.f,
                                           (rnd() - 0.5f) * depth);
        float s = minSize + rnd() * (maxSize - minSize);
        glm::vec4 col = {0.4f + rnd()*0.3f, 0.6f + rnd()*0.3f, 0.2f + rnd()*0.1f, 1.f};
        addClump(pos, {0,1,0}, {s * 0.8f, s}, col);
    }
}

void GrassNode::addQuad(const glm::vec3& center,
                         const glm::vec3& right, const glm::vec3& up,
                         const glm::vec2& size, const glm::vec4& color)
{
    float hw = size.x * 0.5f;
    float h  = size.y;

    glm::vec3 n = glm::normalize(glm::cross(right, up));

    auto base = (uint32_t)buffer_.vertices.size();

    glm::vec4 t = {right, 1.f};
    // Bottom-left, bottom-right, top-right, top-left
    buffer_.vertices.push_back({center - right*hw,          n, t, {0,1}});
    buffer_.vertices.push_back({center + right*hw,          n, t, {1,1}});
    buffer_.vertices.push_back({center + right*hw + up*h,   n, t, {1,0}});
    buffer_.vertices.push_back({center - right*hw + up*h,   n, t, {0,0}});

    buffer_.indices.insert(buffer_.indices.end(),
        {base, base+1, base+2, base+2, base+3, base});
}

void GrassNode::build()
{
    buffer_.vertices.clear();
    buffer_.indices.clear();
    aabb_ = {};
    bool first = true;

    for (const auto& c : clumps_)
    {
        glm::vec3 right, surfUp;
        makeTangentFrame(c.normal, 0.f, right, surfUp);

        // "Up" for quad = surface normal (grass grows along normal)
        glm::vec3 worldUp = c.normal;

        switch (type_)
        {
        case GrassType::TriCross:
            {
                float angles[] = {0.f, 60.f, 120.f};
                for (float a : angles)
                {
                    float rad = glm::radians(a);
                    glm::vec3 r = right * std::cos(rad) + surfUp * std::sin(rad);
                    addQuad(c.position, r, worldUp, c.size, c.color);
                }
            }
            break;
        case GrassType::Cross:
            addQuad(c.position, right,   worldUp, c.size, c.color);
            addQuad(c.position, surfUp,  worldUp, c.size, c.color);
            break;
        case GrassType::Single:
        default:
            addQuad(c.position, right, worldUp, c.size, c.color);
            break;
        }

        // Expand AABB
        glm::vec3 top = c.position + worldUp * c.size.y;
        if (first)
        {
            aabb_.min = aabb_.max = c.position;
            first = false;
        }
        aabb_.min = glm::min(aabb_.min, c.position - glm::vec3(c.size.x));
        aabb_.max = glm::max(aabb_.max, top         + glm::vec3(c.size.x));
    }

    buffer_.aabb = aabb_;
    buffer_.upload();
    built_ = true;
    dirty_ = false;
}

void GrassNode::clear()
{
    clumps_.clear();
    buffer_.vertices.clear();
    buffer_.indices.clear();
    built_ = false;
    dirty_ = true;
}

void GrassNode::update(float dt)
{
    time_ += dt;
}

void GrassNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !material)
        return;

    if (dirty_ || !built_)
        build();

    if (buffer_.indices.empty())
        return;

    const glm::mat4 model = worldMatrix();
    const BoundingBox worldBounds = aabb_.transformed(model);
    if (worldBounds.is_valid() && !camera->frustum.contains(worldBounds))
        return;

    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));

    material->setFloat("u_time", time_);
    material->applyStates();
    material->applyUniformsTo(shader);
    material->bindTexturesTo(shader);
    buffer_.draw();
}

// ============================================================
//  RibbonTrailNode
// ============================================================
RibbonTrailNode::RibbonTrailNode(int maxChains, int maxElementsPerChain)
    : maxChains_(glm::max(1, maxChains))
    , maxElementsPerChain_(glm::max(2, maxElementsPerChain))
{
    type = NodeType::RibbonTrail;
    renderType = RenderType::Transparent;
    ensureCapacity();
}

RibbonTrailNode::~RibbonTrailNode()
{
    buffer_.free();
}

int RibbonTrailNode::addChain(Node3D *emitter,
                              const glm::vec4 &startColor,
                              const glm::vec4 &endColor,
                              float startWidth,
                              float endWidth)
{
    if (!emitter || (int)chains_.size() >= maxChains_)
        return -1;

    Chain chain;
    chain.emitter = emitter;
    chain.startColor = startColor;
    chain.endColor = endColor;
    chain.startWidth = glm::max(0.001f, startWidth);
    chain.endWidth = glm::max(0.0f, endWidth);
    chain.elements.push_back({emitter->worldPosition(), 0.0f});
    chains_.push_back(chain);
    geometryDirty_ = true;
    return (int)chains_.size() - 1;
}

void RibbonTrailNode::removeChain(int index)
{
    if (index < 0 || index >= (int)chains_.size())
        return;

    chains_.erase(chains_.begin() + index);
    geometryDirty_ = true;
}

void RibbonTrailNode::clearChains()
{
    chains_.clear();
    geometryDirty_ = true;
}

bool RibbonTrailNode::setChainColors(int index, const glm::vec4 &startColor, const glm::vec4 &endColor)
{
    if (index < 0 || index >= (int)chains_.size())
        return false;

    chains_[index].startColor = startColor;
    chains_[index].endColor = endColor;
    geometryDirty_ = true;
    return true;
}

bool RibbonTrailNode::setChainWidths(int index, float startWidth, float endWidth)
{
    if (index < 0 || index >= (int)chains_.size())
        return false;

    chains_[index].startWidth = glm::max(0.001f, startWidth);
    chains_[index].endWidth = glm::max(0.0f, endWidth);
    geometryDirty_ = true;
    return true;
}

void RibbonTrailNode::setMaxElementsPerChain(int count)
{
    maxElementsPerChain_ = glm::max(2, count);
    for (auto &chain : chains_)
    {
        if ((int)chain.elements.size() > maxElementsPerChain_)
            chain.elements.erase(chain.elements.begin(),
                                 chain.elements.begin() + ((int)chain.elements.size() - maxElementsPerChain_));
    }
    ensureCapacity();
    geometryDirty_ = true;
}

int RibbonTrailNode::chainCount() const
{
    return (int)chains_.size();
}

int RibbonTrailNode::activeChainCount() const
{
    int count = 0;
    for (const auto &chain : chains_)
        if (chain.active && chain.emitter)
            ++count;
    return count;
}

void RibbonTrailNode::update(float dt)
{
    bool changed = false;

    for (auto &chain : chains_)
    {
        if (!chain.active || !chain.emitter)
            continue;

        for (auto &element : chain.elements)
            element.age += dt;

        while (!chain.elements.empty() && chain.elements.front().age > trailLength_)
        {
            chain.elements.erase(chain.elements.begin());
            changed = true;
        }

        const glm::vec3 currentPos = chain.emitter->worldPosition();
        if (chain.elements.empty())
        {
            chain.elements.push_back({currentPos, 0.0f});
            changed = true;
        }
        else
        {
            ChainElement &head = chain.elements.back();
            if (glm::distance2(head.position, currentPos) >= minSegmentLength_ * minSegmentLength_)
            {
                chain.elements.push_back({currentPos, 0.0f});
                changed = true;
            }
            else
            {
                head.position = currentPos;
                head.age = 0.0f;
                changed = true;
            }
        }

        while ((int)chain.elements.size() > maxElementsPerChain_)
        {
            chain.elements.erase(chain.elements.begin());
            changed = true;
        }
    }

    if (changed)
        geometryDirty_ = true;
}

void RibbonTrailNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !material)
        return;

    if (geometryDirty_)
        rebuildGeometry(camera);

    if (buffer_.indices.empty())
        return;

    if (buffer_.aabb.is_valid() && !camera->frustum.contains(buffer_.aabb))
        return;

    shader->setMat4("u_model", glm::mat4(1.0f));
    shader->setMat3("u_normalMatrix", glm::mat3(1.0f));

    material->applyStates();
    material->applyUniformsTo(shader);
    material->bindTexturesTo(shader);
    buffer_.draw();
}

void RibbonTrailNode::rebuildGeometry(const Camera *camera)
{
    buffer_.vertices.clear();
    buffer_.indices.clear();
    buffer_.aabb = BoundingBox{};

    if (!camera)
    {
        geometryDirty_ = false;
        return;
    }

    const glm::vec3 cameraPos = camera->worldPosition();
    bool firstPoint = true;

    for (const auto &chain : chains_)
    {
        if (!chain.active || !chain.emitter || chain.elements.size() < 2)
            continue;

        const size_t pointCount = chain.elements.size();
        std::vector<glm::vec3> leftPoints(pointCount);
        std::vector<glm::vec3> rightPoints(pointCount);
        std::vector<glm::vec4> colors(pointCount);
        std::vector<float> vCoords(pointCount);

        for (size_t i = 0; i < pointCount; ++i)
        {
            const ChainElement &element = chain.elements[i];
            glm::vec3 prevTangent(0.0f);
            glm::vec3 nextTangent(0.0f);

            if (i > 0)
                prevTangent = element.position - chain.elements[i - 1].position;
            if (i + 1 < pointCount)
                nextTangent = chain.elements[i + 1].position - element.position;

            const bool hasPrev = glm::length2(prevTangent) >= 1e-8f;
            const bool hasNext = glm::length2(nextTangent) >= 1e-8f;
            if (!hasPrev && !hasNext)
                continue;

            glm::vec3 tangent(0.0f);
            if (hasPrev && hasNext)
            {
                prevTangent = glm::normalize(prevTangent);
                nextTangent = glm::normalize(nextTangent);
                tangent = prevTangent + nextTangent;
                if (glm::length2(tangent) < 1e-8f)
                    tangent = nextTangent;
            }
            else
            {
                tangent = hasNext ? nextTangent : prevTangent;
            }

            tangent = glm::normalize(tangent);
            glm::vec3 viewDir = cameraPos - element.position;
            if (glm::length2(viewDir) < 1e-8f)
                viewDir = glm::vec3(0.0f, 0.0f, 1.0f);
            else
                viewDir = glm::normalize(viewDir);

            glm::vec3 side = glm::cross(tangent, viewDir);
            if (glm::length2(side) < 1e-8f)
                side = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length2(side) < 1e-8f)
                side = glm::cross(tangent, glm::vec3(1.0f, 0.0f, 0.0f));
            side = glm::normalize(side);

            const float t = glm::clamp(element.age / trailLength_, 0.0f, 1.0f);
            const float halfWidth = glm::mix(chain.startWidth, chain.endWidth, t) * 0.5f;
            leftPoints[i] = element.position - side * halfWidth;
            rightPoints[i] = element.position + side * halfWidth;
            colors[i] = glm::mix(chain.startColor, chain.endColor, t);
            vCoords[i] = t;

            if (firstPoint)
            {
                buffer_.aabb.min = leftPoints[i];
                buffer_.aabb.max = leftPoints[i];
                firstPoint = false;
            }

            buffer_.aabb.expand(leftPoints[i]);
            buffer_.aabb.expand(rightPoints[i]);
        }

        const uint32_t base = (uint32_t)buffer_.vertices.size();
        for (size_t i = 0; i < pointCount; ++i)
        {
            buffer_.vertices.push_back({leftPoints[i], {0.0f, vCoords[i]}, colors[i]});
            buffer_.vertices.push_back({rightPoints[i], {1.0f, vCoords[i]}, colors[i]});
        }

        for (size_t i = 0; i + 1 < pointCount; ++i)
        {
            const uint32_t i0 = base + (uint32_t)(i * 2);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + 2;
            const uint32_t i3 = i0 + 3;
            buffer_.indices.insert(buffer_.indices.end(), {i0, i1, i3, i3, i2, i0});
        }
    }

    buffer_.upload();
    geometryDirty_ = false;
}

void RibbonTrailNode::ensureCapacity()
{
    buffer_.free();
    buffer_.allocate(maxChains_ * glm::max(1, maxElementsPerChain_ - 1));
}

// ============================================================
//  RibbonSheetNode
// ============================================================
RibbonSheetNode::RibbonSheetNode(int maxSamples)
    : maxSamples_(glm::max(2, maxSamples))
{
    type = NodeType::RibbonSheet;
    renderType = RenderType::Transparent;
    ensureCapacity();
}

RibbonSheetNode::~RibbonSheetNode()
{
    buffer_.free();
}

void RibbonSheetNode::addSample(const glm::vec3 &left, const glm::vec3 &right)
{
    if (glm::distance2(left, right) < 1e-8f)
        return;

    const glm::vec3 newMid = (left + right) * 0.5f;
    const float minDistance2 = minSampleDistance_ * minSampleDistance_;

    if (!samples_.empty())
    {
        Sample &last = samples_.back();
        const glm::vec3 lastMid = (last.left + last.right) * 0.5f;
        const bool movedEnough =
            glm::distance2(lastMid, newMid) >= minDistance2 ||
            glm::distance2(last.left, left) >= minDistance2 ||
            glm::distance2(last.right, right) >= minDistance2;

        if (!movedEnough)
        {
            last.left = left;
            last.right = right;
            last.age = 0.0f;
            geometryDirty_ = true;
            return;
        }
    }

    samples_.push_back({left, right, 0.0f});
    while ((int)samples_.size() > maxSamples_)
        samples_.erase(samples_.begin());

    geometryDirty_ = true;
}

void RibbonSheetNode::clearSamples()
{
    samples_.clear();
    geometryDirty_ = true;
}

void RibbonSheetNode::setMaxSamples(int count)
{
    maxSamples_ = glm::max(2, count);
    while ((int)samples_.size() > maxSamples_)
        samples_.erase(samples_.begin());
    ensureCapacity();
    geometryDirty_ = true;
}

void RibbonSheetNode::update(float dt)
{
    bool changed = false;
    for (auto &sample : samples_)
        sample.age += dt;

    while (!samples_.empty() && samples_.front().age > lifetime_)
    {
        samples_.erase(samples_.begin());
        changed = true;
    }

    if (changed)
        geometryDirty_ = true;
}

void RibbonSheetNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !material)
        return;

    if (geometryDirty_)
        rebuild();

    if (buffer_.indices.empty())
        return;

    if (buffer_.aabb.is_valid() && !camera->frustum.contains(buffer_.aabb))
        return;

    shader->setMat4("u_model", glm::mat4(1.0f));
    shader->setMat3("u_normalMatrix", glm::mat3(1.0f));

    material->applyStates();
    material->applyUniformsTo(shader);
    material->bindTexturesTo(shader);
    buffer_.draw();
}

void RibbonSheetNode::rebuild()
{
    RibbonStripBuilder::build(buildStripSamples(), buffer_);
    geometryDirty_ = false;
}

void RibbonSheetNode::ensureCapacity()
{
    buffer_.free();
    buffer_.allocate(glm::max(1, maxSamples_ - 1));
}

std::vector<RibbonStripVertex> RibbonSheetNode::buildStripSamples() const
{
    std::vector<RibbonStripVertex> stripSamples;
    if (samples_.size() < 2)
        return stripSamples;

    stripSamples.resize(samples_.size());
    std::vector<float> uCoords(samples_.size(), 0.0f);
    float totalLength = 0.0f;
    for (size_t i = 1; i < samples_.size(); ++i)
    {
        const glm::vec3 prevMid = (samples_[i - 1].left + samples_[i - 1].right) * 0.5f;
        const glm::vec3 nextMid = (samples_[i].left + samples_[i].right) * 0.5f;
        totalLength += glm::length(nextMid - prevMid);
        uCoords[i] = totalLength;
    }

    if (totalLength <= 1e-6f)
        totalLength = 1.0f;

    for (size_t i = 0; i < samples_.size(); ++i)
    {
        const Sample &sample = samples_[i];
        const float t = glm::clamp(sample.age / lifetime_, 0.0f, 1.0f);
        stripSamples[i].left = sample.left;
        stripSamples[i].right = sample.right;
        stripSamples[i].color = glm::mix(startColor_, endColor_, t);
        stripSamples[i].u = uCoords[i] / totalLength;
    }

    return stripSamples;
}

// ============================================================
//  ManualMeshNode
// ============================================================
ManualMeshNode::ManualMeshNode()
{
    type = NodeType::ManualMesh;
    renderType = RenderType::Solid;
    buffer_.dynamic = true;
    buffer_.mode    = GL_TRIANGLES;
    // Init current-vertex state
    curNormal_ = {0.f, 1.f, 0.f};
    curUV_     = {0.f, 0.f};
    curColour_ = {1.f, 1.f, 1.f, 1.f};
}

ManualMeshNode::~ManualMeshNode()
{
    buffer_.free();
}

void ManualMeshNode::begin(GLenum primitiveType, bool dynamic)
{
    buffer_.vertices.clear();
    buffer_.indices.clear();
    buffer_.mode    = primitiveType;
    buffer_.dynamic = dynamic;
    building_       = true;
}

ManualMeshNode& ManualMeshNode::normal(float x, float y, float z)
{
    curNormal_ = {x, y, z};
    return *this;
}

ManualMeshNode& ManualMeshNode::normal(const glm::vec3& n)
{
    curNormal_ = n;
    return *this;
}

ManualMeshNode& ManualMeshNode::texCoord(float u, float v)
{
    curUV_ = {u, v};
    return *this;
}

ManualMeshNode& ManualMeshNode::colour(float r, float g, float b, float a)
{
    curColour_ = {r, g, b, a};
    return *this;
}

ManualMeshNode& ManualMeshNode::colour(const glm::vec4& c)
{
    curColour_ = c;
    return *this;
}

ManualMeshNode& ManualMeshNode::position(float x, float y, float z)
{
    Vertex v;
    v.position = {x, y, z};
    v.normal   = curNormal_;
    v.tangent  = {curColour_.r, curColour_.g, curColour_.b, curColour_.a}; // colour in tangent slot
    v.uv       = curUV_;
    buffer_.vertices.push_back(v);
    return *this;
}

ManualMeshNode& ManualMeshNode::position(const glm::vec3& p)
{
    return position(p.x, p.y, p.z);
}

ManualMeshNode& ManualMeshNode::index(uint32_t i)
{
    buffer_.indices.push_back(i);
    return *this;
}

ManualMeshNode& ManualMeshNode::triangle(uint32_t a, uint32_t b, uint32_t c)
{
    buffer_.indices.push_back(a);
    buffer_.indices.push_back(b);
    buffer_.indices.push_back(c);
    return *this;
}

void ManualMeshNode::end()
{
    building_ = false;
    build();
}

void ManualMeshNode::build()
{
    buffer_.aabb = computeAABB();
    if (buffer_.vao == 0)
        buffer_.upload();
    else
        buffer_.update();
}

void ManualMeshNode::clear()
{
    buffer_.vertices.clear();
    buffer_.indices.clear();
}

void ManualMeshNode::computeNormals()
{
    auto& verts   = buffer_.vertices;
    auto& indices = buffer_.indices;

    for (auto& v : verts) v.normal = {0,0,0};

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
        glm::vec3 e1 = verts[b].position - verts[a].position;
        glm::vec3 e2 = verts[c].position - verts[a].position;
        glm::vec3 fn = glm::cross(e1, e2);
        verts[a].normal += fn;
        verts[b].normal += fn;
        verts[c].normal += fn;
    }

    for (auto& v : verts)
        if (glm::length2(v.normal) > 1e-8f)
            v.normal = glm::normalize(v.normal);
}

BoundingBox ManualMeshNode::computeAABB() const
{
    BoundingBox box;
    if (buffer_.vertices.empty()) return box;
    box.min = box.max = buffer_.vertices[0].position;
    for (const auto& v : buffer_.vertices)
    {
        box.min = glm::min(box.min, v.position);
        box.max = glm::max(box.max, v.position);
    }
    return box;
}

void ManualMeshNode::render(Shader *shader, Camera *camera)
{
    if (!shader || !camera || !material || buffer_.indices.empty())
        return;

    const glm::mat4 model = worldMatrix();
    const BoundingBox worldBounds = buffer_.aabb.transformed(model);
    if (worldBounds.is_valid() && !camera->frustum.contains(worldBounds))
        return;

    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));

    material->applyStates();
    material->applyUniformsTo(shader);
    material->bindTexturesTo(shader);
    buffer_.draw();
}
