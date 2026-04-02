#include "RenderPipeline.hpp"
#include "Scene.hpp"
#include "RenderState.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <string>

// Helper: send all active clip planes to a shader
static void sendClipPlanes(Shader *sh, const FrameContext &ctx)
{
    sh->setInt("u_clipPlaneCount", ctx.clipPlaneCount);
    for (int i = 0; i < ctx.clipPlaneCount; ++i)
    {
        std::string name = "u_clipPlanes[" + std::to_string(i) + "]";
        sh->setVec4(name, ctx.clipPlanes[i]);
    }
}

// Helper: send scene lights to a shader.
// Supports both forward (u_lightDir/u_lightColor/u_ambient) and
// deferred (u_dirLightDir/u_dirLightColor) naming conventions.
// Missing uniforms in a shader are silently ignored (getLoc returns -1).
static void sendLights(Shader *sh, const FrameContext &ctx)
{
    // ── Directional light ────────────────────────────────────
    // Default: overhead white light, low ambient
    glm::vec4 lightDir   = glm::vec4(0.f, 1.f, 0.f, 0.f); // toward light
    glm::vec4 lightColor = glm::vec4(1.f, 1.f, 1.f, 1.f);
    glm::vec4 ambient    = glm::vec4(0.08f, 0.08f, 0.08f, 1.f);

    for (const auto *l : ctx.lights)
    {
        if (l->lightType != LightType::Directional) continue;
        const auto *dl = static_cast<const DirectionalLight *>(l);
        // forward() = direction light faces; TOWARD light = -forward()
        glm::vec3 dir = glm::normalize(-dl->forward());
        lightDir   = glm::vec4(dir, 0.f);
        lightColor = glm::vec4(dl->color * dl->intensity, 1.f);
        ambient    = glm::vec4(dl->ambient, 1.f);
        break; // use first directional light only
    }

    // Forward shader names
    sh->setVec4("u_lightDir",   lightDir);
    sh->setVec4("u_lightColor", lightColor);
    sh->setVec4("u_ambient",    ambient);

    // Deferred shader names (silently no-op if not present)
    sh->setVec4("u_dirLightDir",   lightDir);
    sh->setVec4("u_dirLightColor", lightColor);

    // ── Point lights ─────────────────────────────────────────
    int numPt = 0;
    for (const auto *l : ctx.lights)
    {
        if (l->lightType != LightType::Point) continue;
        const auto *pt  = static_cast<const PointLight *>(l);
        std::string idx = "[" + std::to_string(numPt) + "]";
        sh->setVec3("u_pointPos"    + idx, pt->worldPosition());
        sh->setVec3("u_pointColor"  + idx, pt->color * pt->intensity);
        sh->setFloat("u_pointRadius" + idx, pt->range);
        if (++numPt >= 16) break;
    }
    sh->setInt("u_numPointLights", numPt);
}

// ─── RenderPass::execute ──────────────────────────────────────────────────────────────────
void RenderPass::execute(const FrameContext &ctx, RenderQueue &queue) const
{
    if (!ctx.camera)
        return;

    auto &rs = RenderState::instance();
    rs.setViewport(ctx.viewport.x, ctx.viewport.y, ctx.viewport.z, ctx.viewport.w);

    // Scissor — only enabled when the pass opts in
    if (scissorTest)
    {
        const bool hasCustomRect = (scissorRect.z > 0 && scissorRect.w > 0);
        const glm::ivec4 &sr = hasCustomRect ? scissorRect : ctx.viewport;
        rs.setScissorTest(true);
        rs.setScissor(sr.x, sr.y, sr.z, sr.w);
    }
    else
    {
        rs.setScissorTest(false);
    }

    rs.setDepthTest(depthTest);
    rs.setDepthWrite(depthWrite);
    rs.setCull(cull);
    rs.setBlend(blend);
    if (blend)
        rs.setBlendFunc(blendSrc, blendDst);
    if (clearColor)
        rs.setClearColor(clearValue.r, clearValue.g, clearValue.b, clearValue.a);
    rs.clear(clearColor, clearDepth);

    std::vector<RenderItem> *items = nullptr;
    if      (passMask & RenderPassMask::Opaque)      items = &queue.getOpaque();
    else if (passMask & RenderPassMask::Transparent) items = &queue.getTransparent();
    else if (passMask & RenderPassMask::Unlit)       items = &queue.getUnlit();
    else if (passMask & RenderPassMask::Outline)     items = &queue.getOutline();
    else if (passMask & RenderPassMask::Overlay)     items = &queue.getOverlay();
    if (!items || items->empty())
        return;

    if (sortMode != RenderSortMode::None)
    {
        const glm::vec3 camPos = ctx.camera->position;
        for (auto &item : *items)
            item.depth = glm::distance(glm::vec3(item.model[3]), camPos);
    }
    if (sortMode == RenderSortMode::FrontToBack)
        std::sort(items->begin(), items->end(), [](const RenderItem &a, const RenderItem &b)
                  {
            if (a.material != b.material) return a.material < b.material;
            if (a.drawable     != b.drawable)     return a.drawable     < b.drawable;
            return a.depth < b.depth; });
    else if (sortMode == RenderSortMode::BackToFront)
        std::sort(items->begin(), items->end(), [](const RenderItem &a, const RenderItem &b)
                  { return a.depth > b.depth; });

    const glm::mat4 &view     = ctx.camera->view;
    const glm::mat4 &proj     = ctx.camera->projection;
    const glm::mat4 &viewProj = ctx.camera->viewProjection;
    const glm::vec4  camPos   = glm::vec4(ctx.camera->position, 1.f);

    if (ctx.stats)
        ctx.stats->objects += static_cast<uint32_t>(items->size());

    if (shader)
    {
        // Pass has its own shader — bind once, draw all items (shadow / depth / outline)
        rs.useProgram(shader->getId());
        shader->setMat4("u_view",     view);
        shader->setMat4("u_proj",     proj);
        shader->setMat4("u_viewProj", viewProj);
        shader->setVec4("u_cameraPos", camPos);
        sendClipPlanes(shader, ctx);
        sendLights(shader, ctx);
        for (const auto &item : *items)
            drawItem(ctx, item, shader);
    }
    else
    {
        // Defer to each material's shader — batch by shader to minimise program switches
        Shader         *lastShader   = nullptr;
        const Material *lastMaterial = nullptr;
        for (const auto &item : *items)
        {
            if (!item.material)
                continue;
            Shader *sh = item.material->getShader();
            if (!sh)
                continue;
            if (sh != lastShader)
            {
                rs.useProgram(sh->getId());
                sh->setMat4("u_view",     view);
                sh->setMat4("u_proj",     proj);
                sh->setMat4("u_viewProj", viewProj);
                sh->setVec4("u_cameraPos", camPos);
                sendClipPlanes(sh, ctx);
                sendLights(sh, ctx);
                lastShader   = sh;
                lastMaterial = nullptr; // force re-bind on shader change
                if (ctx.stats) ctx.stats->shaderChanges++;
            }
            if (item.material != lastMaterial)
            {
                item.material->applyStates();
                int txCount = item.material->bindTexturesTo(sh);
                item.material->applyUniformsTo(sh);
                lastMaterial = item.material;
                if (ctx.stats)
                {
                    ctx.stats->materialChanges++;
                    ctx.stats->textureBinds += static_cast<uint32_t>(txCount);
                }
            }
            drawItemNoMaterial(ctx, item, sh);
        }
    }
}

// drawItemNoMaterial: material state already applied — only bones + model matrix + draw
void RenderPass::drawItemNoMaterial(const FrameContext &ctx, const RenderItem &item, Shader *sh) const
{
    if (!item.drawable || !sh)
        return;
    item.drawable->applyBoneMatrices(sh);
    sh->setMat4("u_model", item.model);

    const uint32_t idxCount = item.indexCount > 0
                                  ? item.indexCount
                                  : static_cast<uint32_t>(item.drawable->indexCount());
    if (ctx.stats)
    {
        ctx.stats->drawCalls++;
        ctx.stats->triangles += (idxCount / 3) * static_cast<uint32_t>(item.instanceCount);
        ctx.stats->vertices  += static_cast<uint32_t>(item.drawable->vertexCount());
    }

    if (item.instanceCount > 1)
    {
        if (item.indexCount > 0)
            item.drawable->drawRangeInstanced(item.indexStart, item.indexCount, item.instanceCount);
        else
            item.drawable->drawInstanced(item.instanceCount);
    }
    else
    {
        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }
}

void RenderPass::drawItem(const FrameContext &ctx, const RenderItem &item, Shader *sh) const
{
    if (!item.drawable || !item.material || !sh)
        return;

    item.material->applyStates();
    item.material->bindTexturesTo(sh);
    item.material->applyUniformsTo(sh);
    item.drawable->applyBoneMatrices(sh);
    sh->setMat4("u_model", item.model);

    const uint32_t idxCount = item.indexCount > 0
                                  ? item.indexCount
                                  : static_cast<uint32_t>(item.drawable->indexCount());

    if (ctx.stats)
    {
        ctx.stats->drawCalls++;
        ctx.stats->triangles += (idxCount / 3) * static_cast<uint32_t>(item.instanceCount);
        ctx.stats->vertices  += static_cast<uint32_t>(item.drawable->vertexCount());
    }

    if (item.instanceCount > 1)
    {
        if (item.indexCount > 0)
            item.drawable->drawRangeInstanced(item.indexStart, item.indexCount, item.instanceCount);
        else
            item.drawable->drawInstanced(item.instanceCount);
    }
    else
    {
        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }
}

// ─── Concrete passes ──────────────────────────────────────────────────────────────────
OpaquePass::OpaquePass()
{
    clearColor = true;
    clearDepth = true;
    clearValue = {0.15f, 0.15f, 0.2f, 1.0f};
    depthTest = true;
    depthWrite = true;
    cull = true;
    blend = false;
    passMask = RenderPassMask::Opaque;
    sortMode = RenderSortMode::FrontToBack;
}

TransparentPass::TransparentPass()
{
    clearColor = false;
    clearDepth = false;
    depthTest = true;
    depthWrite = false;
    cull = false;
    blend = true;
    passMask = RenderPassMask::Transparent;
    sortMode = RenderSortMode::BackToFront;
}

UnlitPass::UnlitPass()
{
    clearColor = false;
    clearDepth = false;
    depthTest  = true;
    depthWrite = true;
    cull       = true;
    blend      = false;
    passMask   = RenderPassMask::Unlit;
    sortMode   = RenderSortMode::None;
}

OverlayPass::OverlayPass()
{
    clearColor = false;
    clearDepth = false;
    depthTest  = false;
    depthWrite = false;
    cull       = false;
    blend      = true;
    passMask   = RenderPassMask::Overlay;
    sortMode   = RenderSortMode::BackToFront;
}

OutlinePass::OutlinePass()
{
    clearColor = false;
    clearDepth = false;
    depthTest  = true;
    depthWrite = false;
    cull       = false;  // draw back faces
    blend      = false;
    passMask   = RenderPassMask::Outline;
    sortMode   = RenderSortMode::None;
}

void OutlinePass::execute(const FrameContext &ctx, RenderQueue &queue) const
{
    auto &items = queue.getOutline();
    if (items.empty() || !shader || !ctx.camera) return;

    auto &rs = RenderState::instance();
    rs.setViewport(ctx.viewport.x, ctx.viewport.y, ctx.viewport.z, ctx.viewport.w);
    rs.setDepthTest(true);
    rs.setDepthWrite(false);
    rs.setCull(true);
    // Draw only back faces so front face normals extrude outward
    glCullFace(GL_FRONT);
    rs.setBlend(false);

    const glm::mat4 &viewProj = ctx.camera->viewProjection;
    rs.useProgram(shader->getId());
    shader->setMat4("u_viewProj",     viewProj);
    shader->setVec4("u_outlineColor", color);
    shader->setFloat("u_thickness",   thickness);

    for (const auto &item : items)
    {
        if (!item.drawable) continue;
        shader->setMat4("u_model", item.model);
        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }

    glCullFace(GL_BACK); // restore
}

// ─── SkyPass ───────────────────────────────────────────────────────────────────────
void SkyPass::execute(const FrameContext &ctx, RenderQueue &) const
{
    if (!shader) return;

    auto &rs = RenderState::instance();
    rs.setDepthTest(true);
    rs.setDepthWrite(false);
    rs.setCull(false);
    rs.setBlend(false);
    // LEQUAL: sky fragment at depth=1.0 passes for pixels with no geometry
    glDepthFunc(GL_LEQUAL);

    if (dummyVao_ == 0)
        glGenVertexArrays(1, &dummyVao_);
    glBindVertexArray(dummyVao_);

    rs.useProgram(shader->getId());
    shader->setVec3("u_skyTop",      skyTop);
    shader->setVec3("u_skyHorizon",  skyHorizon);
    shader->setVec3("u_groundColor", groundColor);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glDepthFunc(GL_LESS);   // restore default
    rs.setCull(true);
    rs.setDepthWrite(true);
}

// ─── Technique ──────────────────────────────────────────────────────────────────
void Technique::render(const FrameContext &ctx, RenderQueue &queue) const
{
    for (auto *p : passes)
        p->execute(ctx, queue);
}

// ─── ForwardTechnique ──────────────────────────────────────────────────────────────────
ForwardTechnique::ForwardTechnique()
{
    name = "Forward";
    opaque_ = addPass<OpaquePass>();
    transparent_ = addPass<TransparentPass>();
}

// ─── GBufferPass ──────────────────────────────────────────────────────────────────────
GBufferPass::GBufferPass()
{
    clearColor = true;
    clearDepth = true;
    clearValue = {0.f, 0.f, 0.f, 1.f};
    depthTest  = true;
    depthWrite = true;
    cull       = true;
    blend      = false;
    passMask   = RenderPassMask::Opaque;
    sortMode   = RenderSortMode::FrontToBack;
}

void GBufferPass::execute(const FrameContext &ctx, RenderQueue &queue) const
{
    if (!gbuffer || !gbuffer->valid() || !shader || !ctx.camera)
        return;

    gbuffer->bind();

    auto &rs = RenderState::instance();
    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setCull(true);
    rs.setBlend(false);
    rs.setClearColor(clearValue.r, clearValue.g, clearValue.b, clearValue.a);
    rs.clear(true, true);

    rs.useProgram(shader->getId());
    shader->setMat4("u_view", ctx.camera->view);
    shader->setMat4("u_proj", ctx.camera->projection);

    auto &items = queue.getOpaque();
    for (const auto &item : items)
        drawItem(ctx, item, shader);

    gbuffer->unbind();
}

// ─── DeferredLightingPass ─────────────────────────────────────────────────────────────
DeferredLightingPass::DeferredLightingPass()
{
    clearColor = true;
    clearDepth = false;
    clearValue = {0.f, 0.f, 0.f, 1.f};
    depthTest  = false;
    depthWrite = false;
    cull       = false;
    blend      = false;

    // Fullscreen triangle needs a bound VAO even though it uses no attributes
    glGenVertexArrays(1, &dummyVAO_);
}

DeferredLightingPass::~DeferredLightingPass()
{
    if (dummyVAO_) { glDeleteVertexArrays(1, &dummyVAO_); dummyVAO_ = 0; }
}

void DeferredLightingPass::execute(const FrameContext &ctx, RenderQueue &/*queue*/) const
{
    if (!gbuffer || !shader || !ctx.camera)
        return;

    auto &rs = RenderState::instance();
    rs.setViewport(ctx.viewport.x, ctx.viewport.y, ctx.viewport.z, ctx.viewport.w);
    rs.setDepthTest(false);
    rs.setDepthWrite(false);
    rs.setCull(false);
    rs.setBlend(false);
    rs.setClearColor(clearValue.r, clearValue.g, clearValue.b, clearValue.a);
    rs.clear(true, false);

    // Bind GBuffer textures to units 0-2
    rs.bindTexture(0, GL_TEXTURE_2D, gbuffer->gPosition);
    rs.bindTexture(1, GL_TEXTURE_2D, gbuffer->gNormal);
    rs.bindTexture(2, GL_TEXTURE_2D, gbuffer->gAlbedoSpec);

    rs.useProgram(shader->getId());
    shader->setInt("gPosition",   0);
    shader->setInt("gNormal",     1);
    shader->setInt("gAlbedoSpec", 2);

    shader->setVec3("u_viewPos", ctx.camera->position);

    // Directional light — from scene lights (first DirectionalLight found)
    sendLights(shader, ctx);

    // Point lights from scene (sendLights handles them, but deferred has its own uniforms)
    // sendLights already sent u_pointPos/u_pointColor/u_pointRadius/u_numPointLights

    // Draw fullscreen triangle — no vertex data, positions from gl_VertexID
    glBindVertexArray(dummyVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// ─── DeferredTechnique ────────────────────────────────────────────────────────────────
DeferredTechnique::DeferredTechnique()
{
    name = "Deferred";

    gbuffer_ = new GBuffer();
    if (!gbuffer_->initialize(1024, 768))
    {
        delete gbuffer_;
        gbuffer_ = nullptr;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[DeferredTechnique] GBuffer init failed");
        return;
    }

    auto *gp = addPass<GBufferPass>();
    gp->gbuffer = gbuffer_;

    auto *lp = addPass<DeferredLightingPass>();
    lp->gbuffer = gbuffer_;

    addPass<TransparentPass>();
}

DeferredTechnique::~DeferredTechnique()
{
    delete gbuffer_;
}

void DeferredTechnique::onResize(unsigned int w, unsigned int h)
{
    if (gbuffer_) gbuffer_->resize(w, h);
}

void DeferredTechnique::render(const FrameContext &ctx, RenderQueue &queue) const
{
    if (passes.size() < 3) return;

    geometryPass() ->execute(ctx, queue);
    lightingPass() ->execute(ctx, queue);
    transparentPass()->execute(ctx, queue);
}

GBufferPass          *DeferredTechnique::geometryPass()    const { return passes.size() > 0 ? static_cast<GBufferPass*>          (passes[0]) : nullptr; }
DeferredLightingPass *DeferredTechnique::lightingPass()    const { return passes.size() > 1 ? static_cast<DeferredLightingPass*> (passes[1]) : nullptr; }
TransparentPass      *DeferredTechnique::transparentPass() const { return passes.size() > 2 ? static_cast<TransparentPass*>      (passes[2]) : nullptr; }

// ─── Pipeline ────────────────────────────────────────────────────────────────

Pipeline *Pipeline::create(const std::string &name)
{
    auto *p = new Pipeline();
    p->name_ = name;
    return p;
}

void Pipeline::destroy()
{
    for (auto *t : techniques_)
        delete t;
    techniques_.clear();
    current_ = nullptr;
}

Technique *Pipeline::ensureTechnique()
{
    if (!current_)
        newTechnique();
    return current_;
}

Pipeline *Pipeline::newTechnique(const std::string &techName)
{
    auto *t  = new Technique();
    t->name  = techName.empty() ? name_ : techName;
    techniques_.push_back(t);
    current_ = t;
    return this;
}

Pipeline *Pipeline::opaque()
{
    ensureTechnique()->addPass<OpaquePass>();
    return this;
}

Pipeline *Pipeline::transparent()
{
    ensureTechnique()->addPass<TransparentPass>();
    return this;
}

Pipeline *Pipeline::unlit()
{
    ensureTechnique()->addPass<UnlitPass>();
    return this;
}

Pipeline *Pipeline::overlay()
{
    ensureTechnique()->addPass<OverlayPass>();
    return this;
}

Pipeline *Pipeline::sky(Shader *sh, glm::vec3 top, glm::vec3 horizon, glm::vec3 ground)
{
    auto *p        = ensureTechnique()->addPass<SkyPass>();
    p->shader      = sh;
    p->skyTop      = top;
    p->skyHorizon  = horizon;
    p->groundColor = ground;
    return this;
}

Pipeline *Pipeline::outline(Shader *sh, glm::vec4 color, float thickness)
{
    auto *p       = ensureTechnique()->addPass<OutlinePass>();
    p->shader     = sh;
    p->color      = color;
    p->thickness  = thickness;
    return this;
}

Pipeline *Pipeline::addPass(RenderPass *pass)
{
    ensureTechnique()->passes.push_back(pass);
    return this;
}

Technique *Pipeline::build(Scene *scene)
{
    Technique *first = techniques_.empty() ? nullptr : techniques_.front();
    for (auto *t : techniques_)
        scene->addTechnique(t);
    // Transfer ownership to scene — we no longer own them
    techniques_.clear();
    current_ = nullptr;
    return first;
}
