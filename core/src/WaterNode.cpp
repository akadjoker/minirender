#include "WaterNode.hpp"
#include "Camera.hpp"
#include "Manager.hpp"

WaterNode3D::WaterNode3D(const std::string &nodeName)
{
    name = nodeName;
    type = NodeType::ManualMesh;
    castShadow = false;
    receiveShadow = true;
}

WaterNode3D::~WaterNode3D()
{
    release();
}

bool WaterNode3D::init(float width, float depth)
{
    release();

    reflectionRT_ = new RenderTarget();
    reflectionRT_->create(rtWidth, rtHeight);
    reflectionRT_->addColorAttachment();
    reflectionRT_->addDepthAttachment();
    if (!reflectionRT_->finalize())
        return false;

    refractionRT_ = new RenderTarget();
    refractionRT_->create(rtWidth, rtHeight);
    refractionRT_->addColorAttachment();
    refractionRT_->addDepthTexture();
    if (!refractionRT_->finalize())
        return false;

    auto &textures = TextureManager::instance();
    auto &materials = MaterialManager::instance();

    Texture *waterBumpTex = !waterBumpPath.empty()
        ? textures.load(name + "_water_bump", waterBumpPath)
        : textures.getWhite();
    Texture *foamTex = !foamPath.empty()
        ? textures.load(name + "_water_foam", foamPath)
        : textures.getWhite();

    material_ = materials.createWater(name + "_water_mat");
    if (!material_)
        return false;

    material_->setTexture("u_reflection", reflectionRT_->colorTex())
             ->setTexture("u_refraction", refractionRT_->colorTex())
             ->setTexture("u_refrDepth", refractionRT_->depthTex() ? refractionRT_->depthTex() : textures.getWhite())
             ->setTexture("u_waterBump", waterBumpTex ? waterBumpTex : textures.getWhite())
             ->setTexture("u_foamTexture", foamTex ? foamTex : textures.getWhite())
             ->setFloat("u_time", time_)
             ->setFloat("u_distortStrength", distortStrength)
             ->setFloat("u_reflectivity", reflectivity)
             ->setFloat("u_waveHeight", waveHeight)
             ->setFloat("u_waveLength", waveLength)
             ->setFloat("u_windForce", windForce)
             ->setVec2("u_windDirection", windDirection)
             ->setVec4("u_wave1", wave1)
             ->setVec4("u_wave2", wave2)
             ->setVec4("u_wave3", wave3)
             ->setVec4("u_wave4", wave4)
             ->setFloat("u_foamScale", foamScale)
             ->setFloat("u_foamSpeed", foamSpeed)
             ->setFloat("u_foamIntensity", foamIntensity)
             ->setFloat("u_foamRange", foamRange)
             ->setFloat("u_depthMult", depthMult)
             ->setVec4("u_waterColor", waterColor)
             ->setFloat("u_colorBlendFactor", colorBlendFactor)
             ->setFloat("u_waterLevel", waterHeight())
             ->setFloat("u_shoreFadeRange", shoreFadeRange)
             ->setFloat("u_depthDiscardCutoff", depthDiscardCutoff)
             ->setInt("u_foamEnabled", foamEnabled ? 1 : 0);

    buildGrid(width, depth,256);
    return true;
}

void WaterNode3D::release()
{
    buffer_.free();
    delete reflectionRT_;
    reflectionRT_ = nullptr;
    delete refractionRT_;
    refractionRT_ = nullptr;
    material_ = nullptr;
}

void WaterNode3D::update(float dt)
{
    time_ += dt;
    if (material_)
        material_->setFloat("u_time", time_);
}

void WaterNode3D::updateCameraUniforms(const Camera *camera)
{
    if (!camera || !material_)
        return;

    material_->setFloat("u_near", camera->nearPlane);
    material_->setFloat("u_far", camera->farPlane);
    material_->setFloat("u_waterLevel", waterHeight());
}

Texture *WaterNode3D::debugReflTex() const
{
    return reflectionRT_ ? reflectionRT_->colorTex() : nullptr;
}

Texture *WaterNode3D::debugRefrTex() const
{
    return refractionRT_ ? refractionRT_->colorTex() : nullptr;
}

Texture *WaterNode3D::debugRefrDepthTex() const
{
    return refractionRT_ ? refractionRT_->depthTex() : nullptr;
}

void WaterNode3D::buildGrid(float width, float depth, int subdivs)
{
    buffer_.vertices.clear();
    buffer_.indices.clear();
    buffer_.mode = GL_TRIANGLES;

    int vertsX = subdivs + 1;
    int vertsZ = subdivs + 1;
    float stepX = width / static_cast<float>(subdivs);
    float stepZ = depth / static_cast<float>(subdivs);
    float halfX = width * 0.5f;
    float halfZ = depth * 0.5f;

    buffer_.vertices.resize(static_cast<size_t>(vertsX) * vertsZ);
    buffer_.indices.resize(static_cast<size_t>(subdivs) * subdivs * 6);

    for (int z = 0; z < vertsZ; ++z)
    {
        for (int x = 0; x < vertsX; ++x)
        {
            int i = z * vertsX + x;
            buffer_.vertices[i] = {
                {-halfX + x * stepX, 0.0f, -halfZ + z * stepZ},
                {0.0f, 1.0f, 0.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {(static_cast<float>(x) / subdivs) * uvTile,
                 (static_cast<float>(z) / subdivs) * uvTile}
            };
        }
    }

    int idx = 0;
    for (int z = 0; z < subdivs; ++z)
    {
        for (int x = 0; x < subdivs; ++x)
        {
            uint32_t tl = z * vertsX + x;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + vertsX;
            uint32_t br = bl + 1;
            buffer_.indices[idx++] = tl;
            buffer_.indices[idx++] = bl;
            buffer_.indices[idx++] = tr;
            buffer_.indices[idx++] = tr;
            buffer_.indices[idx++] = bl;
            buffer_.indices[idx++] = br;
        }
    }

    buffer_.aabb.min = {-halfX, -0.1f, -halfZ};
    buffer_.aabb.max = { halfX,  0.1f,  halfZ};
    buffer_.upload();
}
