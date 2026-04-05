#pragma once

#include "RenderScene.hpp"
#include "RenderTarget.hpp"

class Scene;
class Shader;
class Material;
class DirectionalLight;
class WaterNode3D;
class Node;

class Renderer
{
public:
    bool initialize();
    void render(Scene &scene, Camera *camera);
    void setShadowBias(float bias) { shadowBias_ = bias; }
    float shadowBias() const { return shadowBias_; }

private:
    static constexpr int MaxClipPlanes = 4;

    bool initialized_ = false;
    GLuint skyVao_ = 0;
    Shader *solidShader_ = nullptr;
    Shader *texturedShader_ = nullptr;
    Shader *detailShader_ = nullptr;
    Shader *terrainShader_ = nullptr;
    Shader *waterShader_ = nullptr;
    Shader *skyShader_ = nullptr;
    Shader *skinnedShader_ = nullptr;
    Shader *shadowStaticShader_ = nullptr;
    Shader *shadowSkinnedShader_ = nullptr;
    RenderTarget shadowTarget_;
    glm::mat4 lightSpaceMatrix_ = glm::mat4(1.0f);
    bool shadowPassReady_ = false;
    float shadowBias_ = 0.0035f;

    void renderList(const RenderScene &scene,
                    std::vector<RenderObject> &items,
                    bool backToFront,
                    const glm::vec4 *clipPlanes = nullptr,
                    int clipPlaneCount = 0,
                    bool allowShadows = true);
    void renderSky(const RenderScene &scene);
    void renderShadowPass(const RenderScene &scene);
    void renderSceneToTarget(Scene &scene,
                             Camera *camera,
                             RenderTarget *target,
                             const Node *ignoredNode = nullptr,
                             const glm::vec4 *clipPlanes = nullptr,
                             int clipPlaneCount = 0);
    void updateWaterTargets(Scene &scene, const RenderScene &sceneView);
    Shader *resolveShader(const Material *material) const;
    const DirectionalLight *primaryShadowLight(const RenderScene &scene) const;
};
