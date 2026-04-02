#pragma once

#include "DemoBase.hpp"
#include "Input.hpp"
#include "Manager.hpp"
#include "MeshLoader.hpp"
#include "RenderPipeline.hpp"
#include "VertexAnimation.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

class DemoMD2 : public DemoBase
{
public:
    const char *name() override { return "MD2 Vertex Anim"; }

    bool init() override
    {
        DemoBase::init();

        camera->setPosition({0.f, 18.f, 46.f});
        camera->lookAt({0.f, 16.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 30.f;

        shader_ = shaders().load("unlit",
                                 "assets/shaders/unlit.vert",
                                 "assets/shaders/unlit.frag");
        if (!shader_)
            return false;

        materials().setDefaults(shader_, textures().getPattern());

        if (!loadMd2("assets/md2/pknight.md2", "file:assets/md2/pknight.jpg"))
            return false;

        tech_ = new ForwardTechnique();
        // Don't set pass shader — let each material's own shader drive rendering
        // (pass shader override skips per-material texture binding in some paths)
        scene.addTechnique(tech_);

        node_ = scene.createVertexAnimMeshNode("md2_knight", mesh_);
        node_->setScale({1.4f, 1.4f, 1.4f});
        node_->controller = pendingController_;

        // Center the mesh around origin and place feet near ground level.
        const glm::vec3 c = mesh_->aabb.center();
        node_->setPosition({-c.x, -mesh_->aabb.min.y, -c.z});
        {
            const glm::vec3 sz = mesh_->aabb.size() * node_->scale;
            const float focusY = std::max(8.f, sz.y * 0.55f);
            const float rawDist = std::max(24.f, std::max(sz.x, sz.z) * 2.6f);
            const float dist = std::min(rawDist, 90.f);
            camera->setPosition({0.f, focusY + 6.f, dist});
            camera->lookAt({0.f, focusY, 0.f});
        }

        clipNames_ = {"Stand", "Run", "Attack", "Jump"};
        playClip(clipNames_[0], 0.f);

        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);

        if (!node_ || !mesh_) return;

        // One-shot diagnostic dump
        if (!diagDone_)
        {
            diagDone_ = true;
            SDL_Log("[DemoMD2] === DIAGNOSTIC ===");
            SDL_Log("[DemoMD2] mesh surfaces: %zu", mesh_->surfaces.size());
            for (size_t i = 0; i < mesh_->surfaces.size(); ++i)
            {
                const auto &s = mesh_->surfaces[i];
                SDL_Log("[DemoMD2]   surf[%zu] start=%u count=%u matIdx=%d",
                        i, s.index_start, s.index_count, s.material_index);
            }
            SDL_Log("[DemoMD2] mesh materials: %zu", mesh_->materials.size());
            for (size_t i = 0; i < mesh_->materials.size(); ++i)
            {
                Material *m = mesh_->materials[i];
                SDL_Log("[DemoMD2]   mat[%zu] name='%s' shader=%p texCount=%zu",
                        i, m ? m->name.c_str() : "(null)",
                        m ? (void*)m->getShader() : nullptr,
                        m ? m->getTextures().size() : 0u);
                if (m)
                {
                    for (const auto &slot : m->getTextures())
                        SDL_Log("[DemoMD2]     tex uniform='%s' id=%u target=0x%X",
                                slot.uniform.c_str(),
                                slot.texture ? slot.texture->id : 0,
                                slot.texture ? slot.texture->target : 0);
                }
            }
            SDL_Log("[DemoMD2] node getMaterial=%p passMask=0x%X",
                    (void*)node_->getMaterial(), node_->passMask);
            SDL_Log("[DemoMD2] shader_=%p id=%u", (void*)shader_, shader_ ? shader_->getId() : 0);
            SDL_Log("[DemoMD2] === END DIAGNOSTIC ===");
        }

        if (Input::IsKeyPressed(KEY_ONE)) playClip("Stand");
        if (Input::IsKeyPressed(KEY_TWO)) playClip("Run");
        if (Input::IsKeyPressed(KEY_THREE)) playClip("Attack");
        if (Input::IsKeyPressed(KEY_FOUR)) playClip("Jump");

        // Scene::update already advanced node_->controller this frame.
        updateMeshFromSample(node_->controller.sample());
    }

    void render() override { DemoBase::render(); }

    void release() override
    {
        tech_ = nullptr;
        shader_ = nullptr;
        mesh_ = nullptr;
        node_ = nullptr;
        framePositions_.clear();
        cornerToBaseVert_.clear();
        DemoBase::release();
    }

private:
    struct Md2Header
    {
        int ident = 0;
        int version = 0;
        int skinWidth = 0;
        int skinHeight = 0;
        int frameSize = 0;
        int numSkins = 0;
        int numVerts = 0;
        int numUV = 0;
        int numTris = 0;
        int numGLCmds = 0;
        int numFrames = 0;
        int ofsSkins = 0;
        int ofsUV = 0;
        int ofsTris = 0;
        int ofsFrames = 0;
        int ofsGLCmds = 0;
        int ofsEnd = 0;
    };

    struct Md2Tri
    {
        uint16_t vi[3] = {};
        uint16_t ti[3] = {};
    };

    struct Md2UV
    {
        int16_t u = 0;
        int16_t v = 0;
    };

    static constexpr int kMd2Ident = 844121161; // IDP2
    static constexpr int kMd2Version = 8;

    Mesh *mesh_ = nullptr;
    VertexAnimMeshNode *node_ = nullptr;
    ForwardTechnique *tech_ = nullptr;
    Shader *shader_ = nullptr;
    bool diagDone_ = false;

    std::vector<glm::vec3> framePositions_; // numFrames * numVerts
    std::vector<uint32_t> cornerToBaseVert_;
    int numFrames_ = 0;
    int numVerts_ = 0;

    std::array<std::string, 4> clipNames_;

    static uint16_t readU16(BinaryStream &s)
    {
        uint16_t v = 0;
        s.readRaw(&v, sizeof(v));
        return SDL_SwapLE16(v);
    }

    static int16_t readI16(BinaryStream &s)
    {
        return static_cast<int16_t>(readU16(s));
    }

    static std::string stripFilePrefix(const std::string &p)
    {
        if (p.rfind("file:", 0) == 0)
            return p.substr(5);
        return p;
    }

    Texture *loadTextureManual(const std::string &pathWithPrefix)
    {
        const std::string rel = stripFilePrefix(pathWithPrefix);
        std::array<std::string, 3> tries = {
            rel,
            std::string("../") + rel,
            std::string("../../") + rel,
        };

        for (const auto &p : tries)
        {
            SDL_Surface *src = IMG_Load(p.c_str());
            if (!src) continue;

            SDL_Surface *rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(src);
            if (!rgba) continue;

            const std::size_t bytes = static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h) * 4u;
            Texture *tex = textures().createFromMemory("md2_pknight_diffuse_manual",
                                                       rgba->w, rgba->h,
                                                       PixelType::RGBA32,
                                                       rgba->pixels, bytes);
            SDL_FreeSurface(rgba);

            if (tex)
            {
                SDL_Log("[DemoMD2] manual texture load ok: %s", p.c_str());
                return tex;
            }
        }

        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[DemoMD2] manual texture load failed: %s",
                    pathWithPrefix.c_str());
        return nullptr;
    }

    bool loadMd2(const std::string &modelPath, const std::string &texturePath)
    {
        BinaryStream s(modelPath, "rb");
        if (!s.isOpen())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoMD2] Failed to open %s", modelPath.c_str());
            return false;
        }

        Md2Header h;
        h.ident = s.readI32();
        h.version = s.readI32();
        h.skinWidth = s.readI32();
        h.skinHeight = s.readI32();
        h.frameSize = s.readI32();
        h.numSkins = s.readI32();
        h.numVerts = s.readI32();
        h.numUV = s.readI32();
        h.numTris = s.readI32();
        h.numGLCmds = s.readI32();
        h.numFrames = s.readI32();
        h.ofsSkins = s.readI32();
        h.ofsUV = s.readI32();
        h.ofsTris = s.readI32();
        h.ofsFrames = s.readI32();
        h.ofsGLCmds = s.readI32();
        h.ofsEnd = s.readI32();

        if (h.ident != kMd2Ident || h.version != kMd2Version ||
            h.numFrames <= 0 || h.numVerts <= 0 || h.numTris <= 0 || h.numUV <= 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DemoMD2] Invalid MD2 header: %s", modelPath.c_str());
            return false;
        }

        std::vector<Md2UV> uvs(h.numUV);
        s.seek(h.ofsUV);
        for (int i = 0; i < h.numUV; ++i)
        {
            uvs[i].u = readI16(s);
            uvs[i].v = readI16(s);
        }

        std::vector<Md2Tri> tris(h.numTris);
        s.seek(h.ofsTris);
        for (int i = 0; i < h.numTris; ++i)
        {
            for (int k = 0; k < 3; ++k) tris[i].vi[k] = readU16(s);
            for (int k = 0; k < 3; ++k) tris[i].ti[k] = readU16(s);
        }

        numFrames_ = h.numFrames;
        numVerts_ = h.numVerts;
        framePositions_.assign(static_cast<size_t>(h.numFrames) * static_cast<size_t>(h.numVerts), glm::vec3(0.f));

        s.seek(h.ofsFrames);
        for (int f = 0; f < h.numFrames; ++f)
        {
            glm::vec3 scale;
            glm::vec3 translate;
            scale.x = s.readF32(); scale.y = s.readF32(); scale.z = s.readF32();
            translate.x = s.readF32(); translate.y = s.readF32(); translate.z = s.readF32();

            char frameName[16] = {};
            s.readRaw(frameName, 16);

            for (int v = 0; v < h.numVerts; ++v)
            {
                const uint8_t x = s.readU8();
                const uint8_t y = s.readU8();
                const uint8_t z = s.readU8();
                (void)s.readU8(); // normal index

                const glm::vec3 p = glm::vec3((float)x, (float)y, (float)z) * scale + translate;
                framePositions_[static_cast<size_t>(f) * static_cast<size_t>(h.numVerts) + static_cast<size_t>(v)] = p;
            }
        }

        mesh_ = meshes().create("md2_pknight");
        mesh_->buffer.dynamic = true;
        mesh_->buffer.vertices.clear();
        mesh_->buffer.indices.clear();
        cornerToBaseVert_.clear();

        mesh_->buffer.vertices.reserve(static_cast<size_t>(h.numTris) * 3u);
        mesh_->buffer.indices.reserve(static_cast<size_t>(h.numTris) * 3u);
        cornerToBaseVert_.reserve(static_cast<size_t>(h.numTris) * 3u);

        for (int t = 0; t < h.numTris; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                const uint16_t baseVi = tris[t].vi[k];
                const uint16_t uvI = tris[t].ti[k];
                if (baseVi >= h.numVerts || uvI >= h.numUV)
                    continue;

                const glm::vec3 p = framePositions_[baseVi];
                const glm::vec2 uv = glm::vec2((float)uvs[uvI].u / (float)h.skinWidth,
                                               (float)uvs[uvI].v / (float)h.skinHeight);

                Vertex vx;
                vx.position = p;
                vx.normal = glm::vec3(0.f, 1.f, 0.f);
                vx.tangent = glm::vec4(1.f, 0.f, 0.f, 1.f);
                vx.uv = uv;

                mesh_->buffer.vertices.push_back(vx);
                mesh_->buffer.indices.push_back(static_cast<uint32_t>(mesh_->buffer.vertices.size() - 1));
                cornerToBaseVert_.push_back(baseVi);
            }
        }

        glm::vec2 uvMin( 1e30f);
        glm::vec2 uvMax(-1e30f);
        for (const auto &v : mesh_->buffer.vertices)
        {
            uvMin = glm::min(uvMin, v.uv);
            uvMax = glm::max(uvMax, v.uv);
        }
        const glm::vec2 uvSpan = uvMax - uvMin;
        SDL_Log("[DemoMD2] UV range min(%.4f, %.4f) max(%.4f, %.4f) span(%.4f, %.4f)",
                uvMin.x, uvMin.y, uvMax.x, uvMax.y, uvSpan.x, uvSpan.y);

        if (uvSpan.x < 1e-5f && uvSpan.y < 1e-5f)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[DemoMD2] Collapsed UVs detected, generating planar fallback UVs");

            BoundingBox bb;
            for (const auto &v : mesh_->buffer.vertices)
                bb.expand(v.position);

            const glm::vec3 span = bb.size();
            const float sx = std::max(span.x, 1e-5f);
            const float sz = std::max(span.z, 1e-5f);
            for (auto &v : mesh_->buffer.vertices)
            {
                const float u = (v.position.x - bb.min.x) / sx;
                const float vv = (v.position.z - bb.min.z) / sz;
                v.uv = {u, vv};
            }
        }

        mesh_->buffer.upload();
        mesh_->surfaces.clear();
        mesh_->add_surface(0, (uint32_t)mesh_->buffer.indices.size(), 0);

        Texture *tex = loadTextureManual(texturePath);

        if (!tex)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[DemoMD2] texture not found '%s', using fallback pattern",
                        texturePath.c_str());
            tex = textures().getPattern();
        }

        Material *mat = materials().create("md2_pknight_mat");
        mat->setShader(shader_);
        mat->setCullFace(false);
        mat->setTexture("u_albedo", tex ? tex : textures().getPattern());
        mat->setVec3("u_color", glm::vec3(1.f, 1.f, 1.f));

        if (tex)
        {
            SDL_Log("[DemoMD2] texture '%s' id=%u size=%dx%d",
                    tex->name.c_str(), tex->id, tex->width, tex->height);
        }

        mesh_->materials.clear();
        mesh_->materials.push_back(mat);
        materials().applyDefaults();

        computeGlobalAabb();

        VertexAnimController ctrl;
        addDefaultMd2Clips(ctrl, h.numFrames);
        if (node_)
            node_->controller = ctrl;
        else
            pendingController_ = ctrl;

        SDL_Log("[DemoMD2] Loaded %s frames=%d verts=%d tris=%d", modelPath.c_str(), h.numFrames, h.numVerts, h.numTris);
        SDL_Log("[DemoMD2] AABB min(%.2f %.2f %.2f) max(%.2f %.2f %.2f)",
            mesh_->aabb.min.x, mesh_->aabb.min.y, mesh_->aabb.min.z,
            mesh_->aabb.max.x, mesh_->aabb.max.y, mesh_->aabb.max.z);
        return true;
    }

    void computeGlobalAabb()
    {
        BoundingBox bb;
        for (const glm::vec3 &p : framePositions_)
            bb.expand(p);

        mesh_->aabb = bb;
        if (!mesh_->surfaces.empty())
            mesh_->surfaces[0].aabb = bb;
    }

    glm::vec3 framePos(int frame, uint32_t baseVertex) const
    {
        if (numVerts_ <= 0 || numFrames_ <= 0)
            return glm::vec3(0.f);

        frame = std::max(0, std::min(frame, numFrames_ - 1));
        const size_t idx = static_cast<size_t>(frame) * static_cast<size_t>(numVerts_) + static_cast<size_t>(baseVertex);
        if (idx >= framePositions_.size())
            return glm::vec3(0.f);
        return framePositions_[idx];
    }

    void updateMeshFromSample(const VertexAnimSample &s)
    {
        auto &verts = mesh_->buffer.vertices;
        auto &idx = mesh_->buffer.indices;
        if (verts.empty() || idx.empty()) return;

        for (size_t i = 0; i < verts.size(); ++i)
        {
            const uint32_t baseV = cornerToBaseVert_[i];
            glm::vec3 cur = glm::mix(framePos(s.currentFrame, baseV),
                                     framePos(s.nextFrame, baseV),
                                     glm::clamp(s.currentInterp, 0.f, 1.f));

            if (s.hasPrevious && s.clipBlend > 0.f)
            {
                glm::vec3 prev = glm::mix(framePos(s.previousFrame, baseV),
                                          framePos(s.previousNextFrame, baseV),
                                          glm::clamp(s.previousInterp, 0.f, 1.f));
                cur = glm::mix(cur, prev, glm::clamp(s.clipBlend, 0.f, 1.f));
            }

            verts[i].position = cur;
            verts[i].normal = glm::vec3(0.f);
        }

        for (size_t i = 0; i + 2 < idx.size(); i += 3)
        {
            Vertex &a = verts[idx[i + 0]];
            Vertex &b = verts[idx[i + 1]];
            Vertex &c = verts[idx[i + 2]];

            const glm::vec3 e1 = b.position - a.position;
            const glm::vec3 e2 = c.position - a.position;
            glm::vec3 n = glm::cross(e1, e2);
            const float len2 = glm::dot(n, n);
            if (len2 > 1e-10f)
                n = glm::normalize(n);
            else
                n = glm::vec3(0.f, 1.f, 0.f);

            a.normal += n;
            b.normal += n;
            c.normal += n;
        }

        for (Vertex &v : verts)
        {
            const float len2 = glm::dot(v.normal, v.normal);
            if (len2 > 1e-10f)
                v.normal = glm::normalize(v.normal);
            else
                v.normal = glm::vec3(0.f, 1.f, 0.f);
        }

        mesh_->buffer.update();
    }

    void playClip(const std::string &name, float blendTime = 0.14f)
    {
        if (!node_)
        {
            if (!pendingController_.play(name, blendTime, false))
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[DemoMD2] clip '%s' not found", name.c_str());
            return;
        }

        if (!node_->controller.play(name, blendTime, false))
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[DemoMD2] clip '%s' not found", name.c_str());
    }

    static void addDefaultMd2Clips(VertexAnimController &controller, int frameCount)
    {
        struct Def
        {
            const char *name;
            int first;
            int last;
            float fps;
            bool loop;
        };

        static const Def defs[] = {
            {"Stand", 0, 39, 9.f, true},
            {"Run", 40, 45, 10.f, true},
            {"Attack", 46, 53, 10.f, true},
            {"Jump", 66, 71, 7.f, true},
        };

        for (const Def &d : defs)
        {
            int first = std::max(0, std::min(d.first, frameCount - 1));
            int last = std::max(first, std::min(d.last, frameCount - 1));
            controller.addClip(VertexAnimClip{d.name, first, last, d.fps, d.loop});
        }
    }

    VertexAnimController pendingController_;
};
