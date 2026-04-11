#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Demo.hpp"
#include "Manager.hpp"
#include "TerrainNode.hpp"
#include "imgui.h"

class TerrainDemo : public IDemo
{
public:
    TerrainDemo();

    virtual const char *title() const override;
    virtual const char *description() const override;

    virtual bool setup(Device &device) override;
    virtual void update(float dt) override;
    virtual void drawGui() override;
    virtual void render() override;
    virtual void shutdown() override;

private:
    enum TerrainCase
    {
        TerrainHeightmap = 0,
        TerrainLod = 1,
        TerrainTiled = 2,
        TerrainInfinite = 3
    };

    struct ProbeRange
    {
        float minX;
        float maxX;
        float minZ;
        float maxZ;
    };

    static Shader *createTerrainShader();
    static Shader *createProbeShader();
    static void setupLighting(Shader *shader);
    static Texture *createTileAtlasTexture(const std::string &name, int tilesPerSide, int tileSize);
    static const char *terrainCaseName(int mode);
    static const char *terrainCaseDescription(int mode);

    std::string assetPath(const char *relativePath) const;
    std::string pickFirstExistingAsset(const std::vector<const char *> &relativePaths) const;
    bool setupTerrainAssets();
    bool setupScene();
    void buildTiledTerrainData(std::vector<uint8_t> &tileMap, uint32_t width, uint32_t height) const;
    void setActiveTerrain(int mode, bool resetCamera);
    void setProbeToCurrentTerrainCenter();
    void resetCameraForCurrentTerrain();
    void syncProbe();
    ProbeRange probeRange() const;
    float clampFloat(float value, float minValue, float maxValue) const;

    Device *device_;
    std::string assetRoot_;
    Scene scene_;
    Camera *camera_;

    Shader *terrainShader_;
    Shader *probeShader_;

    Texture *terrainBaseTexture_;
    Texture *terrainDetailTexture_;
    Texture *tileAtlasTexture_;

    Material terrainMaterial_;
    Material tiledMaterial_;
    Material probeMaterial_;

    TerrainNode *terrainNode_;
    TerrainLodNode *terrainLodNode_;
    TiledTerrainNode *tiledTerrainNode_;
    InfiniteTerrainNode *infiniteTerrainNode_;
    MeshNode *probeNode_;

    RenderBatch debugBatch_;

    int activeTerrain_;
    bool showProbe_;
    bool showLodDebug_;
    float probeX_;
    float probeZ_;
    float sampledHeight_;
    glm::vec3 sampledNormal_;
};

inline TerrainDemo::TerrainDemo()
    : device_(nullptr),
      camera_(nullptr),
      terrainShader_(nullptr),
      probeShader_(nullptr),
      terrainBaseTexture_(nullptr),
      terrainDetailTexture_(nullptr),
      tileAtlasTexture_(nullptr),
      terrainNode_(nullptr),
      terrainLodNode_(nullptr),
      tiledTerrainNode_(nullptr),
      infiniteTerrainNode_(nullptr),
      probeNode_(nullptr),
      activeTerrain_(TerrainHeightmap),
      showProbe_(true),
      showLodDebug_(true),
      probeX_(0.0f),
      probeZ_(0.0f),
      sampledHeight_(0.0f),
      sampledNormal_(0.0f, 1.0f, 0.0f)
{
}

inline const char *TerrainDemo::title() const
{
    return "Terrain Tests";
}

inline const char *TerrainDemo::description() const
{
    return "Teste visual do pipeline novo para TerrainNode, TerrainLodNode, TiledTerrainNode e InfiniteTerrainNode.";
}

inline bool TerrainDemo::setup(Device &device)
{
    device_ = &device;
    assetRoot_ = findProjectAssetRoot();

    terrainShader_ = createTerrainShader();
    probeShader_ = createProbeShader();
    if (!terrainShader_ || !probeShader_)
    {
        shutdown();
        return false;
    }

    if (!setupTerrainAssets() || !setupScene())
    {
        shutdown();
        return false;
    }

    debugBatch_.Init();
    setActiveTerrain(TerrainHeightmap, true);
    return true;
}

inline void TerrainDemo::update(float dt)
{
    if (!device_ || !camera_)
        return;

    if (device_->IsResize())
        camera_->setViewport(0, 0, device_->GetWidth(), device_->GetHeight());

    scene_.update(dt);
    syncProbe();
}

inline void TerrainDemo::drawGui()
{
    ImGui::SetNextWindowPos(ImVec2(16, 72), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Once);
    if (!ImGui::Begin("Terrain Tests"))
    {
        ImGui::End();
        return;
    }

    int selected = activeTerrain_;
    if (ImGui::BeginCombo("Teste", terrainCaseName(activeTerrain_)))
    {
        for (int i = 0; i < 4; ++i)
        {
            const bool isSelected = (i == activeTerrain_);
            if (ImGui::Selectable(terrainCaseName(i), isSelected))
                selected = i;
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (selected != activeTerrain_)
        setActiveTerrain(selected, true);

    ImGui::TextWrapped("%s", terrainCaseDescription(activeTerrain_));
    ImGui::TextWrapped("Assets root: %s", assetRoot_.c_str());

    if (ImGui::Button("Reset Camera"))
        resetCameraForCurrentTerrain();

    ImGui::Checkbox("Show Probe", &showProbe_);
    if (activeTerrain_ == TerrainLod)
        ImGui::Checkbox("Show LOD Debug", &showLodDebug_);

    ProbeRange range = probeRange();
    if (ImGui::SliderFloat("Probe X", &probeX_, range.minX, range.maxX))
        syncProbe();
    if (ImGui::SliderFloat("Probe Z", &probeZ_, range.minZ, range.maxZ))
        syncProbe();

    ImGui::SeparatorText("Sample");
    ImGui::Text("Height: %.3f", sampledHeight_);
    ImGui::Text("Normal: %.3f %.3f %.3f", sampledNormal_.x, sampledNormal_.y, sampledNormal_.z);

    if (terrainNode_)
        ImGui::TextDisabled("TerrainNode: %s", terrainNode_->visible ? "active" : "hidden");
    if (terrainLodNode_)
        ImGui::TextDisabled("TerrainLodNode: %s", terrainLodNode_->visible ? "active" : "hidden");
    if (tiledTerrainNode_)
        ImGui::TextDisabled("TiledTerrainNode: %s", tiledTerrainNode_->visible ? "active" : "hidden");
    if (infiniteTerrainNode_)
        ImGui::TextDisabled("InfiniteTerrainNode: %s", infiniteTerrainNode_->visible ? "active" : "hidden");

    ImGui::End();
}

inline void TerrainDemo::render()
{
    if (!camera_)
        return;

    scene_.setCamera(camera_);
    scene_.beginPass();

    scene_.setShader(terrainShader_);
    setupLighting(terrainShader_);
    scene_.render(RenderType::Terrain);

    scene_.setShader(probeShader_);
    setupLighting(probeShader_);
    scene_.render(RenderType::Solid);

    scene_.endPass();

    if (activeTerrain_ == TerrainLod && showLodDebug_ && terrainLodNode_)
    {
        debugBatch_.SetMatrix(camera_->viewProjection);
        Material::applyDefaultStates();
        terrainLodNode_->debug(&debugBatch_);
        debugBatch_.Render();
    }
}

inline void TerrainDemo::shutdown()
{
    scene_.clear();
    unloadDemoAssets();

    camera_ = nullptr;
    terrainShader_ = nullptr;
    probeShader_ = nullptr;
    terrainBaseTexture_ = nullptr;
    terrainDetailTexture_ = nullptr;
    tileAtlasTexture_ = nullptr;
    terrainNode_ = nullptr;
    terrainLodNode_ = nullptr;
    tiledTerrainNode_ = nullptr;
    infiniteTerrainNode_ = nullptr;
    probeNode_ = nullptr;
    assetRoot_.clear();
}

inline Shader *TerrainDemo::createTerrainShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec2 uvDetail;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;
        out vec2 v_uv;
        out vec2 v_uvDetail;

        void main()
        {
            v_normal = normalize(u_normalMatrix * normal);
            v_uv = uv;
            v_uvDetail = uvDetail;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        in vec2 v_uvDetail;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform sampler2D u_detail;
        uniform vec3 u_lightDir;
        uniform vec3 u_ambient;
        uniform float u_detailBlend;

        void main()
        {
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec3 base = texture(u_albedo, v_uv).rgb;
            vec3 detail = texture(u_detail, v_uvDetail).rgb;
            vec3 albedo = mix(base, base * detail, u_detailBlend) * u_color.rgb;
            vec3 lit = albedo * (u_ambient + vec3(0.9 * diff));
            FragColor = vec4(lit, u_color.a);
        });

    return ShaderManager::instance().loadFromSource("terrain_demo_shader", vert, frag);
}

inline Shader *TerrainDemo::createProbeShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;
        out vec2 v_uv;

        void main()
        {
            v_normal = normalize(u_normalMatrix * normal);
            v_uv = uv;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform vec3 u_lightDir;
        uniform vec3 u_ambient;

        void main()
        {
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec3 albedo = texture(u_albedo, v_uv).rgb * u_color.rgb;
            vec3 lit = albedo * (u_ambient + vec3(0.85 * diff));
            FragColor = vec4(lit, u_color.a);
        });

    return ShaderManager::instance().loadFromSource("terrain_demo_probe_shader", vert, frag);
}

inline void TerrainDemo::setupLighting(Shader *shader)
{
    if (!shader)
        return;

    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.18f, 0.20f, 0.22f));
}

inline Texture *TerrainDemo::createTileAtlasTexture(const std::string &name, int tilesPerSide, int tileSize)
{
    const int width = tilesPerSide * tileSize;
    const int height = tilesPerSide * tileSize;
    std::vector<uint8_t> pixels((size_t)width * height * 4, 255);

    const uint8_t palette[16][3] = {
        {72, 108, 56}, {120, 92, 58}, {92, 92, 96}, {190, 180, 128},
        {62, 132, 118}, {140, 72, 72}, {164, 112, 60}, {88, 124, 164},
        {164, 164, 80}, {118, 74, 150}, {78, 154, 74}, {180, 98, 132},
        {88, 88, 88}, {152, 120, 92}, {86, 140, 176}, {176, 176, 176}
    };

    for (int ty = 0; ty < tilesPerSide; ++ty)
    {
        for (int tx = 0; tx < tilesPerSide; ++tx)
        {
            const int colorIndex = ty * tilesPerSide + tx;
            const uint8_t *base = palette[colorIndex % 16];
            for (int y = 0; y < tileSize; ++y)
            {
                for (int x = 0; x < tileSize; ++x)
                {
                    const bool checker = (((x / 4) + (y / 4)) % 2) == 0;
                    const float shade = checker ? 1.0f : 0.82f;
                    const int px = tx * tileSize + x;
                    const int py = ty * tileSize + y;
                    const size_t index = ((size_t)py * width + px) * 4;
                    pixels[index + 0] = (uint8_t)(base[0] * shade);
                    pixels[index + 1] = (uint8_t)(base[1] * shade);
                    pixels[index + 2] = (uint8_t)(base[2] * shade);
                    pixels[index + 3] = 255;
                }
            }
        }
    }

    return TextureManager::instance().createFromMemory(name, width, height, PixelType::RGBA, pixels.data(), pixels.size());
}

inline const char *TerrainDemo::terrainCaseName(int mode)
{
    switch (mode)
    {
    case TerrainHeightmap: return "TerrainNode";
    case TerrainLod: return "TerrainLodNode";
    case TerrainTiled: return "TiledTerrainNode";
    case TerrainInfinite: return "InfiniteTerrainNode";
    default: return "Unknown";
    }
}

inline const char *TerrainDemo::terrainCaseDescription(int mode)
{
    switch (mode)
    {
    case TerrainHeightmap: return "Heightmap basico dividido em blocos. Bom para validar o path base de terreno.";
    case TerrainLod: return "GeoMipMap com LOD dinamico. Aqui conseguimos validar stitching e o debug dos patches.";
    case TerrainTiled: return "Terreno flat com atlas procedural de tiles. Serve para testar o pipeline novo sem heightmap.";
    case TerrainInfinite: return "Terreno infinito com cache de patches e LOD por distancia.";
    default: return "";
    }
}

inline std::string TerrainDemo::assetPath(const char *relativePath) const
{
    return demoJoinPath(assetRoot_, relativePath);
}

inline std::string TerrainDemo::pickFirstExistingAsset(const std::vector<const char *> &relativePaths) const
{
    for (size_t i = 0; i < relativePaths.size(); ++i)
    {
        const std::string candidate = assetPath(relativePaths[i]);
        if (demoPathExists(candidate))
            return candidate;
    }

    return relativePaths.empty() ? std::string() : assetPath(relativePaths[0]);
}

inline bool TerrainDemo::setupTerrainAssets()
{
    const std::string baseTexturePath = pickFirstExistingAsset({"terrain-texture.jpg", "terrain_texture.jpg", "grass1.png"});
    const std::string detailTexturePath = pickFirstExistingAsset({"detail.jpg", "Detail_Texture.jpg", "detail2.jpg"});

    terrainBaseTexture_ = TextureManager::instance().load("terrain_demo_base", baseTexturePath);
    terrainDetailTexture_ = TextureManager::instance().load("terrain_demo_detail", detailTexturePath);
    tileAtlasTexture_ = createTileAtlasTexture("terrain_demo_tile_atlas", 4, 24);

    if (!terrainBaseTexture_)
        terrainBaseTexture_ = TextureManager::instance().getWhite();
    if (!terrainDetailTexture_)
        terrainDetailTexture_ = TextureManager::instance().getWhite();
    if (!tileAtlasTexture_)
        tileAtlasTexture_ = TextureManager::instance().getPattern();

    terrainMaterial_.name = "terrain_demo_material";
    terrainMaterial_.type = MaterialType::Custom;
    terrainMaterial_.setVec4("u_color", glm::vec4(1.0f));
    terrainMaterial_.setFloat("u_detailBlend", 0.45f);
    terrainMaterial_.setTexture("u_albedo", terrainBaseTexture_);
    terrainMaterial_.setTexture("u_detail", terrainDetailTexture_);

    tiledMaterial_.name = "terrain_demo_tiled_material";
    tiledMaterial_.type = MaterialType::Custom;
    tiledMaterial_.setVec4("u_color", glm::vec4(1.0f));
    tiledMaterial_.setFloat("u_detailBlend", 0.25f);
    tiledMaterial_.setTexture("u_albedo", tileAtlasTexture_);
    tiledMaterial_.setTexture("u_detail", terrainDetailTexture_);

    probeMaterial_.name = "terrain_demo_probe_material";
    probeMaterial_.type = MaterialType::Custom;
    probeMaterial_.setVec4("u_color", glm::vec4(1.0f, 0.25f, 0.18f, 1.0f));
    probeMaterial_.setTexture("u_albedo", TextureManager::instance().getWhite());
    return true;
}

inline bool TerrainDemo::setupScene()
{
    camera_ = scene_.createFreeCamera("terrain_camera",
                                      device_->GetWidth(), device_->GetHeight(),
                                      glm::vec3(85.0f, 38.0f, 85.0f),
                                      glm::vec3(60.0f, 0.0f, 60.0f),
                                      14.0f, 0.18f, 3.0f);
    if (!camera_)
        return false;

    camera_->clearColorVal = glm::vec4(0.70f, 0.81f, 0.92f, 1.0f);

    const std::string heightmapPath = pickFirstExistingAsset({"terrain-heightmap.png", "terrain-heightmap.bmp", "rockwall_height.bmp"});

    terrainNode_ = new TerrainNode("terrain_heightmap");
    terrainNode_->setMaterial(&terrainMaterial_);
    terrainNode_->visible = false;
    if (!terrainNode_->loadFromHeightmap(heightmapPath, 120.0f, 24.0f, 120.0f, 8.0f, 8.0f, 28.0f))
        return false;
    scene_.add(terrainNode_);

    terrainLodNode_ = new TerrainLodNode("terrain_lod", 5, TerrainPatchSize::Patch33, 24.0f);
    terrainLodNode_->setMaterial(&terrainMaterial_);
    terrainLodNode_->visible = false;
    terrainLodNode_->setTextureScale(8.0f);
    if (!terrainLodNode_->loadFromHeightmap(heightmapPath, 1.0f, 1))
        return false;
    terrainLodNode_->setScale(glm::vec3(120.0f, 24.0f, 120.0f));
    terrainLodNode_->setCameraMovementDelta(1.5f);
    terrainLodNode_->setCameraRotationDelta(0.9995f);
    scene_.add(terrainLodNode_);

    tiledTerrainNode_ = new TiledTerrainNode(4, 32.0f, 8, 0, "terrain_tiled");
    tiledTerrainNode_->setMaterial(&tiledMaterial_);
    tiledTerrainNode_->visible = false;
    std::vector<uint8_t> tileMap;
    buildTiledTerrainData(tileMap, 32, 32);
    tiledTerrainNode_->loadTilemap(32, 32, tileMap.data());
    scene_.add(tiledTerrainNode_);

    infiniteTerrainNode_ = new InfiniteTerrainNode("terrain_infinite");
    infiniteTerrainNode_->setMaterial(&terrainMaterial_);
    infiniteTerrainNode_->visible = false;
    if (!infiniteTerrainNode_->loadBaseHeightmap(heightmapPath, 18.0f))
        return false;
    infiniteTerrainNode_->configure(3, 33, 32.0f);
    scene_.add(infiniteTerrainNode_);

    Mesh *probeMesh = MeshManager::instance().create_sphere("terrain_demo_probe_mesh", 1.0f, 14);
    probeNode_ = scene_.createMeshNode("terrain_probe", probeMesh);
    if (!probeNode_)
        return false;
    probeNode_->renderType = RenderType::Solid;
    probeNode_->setMaterial(0, &probeMaterial_);
    probeNode_->setScale(glm::vec3(0.7f));

    return true;
}

inline void TerrainDemo::buildTiledTerrainData(std::vector<uint8_t> &tileMap, uint32_t width, uint32_t height) const
{
    tileMap.resize((size_t)width * height, 0);

    for (uint32_t z = 0; z < height; ++z)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            uint8_t tile = 0;
            if ((x > 6 && x < 12) && (z > 6 && z < 12))
                tile = 5;
            else if ((x / 4 + z / 4) % 2 == 0)
                tile = 1;
            else if ((x + z) % 7 == 0)
                tile = 10;
            else if (x > width / 2 && z < height / 3)
                tile = 6;
            else if (z > height / 2 && x < width / 3)
                tile = 8;
            else
                tile = (uint8_t)((x / 3 + z / 5) % 16);

            tileMap[(size_t)z * width + x] = tile;
        }
    }
}

inline void TerrainDemo::setActiveTerrain(int mode, bool resetCamera)
{
    activeTerrain_ = mode;

    if (terrainNode_)
        terrainNode_->visible = (mode == TerrainHeightmap);
    if (terrainLodNode_)
    {
        terrainLodNode_->visible = (mode == TerrainLod);
        terrainLodNode_->debugDraw = (mode == TerrainLod) && showLodDebug_;
    }
    if (tiledTerrainNode_)
        tiledTerrainNode_->visible = (mode == TerrainTiled);
    if (infiniteTerrainNode_)
        infiniteTerrainNode_->visible = (mode == TerrainInfinite);

    setProbeToCurrentTerrainCenter();
    syncProbe();

    if (resetCamera)
        resetCameraForCurrentTerrain();
}

inline void TerrainDemo::setProbeToCurrentTerrainCenter()
{
    ProbeRange range = probeRange();
    probeX_ = (range.minX + range.maxX) * 0.5f;
    probeZ_ = (range.minZ + range.maxZ) * 0.5f;
}

inline void TerrainDemo::resetCameraForCurrentTerrain()
{
    if (!camera_)
        return;

    switch (activeTerrain_)
    {
    case TerrainHeightmap:
    case TerrainLod:
        camera_->setPosition(glm::vec3(84.0f, 38.0f, 84.0f));
        camera_->lookAt(glm::vec3(60.0f, 8.0f, 60.0f));
        break;
    case TerrainTiled:
        camera_->setPosition(glm::vec3(70.0f, 42.0f, 70.0f));
        camera_->lookAt(glm::vec3(64.0f, 0.0f, 64.0f));
        break;
    case TerrainInfinite:
        camera_->setPosition(glm::vec3(22.0f, 18.0f, 22.0f));
        camera_->lookAt(glm::vec3(0.0f, 4.0f, 0.0f));
        break;
    }
    camera_->updateMatrices();
}

inline void TerrainDemo::syncProbe()
{
    ProbeRange range = probeRange();
    probeX_ = clampFloat(probeX_, range.minX, range.maxX);
    probeZ_ = clampFloat(probeZ_, range.minZ, range.maxZ);

    switch (activeTerrain_)
    {
    case TerrainHeightmap:
        sampledHeight_ = terrainNode_ ? terrainNode_->getHeightAt(probeX_, probeZ_) : 0.0f;
        sampledNormal_ = terrainNode_ ? terrainNode_->getNormalAt(probeX_, probeZ_) : glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case TerrainLod:
        sampledHeight_ = terrainLodNode_ ? terrainLodNode_->getHeightAt(probeX_, probeZ_) : 0.0f;
        sampledNormal_ = terrainLodNode_ ? terrainLodNode_->getNormalAt(probeX_, probeZ_) : glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case TerrainTiled:
        sampledHeight_ = tiledTerrainNode_ ? tiledTerrainNode_->worldPosition().y : 0.0f;
        sampledNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case TerrainInfinite:
        sampledHeight_ = infiniteTerrainNode_ ? infiniteTerrainNode_->getHeightAt(probeX_, probeZ_) : 0.0f;
        sampledNormal_ = infiniteTerrainNode_ ? infiniteTerrainNode_->getNormalAt(probeX_, probeZ_) : glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    default:
        sampledHeight_ = 0.0f;
        sampledNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    }

    if (probeNode_)
    {
        probeNode_->visible = showProbe_;
        probeNode_->setPosition(probeX_, sampledHeight_ + 1.25f, probeZ_);
    }

    if (terrainLodNode_)
        terrainLodNode_->debugDraw = showLodDebug_ && activeTerrain_ == TerrainLod;
}

inline TerrainDemo::ProbeRange TerrainDemo::probeRange() const
{
    switch (activeTerrain_)
    {
    case TerrainHeightmap:
    case TerrainLod:
        return {0.0f, 120.0f, 0.0f, 120.0f};
    case TerrainTiled:
        return {0.0f, 128.0f, 0.0f, 128.0f};
    case TerrainInfinite:
        return {-96.0f, 96.0f, -96.0f, 96.0f};
    default:
        return {0.0f, 1.0f, 0.0f, 1.0f};
    }
}

inline float TerrainDemo::clampFloat(float value, float minValue, float maxValue) const
{
    return glm::clamp(value, minValue, maxValue);
}
