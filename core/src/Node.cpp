#include "Node.hpp"
#include "Animator.hpp"
#include "Md2Loader.hpp"
#include "Md3Loader.hpp"
#include "Manager.hpp"
#include <algorithm>
#include <cctype>

namespace
{
Mesh *cloneMeshInstance(const Mesh *src, const std::string &nameHint)
{
    if (!src)
        return nullptr;

    Mesh *dst = new Mesh();
    dst->name = nameHint.empty() ? (src->name + "_instance") : nameHint;

    dst->buffer.dynamic = src->buffer.dynamic;
    dst->buffer.mode = src->buffer.mode;
    dst->buffer.vertices = src->buffer.vertices;
    dst->buffer.indices = src->buffer.indices;
    dst->surfaces = src->surfaces;
    dst->materials = src->materials;
    dst->aabb = src->aabb;

    dst->buffer.upload();
    return dst;
}

std::string trimStr(const std::string &x)
{
    size_t a = 0;
    while (a < x.size() && std::isspace(static_cast<unsigned char>(x[a])))
        ++a;
    size_t b = x.size();
    while (b > a && std::isspace(static_cast<unsigned char>(x[b - 1])))
        --b;
    return x.substr(a, b - a);
}

std::string toLowerStr(std::string x)
{
    std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return x;
}

bool startsWithStr(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
}


unsigned long globalNodeID = 1; // start at 1 so 0 can be "invalid ID"

Node::Node()
{
     ID = globalNodeID++;
}

Node::~Node()
{
    for (Node *c : children)
    {
        c->parent = nullptr;
        delete c;
    }
    children.clear();
}

void Node::addChild(Node *child)
{
    if (!child || child->parent == this)
        return;
    if (child->parent)
        child->parent->removeChild(child);
    child->parent = this;
    children.push_back(child);
}

void Node::removeChild(Node *child)
{
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
    {
        (*it)->parent = nullptr;
        children.erase(it);
    }
}

Node *Node::getChild(const std::string &name) const
{
    for (Node *c : children)
        if (c->name == name)
            return c;
    return nullptr;
}

void MeshNode::setMaterial(const std::string &name)
{
    materialName_ = name;
    material_     = MaterialManager::instance().get(name); // resolved once
}

MeshNode::MeshNode():Node3D(), mesh(nullptr), castShadow(true), receiveShadow(true), passMask(RenderPassMask::Opaque)
{
    type = NodeType::MeshNode;
}


Light::Light():Node3D(), lightType(LightType::Point), color(1.f, 1.f, 1.f), intensity(1.0f), castShadow(false)
{
    type = NodeType::Light;
}

// ─── AnimatedMeshNode ────────────────────────────────────────────────────────

AnimatedMeshNode::AnimatedMeshNode() : Node3D()
{
    type = NodeType::MeshNode; // reuse MeshNode type
}

AnimatedMeshNode::~AnimatedMeshNode()
{
    delete animator;
}

VertexAnimMeshNode::VertexAnimMeshNode() : MeshNode()
{
    type = NodeType::MeshNode;
}

VertexAnimMeshNode::~VertexAnimMeshNode()
{
    clearTagSockets();
    if (ownedMesh_)
    {
        if (mesh == ownedMesh_)
            mesh = nullptr;
        delete ownedMesh_;
        ownedMesh_ = nullptr;
    }
}

void AnimatedMeshNode::setMaterial(const std::string &name)
{
    materialName_ = name;
    material_     = MaterialManager::instance().get(name);
}

// ── BoneSocket ───────────────────────────────────────────────

BoneSocket *AnimatedMeshNode::addSocket(const std::string &boneName, Node3D *node,
                                        const glm::mat4 &localOffset)
{
    // Avoid duplicates on same bone
    for (auto &s : sockets_)
        if (s.boneName == boneName) { s.node = node; s.localOffset = localOffset; return &s; }

    BoneSocket s;
    s.boneName    = boneName;
    s.boneIndex   = animator ? animator->findBoneIndex(boneName) : -1;
    s.node        = node;
    s.localOffset = localOffset;
    sockets_.push_back(s);

    // Add as child so it participates in scene traversal / rendering
    addChild(node);
    return &sockets_.back();
}

BoneSocket *AnimatedMeshNode::getSocket(const std::string &boneName)
{
    for (auto &s : sockets_)
        if (s.boneName == boneName) return &s;
    return nullptr;
}

void AnimatedMeshNode::removeSocket(const std::string &boneName)
{
    for (auto it = sockets_.begin(); it != sockets_.end(); ++it)
    {
        if (it->boneName == boneName)
        {
            removeChild(it->node);
            sockets_.erase(it);
            return;
        }
    }
}

void AnimatedMeshNode::updateSockets()
{
    if (sockets_.empty() || !animator) return;

    for (auto &s : sockets_)
    {
        if (!s.node) continue;

        // Resolve bone index lazily
        if (s.boneIndex < 0)
            s.boneIndex = animator->findBoneIndex(s.boneName);
        if (s.boneIndex < 0) continue;

        // boneMat = mesh-local transform of the bone (no inverse bind pose applied)
        // The socket node is a child of this AnimatedMeshNode, so its local transform
        // should be relative to this node's space — which is exactly boneMat * localOffset.
        glm::mat4 boneMat = animator->getBoneTransform(s.boneIndex) * s.localOffset;

        // Decompose into TRS for the Node3D
        glm::vec3 scl, pos, skew;
        glm::vec4 persp;
        glm::quat rot;
        if (glm::decompose(boneMat, scl, rot, pos, skew, persp))
        {
            s.node->position = pos;
            s.node->rotation = glm::conjugate(rot); // glm::decompose returns conjugate
            s.node->scale    = scl;
        }
    }
}

VertexTagSocket *VertexAnimMeshNode::addTagSocket(const std::string &tagName, Node3D *node,
                                                   const glm::mat4 &localOffset)
{
    if (!node) return nullptr;

    for (auto &s : tagSockets_)
    {
        if (s.tagName == tagName)
        {
            if (s.node != node)
            {
                if (s.node) removeChild(s.node);
                addChild(node);
            }
            s.node = node;
            s.localOffset = localOffset;
            tagBinder_.addSocket(tagName, node, localOffset);
            return &s;
        }
    }

    VertexTagSocket s;
    s.tagName = tagName;
    s.node = node;
    s.localOffset = localOffset;
    tagSockets_.push_back(s);

    addChild(node);
    tagBinder_.addSocket(tagName, node, localOffset);
    return &tagSockets_.back();
}

VertexTagSocket *VertexAnimMeshNode::getTagSocket(const std::string &tagName)
{
    for (auto &s : tagSockets_)
        if (s.tagName == tagName) return &s;
    return nullptr;
}

void VertexAnimMeshNode::removeTagSocket(const std::string &tagName)
{
    for (auto it = tagSockets_.begin(); it != tagSockets_.end(); ++it)
    {
        if (it->tagName == tagName)
        {
            if (it->node) removeChild(it->node);
            tagBinder_.removeSocket(tagName);
            tagSockets_.erase(it);
            return;
        }
    }
}

void VertexAnimMeshNode::clearTagSockets()
{
    for (auto &s : tagSockets_)
        if (s.node) removeChild(s.node);
    tagSockets_.clear();
    tagBinder_.clear();
}

void VertexAnimMeshNode::updateTagSockets()
{
    tagBinder_.updateSockets(controller, tagTracks_);
}

void VertexAnimMeshNode::setAnimAsset(const VertexAnimAsset *asset, bool cloneTemplateMesh)
{
    if (ownedMesh_)
    {
        if (mesh == ownedMesh_)
            mesh = nullptr;
        delete ownedMesh_;
        ownedMesh_ = nullptr;
    }

    animAsset_ = asset;
    controller = VertexAnimController();
    tagTracks_.clear();

    if (!animAsset_)
        return;

    tagTracks_ = animAsset_->tags;
    for (const VertexAnimClip &clip : animAsset_->clips)
        controller.addClip(clip);

    if (cloneTemplateMesh && animAsset_->templateMesh)
    {
        const std::string clonedName = name.empty()
                                           ? (animAsset_->templateMesh->name + "_inst")
                                           : (name + "_mesh");
        ownedMesh_ = cloneMeshInstance(animAsset_->templateMesh, clonedName);
        mesh = ownedMesh_;
    }
    else
    {
        mesh = animAsset_->templateMesh;
    }

    if (!animAsset_->clips.empty())
        controller.play(animAsset_->clips.front().name, 0.0f, true);

    if (autoApplySample_ && animAsset_->valid() && mesh)
        animAsset_->apply(mesh, controller.sample());

    updateTagSockets();
}

void VertexAnimMeshNode::updateAnimation(float dt)
{
    controller.update(dt);

    if (autoApplySample_ && animAsset_ && animAsset_->valid() && mesh)
        animAsset_->apply(mesh, controller.sample());

    updateTagSockets();
}

VertexAnimMeshNode *VertexAnimMeshNode::cloneInstance(const std::string &newName) const
{
    auto *node = new VertexAnimMeshNode();
    node->name = newName.empty() ? name : newName;
    node->visible = visible;
    node->position = position;
    node->rotation = rotation;
    node->scale = scale;
    node->castShadow = castShadow;
    node->receiveShadow = receiveShadow;
    node->passMask = passMask;
    node->setAutoApplySample(autoApplySample_);

    if (!getMaterialName().empty())
        node->setMaterial(getMaterialName());

    if (animAsset_)
    {
        node->setAnimAsset(animAsset_, true);
        if (const VertexAnimClip *cur = controller.currentClip())
            node->controller.play(cur->name, 0.0f, true);
    }
    else
    {
        node->setTagTracks(tagTracks_);
        if (mesh)
        {
            node->ownedMesh_ = cloneMeshInstance(mesh, node->name + "_mesh");
            node->mesh = node->ownedMesh_;
        }
    }

    node->markDirty();
    return node;
}

Md2Node::Md2Node() : VertexAnimMeshNode()
{
    type = NodeType::Md2Node;
}

bool Md2Node::setMd2Data(Mesh *templateMesh,
                         const Md2RuntimeData &runtime,
                         bool cloneTemplateMesh,
                         const std::string &defaultClip)
{
    if (!templateMesh || !runtime.valid())
        return false;

    md2Runtime_ = runtime;
    md2Asset_ = VertexAnimAsset{};
    md2Asset_.templateMesh = templateMesh;
    md2Asset_.tags.clear();

    struct Def
    {
        const char *name;
        int first;
        int last;
        float fps;
        bool loop;
    };

    static const Def defs[] = {
        {"Stand", 0, 39, 9.0f, true},
        {"Run", 40, 45, 10.0f, true},
        {"Attack", 46, 53, 10.0f, false},
        {"PainA", 54, 57, 7.0f, false},
        {"PainB", 58, 61, 7.0f, false},
        {"PainC", 62, 65, 7.0f, false},
        {"Jump", 66, 71, 7.0f, false},
        {"Flip", 72, 83, 7.0f, false},
        {"Salute", 84, 94, 7.0f, false},
        {"Fallback", 95, 111, 10.0f, false},
        {"Wave", 112, 122, 7.0f, false},
        {"Point", 123, 134, 6.0f, false},
        {"CrouchStand", 135, 153, 10.0f, true},
        {"CrouchWalk", 154, 159, 7.0f, true},
        {"CrouchAttack", 160, 168, 10.0f, false},
        {"CrouchPain", 169, 172, 7.0f, false},
        {"CrouchDeath", 173, 177, 5.0f, false},
        {"DeathFallback", 178, 183, 7.0f, false},
        {"DeathFallForward", 184, 189, 7.0f, false},
        {"DeathFallbackSlow", 190, 197, 7.0f, false},
        {"Boom", 198, 198, 5.0f, false},
    };

    for (const Def &d : defs)
    {
        const int first = std::clamp(d.first, 0, md2Runtime_.numFrames - 1);
        const int last = std::clamp(d.last, first, md2Runtime_.numFrames - 1);
        md2Asset_.clips.emplace_back(d.name, first, last, d.fps, d.loop);
    }

    md2Asset_.applySample = &Md2Node::applyMd2Sample;
    md2Asset_.applyUserData = &md2Runtime_;

    return setMd2Asset(&md2Asset_, cloneTemplateMesh, defaultClip);
}

bool Md2Node::setMd2Asset(const VertexAnimAsset *asset,
                          bool cloneTemplateMesh,
                          const std::string &defaultClip)
{
    if (!asset || !asset->valid())
        return false;

    setAnimAsset(asset, cloneTemplateMesh);

    if (!defaultClip.empty())
        controller.play(defaultClip, 0.0f, true);

    if (autoApplySample() && animAsset() && animAsset()->valid() && mesh)
        animAsset()->apply(mesh, controller.sample());
    updateTagSockets();
    return true;
}

bool Md2Node::playMd2(const std::string &clipName, float blendTime)
{
    return controller.play(clipName, blendTime, false);
}

bool Md2Node::loadMd2(const std::string &modelPath,
                      const std::string &texturePath,
                      Shader *shader,
                      bool cloneTemplateMesh,
                      const std::string &defaultClip)
{
    Md2Loader loader;
    Md2Loader::Options opt;
    opt.meshName = name.empty() ? "md2_mesh" : (name + "_template");
    opt.materialName = name.empty() ? "md2_material" : (name + "_mat");
    opt.texturePath = texturePath;
    opt.shader = shader;
    opt.disableBackfaceCulling = true;

    Md2RuntimeData runtime;
    Mesh *templateMesh = loader.load(modelPath, opt, &runtime);
    if (!templateMesh)
        return false;

    return setMd2Data(templateMesh, runtime, cloneTemplateMesh, defaultClip);
}

void Md2Node::applyMd2Sample(Mesh *mesh,
                             const VertexAnimSample &sample,
                             const void *userData)
{
    const Md2RuntimeData *runtime = static_cast<const Md2RuntimeData *>(userData);
    if (!runtime)
        return;

    const Md2RuntimeData &rt = *runtime;

    if (!mesh || !rt.valid())
        return;

    auto &verts = mesh->buffer.vertices;
    auto &indices = mesh->buffer.indices;
    if (verts.empty() || indices.empty() ||
        rt.cornerToBaseVertex.size() < verts.size())
    {
        return;
    }

    const float tCur = std::clamp(sample.currentInterp, 0.0f, 1.0f);
    const float tPrev = std::clamp(sample.previousInterp, 0.0f, 1.0f);
    const float tBlend = std::clamp(sample.clipBlend, 0.0f, 1.0f);

    for (size_t i = 0; i < verts.size(); ++i)
    {
        const uint32_t baseV = rt.cornerToBaseVertex[i];

        glm::vec3 cur = glm::mix(rt.framePos(sample.currentFrame, baseV),
                                 rt.framePos(sample.nextFrame, baseV),
                                 tCur);

        if (sample.hasPrevious && tBlend > 0.0f)
        {
            const glm::vec3 prev = glm::mix(rt.framePos(sample.previousFrame, baseV),
                                            rt.framePos(sample.previousNextFrame, baseV),
                                            tPrev);
            cur = glm::mix(cur, prev, tBlend);
        }

        verts[i].position = cur;
        verts[i].normal = glm::vec3(0.0f);
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        Vertex &a = verts[indices[i + 0]];
        Vertex &b = verts[indices[i + 1]];
        Vertex &c = verts[indices[i + 2]];

        const glm::vec3 e1 = b.position - a.position;
        const glm::vec3 e2 = c.position - a.position;
        glm::vec3 n = glm::cross(e1, e2);
        const float len2 = glm::dot(n, n);
        if (len2 > 1e-10f)
            n = glm::normalize(n);
        else
            n = glm::vec3(0.0f, 1.0f, 0.0f);

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
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    mesh->buffer.update();
}

Md3Node::Md3Node() : VertexAnimMeshNode()
{
    type = NodeType::Md3Node;
}

bool Md3Node::setMd3Data(Mesh *templateMesh,
                         const Md3PartRuntime &runtime,
                         const std::vector<VertexAnimClip> &clips,
                         bool cloneTemplateMesh,
                         const std::string &defaultClip)
{
    if (!templateMesh || !runtime.valid())
        return false;

    md3Runtime_ = runtime;
    md3Asset_ = VertexAnimAsset{};
    md3Asset_.templateMesh = templateMesh;
    md3Asset_.tags = md3Runtime_.tagTracks;
    md3Asset_.clips = clips;
    if (md3Asset_.clips.empty())
        md3Asset_.clips.emplace_back("all", 0, std::max(0, md3Runtime_.frameCount - 1), 10.0f, true);

    md3Asset_.applySample = &Md3Node::applyMd3Sample;
    md3Asset_.applyUserData = &md3Runtime_;

    return setMd3Asset(&md3Asset_, cloneTemplateMesh, defaultClip);
}

bool Md3Node::setMd3Asset(const VertexAnimAsset *asset,
                          bool cloneTemplateMesh,
                          const std::string &defaultClip)
{
    if (!asset || !asset->valid())
        return false;

    setAnimAsset(asset, cloneTemplateMesh);

    if (!defaultClip.empty())
        controller.play(defaultClip, 0.0f, true);

    if (autoApplySample() && animAsset() && animAsset()->valid() && mesh)
        animAsset()->apply(mesh, controller.sample());
    updateTagSockets();
    return true;
}

bool Md3Node::playMd3(const std::string &clipName, float blendTime)
{
    return controller.play(clipName, blendTime, false);
}

bool Md3Node::loadMd3Part(const std::string &modelPath,
                          const std::string &skinPath,
                          const std::string &partId,
                          const std::string &animationCfgPath,
                          const std::string &clipPrefix,
                          bool includeBoth,
                          Shader *shader,
                          bool cloneTemplateMesh,
                          const std::string &defaultClip)
{
    Md3Loader loader;
    Md3Loader::Options opt;
    opt.partId = partId.empty() ? name : partId;
    opt.meshName = name.empty() ? "md3_mesh" : (name + "_template");
    opt.materialPrefix = "md3_mat";
    opt.skinPath = skinPath;
    opt.shader = shader;
    opt.disableBackfaceCulling = true;

    Md3PartRuntime runtime;
    Mesh *templateMesh = loader.load(modelPath, opt, &runtime, nullptr);
    if (!templateMesh)
        return false;

    std::vector<VertexAnimClip> clips;
    if (!animationCfgPath.empty() && runtime.valid())
    {
        std::vector<Md3AnimCfgClip> cfg;
        if (Md3Loader::parseAnimationCfg(animationCfgPath, cfg))
        {
            const std::string pref = toLowerStr(trimStr(clipPrefix));
            for (const Md3AnimCfgClip &c : cfg)
            {
                const std::string nm = toLowerStr(trimStr(c.name));
                bool take = pref.empty();
                if (!take)
                    take = startsWithStr(nm, pref);
                if (!take && includeBoth && startsWithStr(nm, "both_"))
                    take = true;
                if (!take)
                    continue;

                const int first = std::max(0, std::min(c.first, runtime.frameCount - 1));
                const int last = std::max(first, std::min(c.last, runtime.frameCount - 1));
                const float fps = std::max(c.fps, 0.01f);
                clips.emplace_back(c.name, first, last, fps, c.loop);
            }
        }
    }

    return setMd3Data(templateMesh, runtime, clips, cloneTemplateMesh, defaultClip);
}

void Md3Node::applyMd3Sample(Mesh *mesh,
                             const VertexAnimSample &sample,
                             const void *userData)
{
    const Md3PartRuntime *runtime = static_cast<const Md3PartRuntime *>(userData);
    if (!runtime)
        return;

    const Md3PartRuntime &rt = *runtime;
    if (!mesh || !rt.valid())
        return;

    auto &verts = mesh->buffer.vertices;
    if (verts.empty())
        return;

    const float tCur = std::clamp(sample.currentInterp, 0.0f, 1.0f);
    const float tPrev = std::clamp(sample.previousInterp, 0.0f, 1.0f);
    const float tBlend = std::clamp(sample.clipBlend, 0.0f, 1.0f);

    for (const Md3SurfaceRuntime &surf : rt.surfaces)
    {
        if (!surf.ready())
            continue;

        for (int v = 0; v < surf.numVerts; ++v)
        {
            const int c0 = std::clamp(sample.currentFrame, 0, surf.numFrames - 1);
            const int c1 = std::clamp(sample.nextFrame, 0, surf.numFrames - 1);
            const int p0 = std::clamp(sample.previousFrame, 0, surf.numFrames - 1);
            const int p1 = std::clamp(sample.previousNextFrame, 0, surf.numFrames - 1);

            const size_t i0 = static_cast<size_t>(c0) * static_cast<size_t>(surf.numVerts) + static_cast<size_t>(v);
            const size_t i1 = static_cast<size_t>(c1) * static_cast<size_t>(surf.numVerts) + static_cast<size_t>(v);
            const size_t j0 = static_cast<size_t>(p0) * static_cast<size_t>(surf.numVerts) + static_cast<size_t>(v);
            const size_t j1 = static_cast<size_t>(p1) * static_cast<size_t>(surf.numVerts) + static_cast<size_t>(v);

            if (i0 >= surf.framePositions.size() || i1 >= surf.framePositions.size() ||
                i0 >= surf.frameNormals.size() || i1 >= surf.frameNormals.size())
            {
                continue;
            }

            glm::vec3 pos = glm::mix(surf.framePositions[i0], surf.framePositions[i1], tCur);
            glm::vec3 nrm = glm::mix(surf.frameNormals[i0], surf.frameNormals[i1], tCur);

            if (sample.hasPrevious && tBlend > 0.0f &&
                j0 < surf.framePositions.size() && j1 < surf.framePositions.size() &&
                j0 < surf.frameNormals.size() && j1 < surf.frameNormals.size())
            {
                const glm::vec3 prevPos = glm::mix(surf.framePositions[j0], surf.framePositions[j1], tPrev);
                const glm::vec3 prevNrm = glm::mix(surf.frameNormals[j0], surf.frameNormals[j1], tPrev);
                pos = glm::mix(pos, prevPos, tBlend);
                nrm = glm::mix(nrm, prevNrm, tBlend);
            }

            const float len2 = glm::dot(nrm, nrm);
            if (len2 > 1e-10f)
                nrm = glm::normalize(nrm);
            else
                nrm = glm::vec3(0.0f, 1.0f, 0.0f);

            const uint32_t dst = surf.vertexStart + static_cast<uint32_t>(v);
            if (dst < verts.size())
            {
                verts[dst].position = pos;
                verts[dst].normal = nrm;
            }
        }
    }

    mesh->buffer.update();
}

VertexTagSocket *Md3Node::attachTagNode(const std::string &tagName,
                                        Node3D *child,
                                        const glm::mat4 &localOffset)
{
    return addTagSocket(tagName, child, localOffset);
}
