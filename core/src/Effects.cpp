#include "Effects.hpp"
#include "RenderPipeline.hpp"
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

// ============================================================
//  DecalNode
// ============================================================
DecalNode::DecalNode(int maxDecals)
    : maxDecals_(maxDecals)
{
    type = NodeType::Decal;
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

void DecalNode::gatherRenderItems(RenderQueue& q, const FrameContext& ctx)
{
    if (!visible || !material) return;
    if (dirty_) rebuild();
    if (buffer_.indices.empty()) return;

    RenderItem item;
    item.drawable   = &buffer_;
    item.material   = material;
    item.model      = glm::mat4(1.f);
    item.passMask   = RenderPassMask::Transparent;
    q.add(item);
}

// ============================================================
//  LensFlareNode
// ============================================================
LensFlareNode::LensFlareNode()
{
    type = NodeType::LensFlare;
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

void LensFlareNode::gatherRenderItems(RenderQueue& q, const FrameContext& ctx)
{
    if (!visible || !enabled_ || !material || !ctx.camera) return;

    glm::mat4 view = ctx.camera->getView();
    glm::mat4 proj = ctx.camera->getProjection();

    // Project sun position (camera position + sun direction * far distance)
    glm::vec3 camPos   = ctx.camera->worldPosition();
    glm::vec3 sunWorld = camPos + sunDirection_ * 9000.f;
    glm::vec4 clip     = proj * view * glm::vec4(sunWorld, 1.f);

    if (clip.w <= 0.f) return;   // behind camera

    glm::vec2 sunNDC = toNDC(clip);
    float fade       = computeFade(sunNDC);
    if (fade <= 0.f) return;

    int vpW = ctx.viewport.z > 0 ? ctx.viewport.z : 1;
    int vpH = ctx.viewport.w > 0 ? ctx.viewport.w : 1;
    buildGeometry(sunNDC, fade, vpW, vpH);

    if (buffer_.indices.empty()) return;

    // Submit as a screen-space transparent item.
    // The material's shader should use a passthrough vertex shader
    // (gl_Position = vec4(a_position, 1.0) — no MVP transform).
    RenderItem item;
    item.drawable = &buffer_;
    item.material = material;
    item.model    = glm::mat4(1.f);
    item.passMask = RenderPassMask::Transparent;
    q.add(item);
}

// ============================================================
//  GrassNode
// ============================================================
GrassNode::GrassNode(GrassType type)
    : type_(type)
{
    this->type = NodeType::Grass;
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

void GrassNode::gatherRenderItems(RenderQueue& q, const FrameContext& ctx)
{
    if (!visible || !material || clumps_.empty()) return;
    if (dirty_ || !built_) build();
    if (buffer_.indices.empty()) return;

    // Pass wind parameters to shader via material uniforms
    material->setFloat("u_time",         time_);
    material->setFloat("u_windStrength", windStrength_);
    material->setFloat("u_windSpeed",    windSpeed_);
    material->setVec2 ("u_windDir",      windDir_);

    RenderItem item;
    item.drawable  = &buffer_;
    item.material  = material;
    item.model     = worldMatrix();
    item.worldAABB = aabb_;
    item.passMask  = RenderPassMask::Transparent;
    q.add(item);
}

// ============================================================
//  ManualMeshNode
// ============================================================
ManualMeshNode::ManualMeshNode()
{
    type = NodeType::ManualMesh;
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

void ManualMeshNode::gatherRenderItems(RenderQueue& q, const FrameContext& ctx)
{
    if (!visible || !material || buffer_.indices.empty()) return;

    const glm::mat4  world     = worldMatrix();
    const BoundingBox worldAABB = buffer_.aabb.transformed(world);

    // Frustum cull — skip if AABB is completely outside the view frustum.
    if (worldAABB.is_valid() && !ctx.frustum.contains(worldAABB))
        return;

    RenderItem item;
    item.drawable  = &buffer_;
    item.material  = material;
    item.model     = world;
    item.worldAABB = worldAABB;
    item.passMask  = RenderPassMask::Opaque;
    q.add(item);
}
