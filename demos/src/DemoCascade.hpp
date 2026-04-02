#pragma once
#include "DemoBase.hpp"
#include "CascadeShadowMap.hpp"
#include "RenderState.hpp"
#include "Effects.hpp"
#include "Batch.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
//  DemoCascade — 4-cascade shadow maps
//  Shows large scene illuminated by a single directional sun
//  with hi-res close shadows fading to cheaper distant cascades.
// ============================================================
class DemoCascade : public DemoBase
{
public:
    const char *name() override { return "Cascade Shadow Maps"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 20.f, 60.f});
        camera->lookAt({0.f, 0.f, 0.f});
        camera->farPlane = 600.f;

        // ── Shaders ─────────────────────────────────────────
        auto *depthShader = shaders().load("csm_depth",
            "assets/shaders/csm_depth.vert","assets/shaders/csm_depth.frag");
        litShader_ = shaders().load("csm_lit",
            "assets/shaders/csm_lit.vert","assets/shaders/csm_lit.frag");
        skyShader_ = shaders().load("sky",
            "assets/shaders/sky.vert","assets/shaders/sky.frag");
        if (!depthShader || !litShader_ || !skyShader_) return false;

 

        glm::vec3 lightDir = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));

        // ── Scene-wide light data (uploaded via UBO, not per-material) ────────
 

        // ── Textures ─────────────────────────────────────────
        auto *texGround = textures().load("tex_ground","assets/wall.jpg");
        auto *texCube   = textures().load("tex_cube",  "assets/noise.jpg");
        auto *white     = textures().getWhite();

        // ── Materials (only per-object state: texture + toggle) ─────────────
        matGround_ = materials().create("csm_ground");
        matGround_->setShader(litShader_)
                  ->setTexture("u_albedo", texGround ? texGround : white)
                  ->setBool("u_showCascades", false);

        matCube_ = materials().create("csm_cube");
        matCube_->setShader(litShader_)
                ->setTexture("u_albedo", texCube ? texCube : white)
                ->setBool("u_showCascades", false);

        // ── Scene geometry ─────────────────────────────────────
        buildGround(matGround_);
        buildCubes(matCube_);

        // ── CSM technique ─────────────────────────────────────
        auto *tech = new CsmTechnique();
        if (!tech->initialize(2048))
        {
            delete tech;
            return false;
        }
        csm_ = tech;

        tech->litShader = litShader_;
        tech->getCsm()->setLightDirection(lightDir);
        tech->getCsm()->setShadowFarPlane(350.f);  // cobre a cena (±250 unidades)
        tech->getCsm()->setLambda(0.6f);           // distribui mais res. perto

        // Add depth passes for each cascade
        for (int i = 0; i < CSM_NUM_CASCADES; ++i)
        {
            auto *dp  = tech->addPass<CsmDepthPass>();
            dp->csm     = tech->getCsm();
            dp->cascade = i;
            dp->shader  = depthShader;
        }
        // Opaque lit pass
        auto *op = tech->addPass<OpaquePass>();
        op->shader = nullptr;  // defer to material shaders (csm_lit)

        // Transparent pass
        tech->addPass<TransparentPass>();

        // Sky rendered last — fills pixels with no geometry (depth = 1.0)
        auto *sky = tech->addPass<SkyPass>();
        sky->shader = skyShader_;

        scene.addTechnique(tech);
        debugBatch_.Init();
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt * 0.05f;
        float angle  = time_ * 0.05f;
        lightDir_ = glm::normalize(glm::vec3(std::sin(angle), -0.8f, std::cos(angle)));
        csm_->getCsm()->setLightDirection(lightDir_);

        if (Input::IsKeyPressed(KEY_C))
        {
            showCascades_ = !showCascades_;
            if (matGround_) matGround_->setBool("u_showCascades", showCascades_);
            if (matCube_)   matCube_->setBool("u_showCascades",   showCascades_);
        }
        if (Input::IsKeyPressed(KEY_F))
            showFrustums_ = !showFrustums_;
    }

    void render() override
    {
        const glm::vec4 lightDirV  = glm::vec4(-lightDir_, 0.f);
        const glm::vec4 lightColor = glm::vec4(1.f, 1.f, 0.95f, 1.f);
        const glm::vec4 ambient    = glm::vec4(0.1f, 0.12f, 0.15f, 1.f);
        const glm::vec4 camPos     = glm::vec4(camera->position, 1.f);
        auto &rs = RenderState::instance();

        rs.useProgram(litShader_->getId());
        litShader_->setVec4("u_lightDir",   lightDirV);
        litShader_->setVec4("u_lightColor", lightColor);
        litShader_->setVec4("u_ambient",    ambient);

        rs.useProgram(skyShader_->getId());
        skyShader_->setMat4("u_invViewProj", glm::inverse(camera->viewProjection));
        skyShader_->setVec4("u_cameraPos",  camPos);
        skyShader_->setVec4("u_lightDir",   lightDirV);
        skyShader_->setVec4("u_lightColor", lightColor);

        DemoBase::render();
        if (showFrustums_)
            drawCascadeDebug();
    }
    void release() override
    {
        debugBatch_.Release();
        DemoBase::release();
    }

private:
    CsmTechnique *csm_          = nullptr;
    Shader       *litShader_    = nullptr;
    Shader       *skyShader_    = nullptr;
    Material     *matGround_    = nullptr;
    Material     *matCube_      = nullptr;
    RenderBatch   debugBatch_;
    float         time_         = 0.f;
    bool          showCascades_ = false;
    bool          showFrustums_ = false;
    glm::vec3     lightDir_     = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));

    void drawCascadeDebug()
    {
        auto *csm = csm_->getCsm();

        // Cascade wireframe colours: red, green, blue, yellow
        static const u8 cols[CSM_NUM_CASCADES][3] = {
            {255, 80,  80},
            {80,  255, 80},
            {80,  80,  255},
            {255, 220, 40},
        };

        auto &rs = RenderState::instance();
        rs.setDepthTest(true);
        rs.setBlend(false);

        debugBatch_.SetMatrix(camera->viewProjection);

        float prevSplit = camera->nearPlane;
        for (int ci = 0; ci < CSM_NUM_CASCADES; ++ci)
        {
            float splitFar = csm->cascadeSplits[ci];

            // Reconstruct the 8 world-space corners of this cascade slice
            glm::mat4 sliceProj = glm::perspective(
                glm::radians(camera->fov), camera->aspect(), prevSplit, splitFar);
            glm::mat4 inv = glm::inverse(sliceProj * camera->getView());

            glm::vec3 c[8];
            int k = 0;
            for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
            {
                glm::vec4 p = inv * glm::vec4(x*2.f-1.f, y*2.f-1.f, z*2.f-1.f, 1.f);
                c[k++] = glm::vec3(p) / p.w;
            }
            // Corner layout: z-bit=0 → near, z-bit=1 → far
            // near (0,2,4,6), far (1,3,5,7)
            debugBatch_.SetColor(cols[ci][0], cols[ci][1], cols[ci][2], 255);
            // near face
            debugBatch_.Line3D(c[0],c[2]); debugBatch_.Line3D(c[2],c[6]);
            debugBatch_.Line3D(c[6],c[4]); debugBatch_.Line3D(c[4],c[0]);
            // far face
            debugBatch_.Line3D(c[1],c[3]); debugBatch_.Line3D(c[3],c[7]);
            debugBatch_.Line3D(c[7],c[5]); debugBatch_.Line3D(c[5],c[1]);
            // connecting edges
            debugBatch_.Line3D(c[0],c[1]); debugBatch_.Line3D(c[2],c[3]);
            debugBatch_.Line3D(c[4],c[5]); debugBatch_.Line3D(c[6],c[7]);

            // Also draw the light-space ortho box for this cascade
            // (inverse of the lightSpaceMatrix gives us the 8 corners of the shadow volume)
            debugBatch_.SetColor(cols[ci][0]/2, cols[ci][1]/2, cols[ci][2]/2, 180);
            glm::mat4 invLS = glm::inverse(csm->lightSpaceMatrices[ci]);
            glm::vec3 lc[8];
            k = 0;
            for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
            for (int zz = 0; zz < 2; ++zz)
            {
                glm::vec4 p = invLS * glm::vec4(x*2.f-1.f, y*2.f-1.f, zz*2.f-1.f, 1.f);
                lc[k++] = glm::vec3(p) / p.w;
            }
            debugBatch_.Line3D(lc[0],lc[2]); debugBatch_.Line3D(lc[2],lc[6]);
            debugBatch_.Line3D(lc[6],lc[4]); debugBatch_.Line3D(lc[4],lc[0]);
            debugBatch_.Line3D(lc[1],lc[3]); debugBatch_.Line3D(lc[3],lc[7]);
            debugBatch_.Line3D(lc[7],lc[5]); debugBatch_.Line3D(lc[5],lc[1]);
            debugBatch_.Line3D(lc[0],lc[1]); debugBatch_.Line3D(lc[2],lc[3]);
            debugBatch_.Line3D(lc[4],lc[5]); debugBatch_.Line3D(lc[6],lc[7]);

            prevSplit = splitFar;
        }

        debugBatch_.Render();
    }

    void buildGround(Material *mat)
    {
        auto *node = scene.createManualMeshNode("csm_ground");
        node->material = mat;
        node->begin(GL_TRIANGLES);
        float s = 250.f;
        node->normal(0,1,0).texCoord(0,0).position(-s,0,-s);
        node->normal(0,1,0).texCoord(20,0).position(s,0,-s);
        node->normal(0,1,0).texCoord(20,20).position(s,0,s);
        node->normal(0,1,0).texCoord(0,20).position(-s,0,s);
        node->triangle(2,1,0).triangle(0,3,2);
        node->end();
    }

    void buildCubes(Material *mat)
    {
        for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 6; ++col)
        {
            float x = (col - 3) * 20.f;
            float z = (row - 3) * 20.f;
            float h = 2.f + (float)((row * 7 + col * 3) % 5) * 1.5f;

            auto *node = scene.createManualMeshNode(
                "cube_" + std::to_string(row) + "_" + std::to_string(col));
            node->material = mat;
            buildBox(node, {x, h*0.5f, z}, 2.f, h, 2.f);
        }
    }

    void buildBox(ManualMeshNode *node, glm::vec3 centre, float w, float h, float d)
    {
        float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;

        node->begin(GL_TRIANGLES);
        struct Face { glm::vec3 n; glm::vec3 verts[4]; glm::vec2 uvs[4]; };
        std::vector<Face> faces = {
            {{0,0,-1},{{-hw,-hh,-hd},{hw,-hh,-hd},{hw,hh,-hd},{-hw,hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,0, 1},{{hw,-hh,hd},{-hw,-hh,hd},{-hw,hh,hd},{hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{-1,0,0},{{-hw,-hh,hd},{-hw,-hh,-hd},{-hw,hh,-hd},{-hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{1,0,0}, {{hw,-hh,-hd},{hw,-hh,hd},{hw,hh,hd},{hw,hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,1,0}, {{-hw,hh,-hd},{hw,hh,-hd},{hw,hh,hd},{-hw,hh,hd}},{{0,0},{1,0},{1,1},{0,1}}},
            {{0,-1,0},{{-hw,-hh,hd},{hw,-hh,hd},{hw,-hh,-hd},{-hw,-hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
        };
        for (const auto &f : faces)
        {
            uint32_t base = (uint32_t)node->vertexCount();
            for (int i = 0; i < 4; ++i)
                node->normal(f.n.x,f.n.y,f.n.z)
                     .texCoord(f.uvs[i].x,f.uvs[i].y)
                     .position(centre.x+f.verts[i].x, centre.y+f.verts[i].y, centre.z+f.verts[i].z);
            node->triangle(base+2,base+1,base).triangle(base,base+3,base+2);
        }
        node->end();
    }
};
