#include "PipelinePresets.hpp"

#include "Scene.hpp"

namespace PipelinePresets
{
WorldForwardPipeline createWorldForward(Scene &scene, const WorldForwardDesc &desc)
{
    WorldForwardPipeline out;

    auto *mainTech = new CsmTechnique();
    mainTech->name = "WorldForward";
    mainTech->litShader = desc.litShader;

    if (!mainTech->initialize(desc.shadowResolution))
    {
        delete mainTech;
        return out;
    }

    if (CascadeShadowMap *csm = mainTech->getCsm())
    {
        csm->setLightDirection(desc.lightDirection);
        csm->setLambda(desc.csmLambda);
        csm->setShadowFarPlane(desc.shadowFarPlane);
    }

    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        auto *depth = mainTech->addPass<CsmDepthPass>();
        depth->csm             = mainTech->getCsm();
        depth->cascade         = i;
        depth->shader          = desc.depthShader;
        depth->instancedShader = desc.instancedDepthShader;
    }

    if (desc.skyShader)
    {
        auto *sky = mainTech->addPass<SkyPass>();
        sky->shader      = desc.skyShader;
        sky->skyTop      = desc.skyTop;
        sky->skyHorizon  = desc.skyHorizon;
        sky->groundColor = desc.groundColor;
    }

    mainTech->addPass<OpaquePass>();
    mainTech->addPass<TransparentPass>();

    if (OpaquePass *opaque = mainTech->getOpaquePass())
        opaque->clearColor = (desc.skyShader == nullptr);

    scene.addTechnique(mainTech);
    out.mainTechnique = mainTech;
    return out;
}

IndoorHybridPipeline createIndoorHybrid(Scene &scene, const IndoorHybridDesc &desc)
{
    IndoorHybridPipeline out;

    auto *mainTech = new DeferredTechnique();
    mainTech->name = "IndoorHybrid";

    if (GBufferPass *geometry = mainTech->geometryPass())
        geometry->shader = desc.gbufferShader;

    if (DeferredLightingPass *lighting = mainTech->lightingPass())
    {
        lighting->shader     = desc.lightingShader;
        lighting->clearValue = desc.clearColor;
    }

    scene.addTechnique(mainTech);
    out.mainTechnique = mainTech;
    return out;
}
}