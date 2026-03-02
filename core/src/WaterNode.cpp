#include "WaterNode.hpp"
#include "Scene.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <SDL2/SDL.h>
#include <string>

// ─── init / release ─────────────────────────────────────────────────────────

bool WaterNode3D::init(float width, float depth, Shader *waterShader)
{
    if (!waterShader)
    {
        SDL_Log("[WaterNode3D] waterShader is null");
        return false;
    }

    // Create reflection + refraction render targets
    const std::string baseName = name.empty() ? "water" : name;

    auto &tex = TextureManager::instance();

    reflRT_ = new RenderTarget();
    reflRT_->create(rtWidth, rtHeight);
    reflRT_->addColorAttachment();
    reflRT_->addDepthAttachment();
    if (!reflRT_->finalize())
    {
        SDL_Log("[WaterNode3D] Failed to create reflection render target");
        return false;
    }

    refrRT_ = new RenderTarget();
    refrRT_->create(rtWidth, rtHeight);
    refrRT_->addColorAttachment();
    refrRT_->addDepthTexture();
    if (!refrRT_->finalize())
    {
        SDL_Log("[WaterNode3D] Failed to create refraction render target");
        return false;
    }

    // ── Optional textures (waterbump + foam) ──────────────────
    Texture *waterBumpTex = !waterBumpPath.empty()
                            ? tex.load(baseName + "_bump", waterBumpPath)
                            : tex.getWhite();
    Texture *foamTex      = !foamPath.empty()
                            ? tex.load(baseName + "_foam", foamPath)
                            : tex.getWhite();

    // ── Material ────────────────────────────────────────────
    auto &mats = MaterialManager::instance();
    material_ = mats.create(baseName + "_mat");
    material_->setShader(waterShader)
             ->setBlend(false)
             ->setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
             ->setDepthTest(true)
             ->setDepthWrite(true)
             ->setCullFace(false)
             ->setTexture("u_reflection",      reflRT_->colorTex())
             ->setTexture("u_refraction",      refrRT_->colorTex())
             ->setTexture("u_refrDepth",       refrRT_->depthTex() ? refrRT_->depthTex() : tex.getWhite())
             ->setTexture("u_waterBump",       waterBumpTex)
             ->setTexture("u_foamTexture",     foamTex)
             ->setFloat("u_time",              0.f)
             ->setFloat("u_distortStrength", distortStrength)
             ->setFloat("u_reflectivity",      reflectivity)
             ->setFloat("u_waveHeight",        waveHeight)
             ->setFloat("u_waveLength",        waveLength)
             ->setFloat("u_windForce",         windForce)
             ->setVec2 ("u_windDirection",     windDirection)
             ->setVec4 ("u_wave1",             wave1)
             ->setVec4 ("u_wave2",             wave2)
             ->setVec4 ("u_wave3",             wave3)
             ->setVec4 ("u_wave4",             wave4)
             ->setFloat("u_foamScale",         foamScale)
             ->setFloat("u_foamSpeed",         foamSpeed)
             ->setFloat("u_foamIntensity",     foamIntensity)
             ->setFloat("u_foamRange",         foamRange)
             ->setFloat("u_depthMult",         depthMult)
             ->setVec4 ("u_waterColor",        waterColor)
             ->setFloat("u_colorBlendFactor",  colorBlendFactor);

 

    // ── Geometry ─────────────────────────────────────────────
    buildQuad(width, depth);
    type = NodeType::ManualMesh; // so gatherNode traverses children
    return true;
}

void WaterNode3D::release()
{
    buffer_.free();
    delete reflRT_;  reflRT_ = nullptr;
    delete refrRT_;  refrRT_ = nullptr;
}

// ─── geometry ───────────────────────────────────────────────────────────────

void WaterNode3D::buildQuad(float width, float depth)
{
    

    buffer_.vertices.clear();
    buffer_.indices.clear();
    buffer_.mode = GL_TRIANGLES;


    int subdivs = 264;
    int verts_x = subdivs + 1;
    int verts_z = subdivs + 1;
    float step_x = width / subdivs;
    float step_z = depth / subdivs;
    float half_x = width * 0.5f;
    float half_z = depth * 0.5f;

    int vert_count = verts_x * verts_z;
    int idx_count = subdivs * subdivs * 6;
 
    buffer_.vertices.resize(vert_count);
    buffer_.indices.resize(idx_count);

    // vértices
    for (int z = 0; z < verts_z; z++)
    {
        for (int x = 0; x < verts_x; x++)
        {
            int i = z * verts_x + x;
            buffer_.vertices[i] = {
                {-half_x + x * step_x, 0.0f, -half_z + z * step_z},
                {0, 1, 0},
                {1, 0, 0, 1},
                {(float)x / subdivs, (float)z / subdivs}};
        }
    }

    // índices
    int idx = 0;
    for (int z = 0; z < subdivs; z++)
    {
        for (int x = 0; x < subdivs; x++)
        {
            uint32_t tl = z * verts_x + x;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + verts_x;
            uint32_t br = bl + 1;
            buffer_.indices[idx++] = tl;
            buffer_.indices[idx++] = bl;
            buffer_.indices[idx++] = tr;
            buffer_.indices[idx++] = tr;
            buffer_.indices[idx++] = bl;
            buffer_.indices[idx++] = br;
        }
    }


    buffer_.aabb.min = {-half_x, -0.1f, -half_z};
    buffer_.aabb.max = { half_x,  0.1f,  half_z};

    buffer_.upload();
}

// ─── update ─────────────────────────────────────────────────────────────────

void WaterNode3D::update(float dt)
{
    time_ += dt;
    if (material_)
        material_->setFloat("u_time", time_);
}

// ─── preRender ──────────────────────────────────────────────────────────────

void WaterNode3D::preRender(Scene *scene, const Camera *mainCam)
{
    if (!material_ || rendering_) return; // recursion guard

    rendering_ = true;

    const float waterY = worldPosition().y;
    const glm::vec3 camPos = mainCam->position;

    glm::vec3 euler = mainCam->getEulerAngles();
    euler.x = -euler.x; // negate pitch

    const glm::vec3 reflPos = {camPos.x, 2.f * waterY - camPos.y, camPos.z};

    Camera reflCam;
    reflCam.setPosition(reflPos);
    reflCam.setEulerAngles(euler);
    reflCam.fov           = mainCam->fov;
    reflCam.nearPlane     = mainCam->nearPlane;
    reflCam.farPlane      = mainCam->farPlane;
    reflCam.viewport      = mainCam->viewport;
    reflCam.setAspect(mainCam->viewport.z, mainCam->viewport.w);
    reflCam.updateMatrices();
    reflCam.clearColor    = mainCam->clearColor;
    reflCam.clearDepth    = mainCam->clearDepth;
    reflCam.clearColorVal = mainCam->clearColorVal;



    scene->renderToTarget(&reflCam, reflRT_, {0.f, 1.f, 0.f, 0.f});

    Camera *mutableCam = const_cast<Camera *>(mainCam);
    scene->renderToTarget(mutableCam, refrRT_, {0.f, -1.f, 0.f, 0.f});

    rendering_ = false;
}

// ─── gatherRenderItems ───────────────────────────────────────────────────────

void WaterNode3D::gatherRenderItems(RenderQueue &q, const FrameContext &ctx)
{
    // Don't draw the water surface in secondary (reflection/refraction) renders
    if (ctx.secondary) return;
    if (!visible || !material_) return;

    const glm::mat4  world     = worldMatrix();
    const BoundingBox worldAABB = buffer_.aabb.transformed(world);

    if (worldAABB.is_valid() && !ctx.frustum.contains(worldAABB))
        return;

    RenderItem item;
    item.drawable  = &buffer_;
    item.material  = material_;
    item.model     = world;
    item.worldAABB = worldAABB;
    item.passMask  = RenderPassMask::Transparent;
    q.add(item);
}
