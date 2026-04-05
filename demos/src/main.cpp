#include "Core.hpp"
#include "Device.hpp"
#include "Animation.hpp"
#include "Animator.hpp"
#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>


extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libSDL2\n"
           "leak:SDL_DBus\n";
}


namespace
{

int findAnimationIndex(const AnimatedMesh *mesh, const std::string &name)
{
    if (!mesh)
        return -1;
    for (int i = 0; i < (int)mesh->animations.size(); ++i)
    {
        if (mesh->animations[i] && mesh->animations[i]->name == name)
            return i;
    }
    return -1;
}

void addSargeAnimations(VertexAnimatedMeshNode *lower, VertexAnimatedMeshNode *torso)
{
    if (lower)
    {
       
 
        lower->frameAnimator.addAnimation("idle", 165, 1, 15.0f, false);
        lower->frameAnimator.addAnimation("jump", 131, 140, 15.0f, false);
        
        lower->frameAnimator.addAnimation("walk", 98, 108, 6, true);
        lower->frameAnimator.addAnimation("run", 98, 108, 18, true);

 
    
 
        lower->frameAnimator.play("run");
 
    }

    if (torso)
    {
        torso->frameAnimator.addAnimation("stand", 152, 152, 15.0f, false);
        torso->frameAnimator.addAnimation("stand2", 151, 151, 15.0f, false);
        torso->frameAnimator.addAnimation("wepon", 90, 90, 18.0f, false);
        torso->frameAnimator.addAnimation("gesture", 91, 127, 18.0f, true);
        torso->frameAnimator.addAnimation("attack", 130, 135, 15.0f, false);
        torso->frameAnimator.addAnimation("attack2", 136, 141, 15.0f, false);
        torso->frameAnimator.addAnimation("drop", 142, 146, 20.0f, false);
        torso->frameAnimator.addAnimation("raise", 147, 150, 20.0f, false);
       

        torso->frameAnimator.play("stand2");
    }
}

void drawTagAxes(RenderBatch &batch, VertexAnimatedMeshNode *node, float axisLength)
{
    if (!node || !node->mesh || node->mesh->tagsPerFrame <= 0)
        return;

    for (int i = 0; i < node->mesh->tagsPerFrame; ++i)
    {
        Node3D *tag = node->getTag(i);
        if (!tag)
            continue;

        const glm::mat4 tagWorld = tag->worldMatrix();
        const glm::vec3 origin = glm::vec3(tagWorld[3]);
        const glm::vec3 axisX = glm::normalize(glm::vec3(tagWorld[0])) * axisLength;
        const glm::vec3 axisY = glm::normalize(glm::vec3(tagWorld[1])) * axisLength;
        const glm::vec3 axisZ = glm::normalize(glm::vec3(tagWorld[2])) * axisLength;

        batch.SetColor(Color::RED);
        batch.Line3D(origin, origin + axisX);
        batch.SetColor(Color::GREEN);
        batch.Line3D(origin, origin + axisY);
        batch.SetColor(Color::BLUE);
        batch.Line3D(origin, origin + axisZ);
    }
}

Material *ensureMaterial(Mesh *mesh, int slot, const char *name, const glm::vec4 &color)
{
    if (!mesh)
        return nullptr;
    if (slot >= (int)mesh->materials.size())
        mesh->materials.resize(slot + 1, nullptr);
    if (!mesh->materials[slot])
        mesh->materials[slot] = new Material();

    Material *mat = mesh->materials[slot];
    mat->name = name;
    mat->type = MaterialType::Custom;
    mat->setVec4("u_color", color);
    mat->setTexture("u_albedo", TextureManager::instance().getWhite());
    return mat;
}

Material *ensureMaterial(VertexAnimatedMesh *mesh, int slot, const char *name, const glm::vec4 &color)
{
    if (!mesh)
        return nullptr;
    if (slot >= (int)mesh->materials.size())
        mesh->materials.resize(slot + 1, nullptr);
    if (!mesh->materials[slot])
        mesh->materials[slot] = new Material();

    Material *mat = mesh->materials[slot];
    mat->name = name;
    mat->type = MaterialType::Custom;
    mat->setVec4("u_color", color);
    mat->setTexture("u_albedo", TextureManager::instance().getWhite());
    return mat;
}

Shader *createStaticShader()
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
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            FragColor = vec4(lit, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("demo_static_shader", vert, frag);
}

Shader *createVertexAnimShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in vec3 normal;

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
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            FragColor = albedo;
        });

    return ShaderManager::instance().loadFromSource("demo_vertex_anim_shader", vert, frag);
}

Shader *createSkinnedShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 3) in vec2 uv;
        layout(location = 4) in ivec4 boneIds;
        layout(location = 5) in vec4 boneWeights;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;
        uniform mat4 u_boneMatrices[100];

        out vec3 v_normal;
        out vec2 v_uv;

        void main()
        {
            mat4 skin =
                boneWeights.x * u_boneMatrices[boneIds.x] +
                boneWeights.y * u_boneMatrices[boneIds.y] +
                boneWeights.z * u_boneMatrices[boneIds.z] +
                boneWeights.w * u_boneMatrices[boneIds.w];

            vec3 skinnedPos = vec3(skin * vec4(position, 1.0));
            vec3 skinnedNormal = mat3(skin) * normal;

            v_normal = normalize(u_normalMatrix * skinnedNormal);
            v_uv = uv;
            gl_Position = u_projection * u_view * u_model * vec4(skinnedPos, 1.0);
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
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            FragColor = vec4(lit, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("demo_skinned_shader", vert, frag);
}

Shader *createLightmapShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec4 tangent;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;

        out vec2 v_uv;
        out vec2 v_lmUv;

        void main()
        {
            v_uv = uv;
            v_lmUv = tangent.xy;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec2 v_uv;
        in vec2 v_lmUv;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform sampler2D u_lightmap;
        uniform int u_useLightmap;

        void main()
        {
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lm = (u_useLightmap != 0) ? texture(u_lightmap, v_lmUv).rgb : vec3(1.0);
            FragColor = vec4(albedo.rgb * lm, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("demo_lightmap_shader", vert, frag);
}

void setupLighting(Shader *shader)
{
    if (!shader)
        return;
    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.20f, 0.22f, 0.24f));
}

} // namespace

int main()
{
    Device &device = Device::Instance();
    if (!device.Create(1280, 720, "MiniRender Scene Demo", true))
        return 1;

    device.ImGuiInit();

    Shader *staticShader = createStaticShader();
    Shader *vertexAnimShader = createVertexAnimShader();
    Shader *skinnedShader = createSkinnedShader();
    Shader *lightmapShader = createLightmapShader();
    if (!staticShader || !vertexAnimShader || !skinnedShader || !lightmapShader)
        return 1;

    Mesh *groundMesh = MeshManager::instance().create_plane("ground_demo", 14.0f, 14.0f, 8);
    Mesh *cubeMesh = MeshManager::instance().create_cube("cube_demo", 1.5f);
    Mesh *sphereMesh = MeshManager::instance().create_sphere("sphere_demo", 1.0f, 24);
    Mesh *glassMesh = MeshManager::instance().create_quad("glass_demo", 2.0f, 2.0f);

    ensureMaterial(groundMesh, 0, "ground", {0.58f, 0.60f, 0.63f, 1.0f});
    ensureMaterial(cubeMesh, 0, "cube_red", {0.85f, 0.28f, 0.22f, 1.0f});
    ensureMaterial(sphereMesh, 0, "sphere_green", {0.24f, 0.78f, 0.42f, 1.0f});
    Material *glassMat = ensureMaterial(glassMesh, 0, "glass", {0.22f, 0.62f, 0.95f, 0.35f});
    glassMat->setBlend(true);
    glassMat->setCullFace(false);

    Material overrideBlue;
    overrideBlue.name = "override_blue";
    overrideBlue.type = MaterialType::Custom;
    overrideBlue.setVec4("u_color", {0.20f, 0.46f, 0.95f, 1.0f});
    overrideBlue.setTexture("u_albedo", TextureManager::instance().getWhite());

    VertexAnimatedMesh *md2Mesh = VertexAnimatedMeshManager::instance().load(
        "pknight_demo",
        "assets/md2/pknight.md2",
        "assets/md2");
    if (md2Mesh)
    {
        Material *md2Mat = ensureMaterial(md2Mesh, 0, "pknight_tint", {1.0f, 1.0f, 1.0f, 1.0f});
        if (Texture *md2Tex = TextureManager::instance().load("pknight_manual_tex", "assets/md2/pknight.jpg"))
            md2Mat->setTexture("u_albedo", md2Tex);
    }

    VertexAnimatedMesh *md3Mesh = VertexAnimatedMeshManager::instance().load(
        "sarge_lower",
        "assets/md3/sarge/lower.md3",
        "assets/md3/sarge/lower_default.skin");

    VertexAnimatedMesh *md3MeshTorso = VertexAnimatedMeshManager::instance().load(
        "sarge_upper",
        "assets/md3/sarge/upper.md3",
        "assets/md3/sarge/upper_default.skin");

    VertexAnimatedMesh *md3MeshHead = VertexAnimatedMeshManager::instance().load(
        "sarge_head",
        "assets/md3/sarge/head.md3",
        "assets/md3/sarge/head_default.skin");

    AnimatedMesh *b3dAnimatedMesh = AnimatedMeshManager::instance().load(
        "ninja_b3d_anim",
        "assets/b3d/ninja.b3d",
        "assets/b3d");

    AnimatedMesh *iqmAnimatedMesh = AnimatedMeshManager::instance().load(
        "erebus_iqm_anim",
        "assets/iqm/erebus/erebus.iqm",
        "assets/iqm/erebus");

    if (iqmAnimatedMesh)
    {
        Texture *erebusTex = TextureManager::instance().load(
            "erebus_iqm_manual_base",
            "assets/iqm/erebus/erebus.png");
        Texture *shadowTex = TextureManager::instance().load(
            "erebus_iqm_manual_shadow",
            "assets/iqm/erebus/shadowhead.png");

        
             iqmAnimatedMesh->materials[0]->setTexture("u_albedo", shadowTex);
             iqmAnimatedMesh->materials[1]->setTexture("u_albedo", erebusTex);
    }

    // Mesh *b3dStaticMesh = MeshManager::instance().load(
    //     "ninja_b3d_static",
    //     "assets/b3d/ninja.b3d",
    //     "assets/b3d");

    Mesh *gltfMesh = MeshManager::instance().load(
        "idle_glb_static",
        "assets/gltf/idle.glb",
        "assets/gltf");

    AnimatedMesh *gltfAnimatedMesh = AnimatedMeshManager::instance().load(
        "idle_glb_anim",
        "assets/gltf/idle.glb",
        "assets/gltf");

    Mesh *bspMesh = MeshManager::instance().load(
        "oa_rpg3dm2_bsp",
        "assets/maps/oa_rpg3dm/maps/oa_rpg3dm2.bsp",
        "assets/maps/oa_rpg3dm");


    Scene scene;
    Camera *camera = scene.createFreeCamera("main",
                                            device.GetWidth(), device.GetHeight(),
                                            glm::vec3(0.0f, 3.0f, 9.0f),
                                            glm::vec3(0.0f, 1.0f, 0.0f),
                                            8.0f, 0.18f, 3.0f);
    camera->clearColorVal = glm::vec4(0.73f, 0.82f, 0.93f, 1.0f);
    scene.setCamera(camera);

    auto *ground = scene.createMeshNode("ground", groundMesh);
    ground->renderType = RenderType::Solid;
    ground->setPosition(0.0f, -1.0f, 0.0f);

    auto *cubeA = scene.createMeshNode("cubeA", cubeMesh);
    cubeA->renderType = RenderType::Solid;
    cubeA->setPosition(-2.2f, 0.0f, 0.0f);

    auto *cubeB = scene.createMeshNode("cubeB", cubeMesh);
    cubeB->renderType = RenderType::Solid;
    //cubeB->setPosition(2.0f, 0.3f, -1.5f);
    cubeB->setMaterial(0, &overrideBlue);

    auto *sphere = scene.createMeshNode("sphere", sphereMesh);
    sphere->renderType = RenderType::Solid;
    sphere->setPosition(0.0f, 0.0f, -3.5f);

    auto *glass = scene.createMeshNode("glass", glassMesh);
    glass->renderType = RenderType::Transparent;
    glass->setPosition(0.0f, 0.2f, 1.8f);
    glass->yaw(20.0f);

    MeshNode *gltfNode = nullptr;
    MeshNode *bspNode = nullptr;
    AnimatedMeshNode *gltfAnimatedNode = nullptr;
    // if (b3dStaticMesh)
    // {
    //     b3dStaticNode = scene.createMeshNode("ninja_b3d_static", b3dStaticMesh);
    //     b3dStaticNode->renderType = RenderType::Solid;
    //     b3dStaticNode->setPosition(2.8f, 0.0f, 2.0f);
    //     b3dStaticNode->setScale(glm::vec3(0.05f));
    //     b3dStaticNode->yaw(180.0f);
    // }

    if (gltfMesh)
    {
        gltfNode = scene.createMeshNode("idle_glb_static", gltfMesh);
        gltfNode->renderType = RenderType::Solid;
        gltfNode->setPosition(5.5f, 0.0f, 1.5f);
        gltfNode->setScale(glm::vec3(1.0f));
        gltfNode->yaw(180.0f);
    }

    if (gltfAnimatedMesh)
    {
        gltfAnimatedNode = scene.createAnimatedMeshNode("idle_glb_anim", gltfAnimatedMesh);
        gltfAnimatedNode->renderType = RenderType::Skinning;
        gltfAnimatedNode->setPosition(8.0f, 0.0f, 1.5f);
        gltfAnimatedNode->setScale(glm::vec3(0.01f));
        //gltfAnimatedNode->yaw(180.0f);
    }

    // if (bspMesh)
    // {
    //     bspNode = scene.createMeshNode("oa_rpg3dm2_bsp", bspMesh);
    //     bspNode->renderType = RenderType::Lightmap;
    //     bspNode->setPosition(0.0f, -1.0f, 0.0f);
    //     bspNode->setScale(glm::vec3(0.03f));
    // }

    
    VertexAnimatedMeshNode *actor = nullptr;
    VertexAnimatedMeshNode *md3Actor = nullptr;
    VertexAnimatedMeshNode *md3ActorTorso = nullptr;
    VertexAnimatedMeshNode *md3ActorHead = nullptr;
    AnimatedMeshNode *b3dAnimatedNode = nullptr;
    AnimatedMeshNode *iqmAnimatedNode = nullptr;

    if (md2Mesh)
    {
        actor = scene.createVertexAnimatedMeshNode("pknight", md2Mesh);
        actor->renderType = RenderType::Special;
        actor->setPosition(0.0f, 2.0f, 3.8f);
        actor->setScale(glm::vec3(0.10f));
        actor->yaw(180.0f);
 
        actor->setFrame(0.0f);
        actor->visible = true;
    }

    float yaw = 0.0f;
    float pitch = -90.0f;
    float roll = 0.0f;


    if (md3Mesh)
    {
        md3Actor = scene.createVertexAnimatedMeshNode("sarge_lower", md3Mesh);
        md3Actor->renderType = RenderType::Special;
        md3Actor->setPosition(-2.5f, 2.0f, 3.8f);
        md3Actor->setScale(glm::vec3(0.12f));
        md3Actor->yaw(yaw);
        md3Actor->pitch(pitch);
        md3Actor->roll(roll);

 
        md3Actor->setFrame(100.0f);

        cubeB->setParent(md3Actor->getTag(0));
    }

    if (md3MeshTorso)
     {
        md3ActorTorso = scene.createVertexAnimatedMeshNode("sarge_torso", md3MeshTorso);
        md3ActorTorso->renderType = RenderType::Special;
        //md3ActorTorso->setPosition(-2.5f, 1.0f, 3.8f);
        //md3ActorTorso->setScale(glm::vec3(1.0f));
        //md3ActorTorso->yaw(180.0f);
 
        md3ActorTorso->setFrame(0.0f);

       md3ActorTorso->setParent(md3Actor->getTag(0));
     }

    addSargeAnimations(md3Actor, md3ActorTorso);

    if (b3dAnimatedMesh)
    {
        b3dAnimatedNode = scene.createAnimatedMeshNode("ninja_b3d_anim", b3dAnimatedMesh);
        b3dAnimatedNode->renderType = RenderType::Skinning;
        b3dAnimatedNode->setPosition(2.8f, 0.0f, -1.0f);
        b3dAnimatedNode->setScale(glm::vec3(0.05f));
        b3dAnimatedNode->yaw(180.0f);
    }

    if (iqmAnimatedMesh)
    {
        iqmAnimatedNode = scene.createAnimatedMeshNode("erebus_iqm_anim", iqmAnimatedMesh);
        iqmAnimatedNode->renderType = RenderType::Skinning;
        iqmAnimatedNode->setPosition(-5.0f, 0.0f, -1.0f);
        iqmAnimatedNode->setScale(glm::vec3(0.04f));
        iqmAnimatedNode->yaw(180.0f);
    }

        if (md3MeshHead)
        {
            md3ActorHead = scene.createVertexAnimatedMeshNode("sarge_head", md3MeshHead);
            md3ActorHead->renderType = RenderType::Special;
            //md3ActorHead->setPosition(-2.5f, 1.0f, 3.8f);
            //md3ActorHead->setScale(glm::vec3(0.12f));
            //md3ActorHead->yaw(180.0f);
 
            md3ActorHead->setFrame(0.0f);

            md3ActorHead->setParent(md3ActorTorso->getTag(0));
        }

    float frameSlider = 0.0f;
    float modelScale = 0.10f;
    bool play = true;
    float fps = 6.0f;
    float md3FrameSlider = 0.0f;
    float md3TorsoFrameSlider = 0.0f;
    bool md3Play = true;
    float md3Fps = 8.0f;
    bool showMd3Tags = true;
    float tagAxisSize = 1.0f;

    RenderBatch tagBatch;
    tagBatch.Init();

    while (device.Run())
    {
        if (device.IsResize())
            camera->setViewport(0, 0, device.GetWidth(), device.GetHeight());

        const float dt = device.GetFrameTime();

       // cubeA->yaw(20.0f * dt);
        //cubeB->pitch(35.0f * dt);
        sphere->yaw(-15.0f * dt);

        if (actor)
        {
 
            actor->setScale(glm::vec3(modelScale));
        }
  

        scene.update(dt);

        if (actor)
            frameSlider = actor->currentFrame();
        if (md3Actor)
            md3FrameSlider = md3Actor->currentFrame();
        if (md3ActorTorso)
            md3TorsoFrameSlider = md3ActorTorso->currentFrame();

        device.ImGuiBegin();
        ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Once);
        if (ImGui::Begin("Vertex Animation"))
        {
            if (actor && md2Mesh)
            {
                ImGui::SeparatorText("MD2");
                ImGui::Text("Mesh: %s", md2Mesh->name.c_str());
                ImGui::Text("Frames: %d", md2Mesh->frameCount());
                ImGui::Checkbox("Play", &play);
                ImGui::SliderFloat("FPS", &fps, 1.0f, 20.0f);
                ImGui::SliderFloat("Scale", &modelScale, 0.02f, 0.25f);
     




                float maxFrame = (float)glm::max(0, md2Mesh->frameCount() - 1);
                if (ImGui::SliderFloat("Frame", &frameSlider, 0.0f, maxFrame))
                {
                    play = false;
                    actor->setFrame(frameSlider);
                }

                const int frameIndex = glm::clamp((int)frameSlider, 0, glm::max(0, md2Mesh->frameCount() - 1));
                if (frameIndex < (int)md2Mesh->frameNames.size())
                    ImGui::Text("Frame name: %s", md2Mesh->frameNames[frameIndex].c_str());
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load bin/assets/md2/pknight.md2");
            }

            if (md3Actor && md3Mesh)
            {
                ImGui::SeparatorText("MD3");
                ImGui::Text("Mesh: %s", md3Mesh->name.c_str());
                ImGui::Text("Frames: %d", md3Mesh->frameCount());
                ImGui::Text("Tags/frame: %d", md3Mesh->tagsPerFrame);
                ImGui::Checkbox("Play MD3", &md3Play);
                ImGui::Checkbox("Show Tags", &showMd3Tags);

                ImGui::SliderFloat("MD3 FPS", &md3Fps, 1.0f, 20.0f);
                //ImGui::SliderFloat("MD3 Scale", &md3Scale, 0.02f, 0.25f);
                ImGui::SliderFloat("Tag Axis", &tagAxisSize, 1.0f, 20.0f);

                float maxFrame = (float)glm::max(0, md3Mesh->frameCount() - 1);
                if (ImGui::SliderFloat("MD3 Frame", &md3FrameSlider, 0.0f, maxFrame))
                {
                    md3Play = false;
                    md3Actor->setFrame(md3FrameSlider);
                
             
                }

                const int frameIndex = glm::clamp((int)md3FrameSlider, 0, glm::max(0, md3Mesh->frameCount() - 1));
                if (frameIndex < (int)md3Mesh->frameNames.size())
                    ImGui::Text("Frame name: %s", md3Mesh->frameNames[frameIndex].c_str());

                for (int i = 0; i < md3Mesh->tagsPerFrame; ++i)
                    ImGui::Text("Tag %d: %s", i, md3Mesh->tags[i].tag);
            
       
            
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/md3/sarge/lower.md3");
            }

            if (md3ActorTorso && md3MeshTorso)
            {
                ImGui::SeparatorText("MD3 Torso");
                ImGui::Text("Mesh: %s", md3MeshTorso->name.c_str());
                ImGui::Text("Frames: %d", md3MeshTorso->frameCount());
                ImGui::Text("Tags/frame: %d", md3MeshTorso->tagsPerFrame);
                float maxFrame = (float)glm::max(0, md3MeshTorso->frameCount() - 1);
                if (ImGui::SliderFloat("MD3 Torso Frame", &md3TorsoFrameSlider, 0.0f, maxFrame))
                {
                  
                    md3ActorTorso->setFrame(md3TorsoFrameSlider);
                  
                }

                const int frameIndex = glm::clamp((int)md3TorsoFrameSlider, 0, glm::max(0, md3MeshTorso->frameCount() - 1));
                if (frameIndex < (int)md3MeshTorso->frameNames.size())
                    ImGui::Text("Frame name: %s", md3MeshTorso->frameNames[frameIndex].c_str());
            }

            if (b3dAnimatedNode && b3dAnimatedMesh)
            {
                ImGui::SeparatorText("B3D Animated");
                ImGui::Text("Mesh: %s", b3dAnimatedMesh->name.c_str());
                ImGui::Text("Bones: %d", (int)b3dAnimatedMesh->bones.size());
                ImGui::Text("Animations: %d", (int)b3dAnimatedMesh->animations.size());
            }
            else
            {
                ImGui::SeparatorText("B3D Animated");
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load animated assets/b3d/ninja.b3d");
            }

            if (gltfNode && gltfMesh)
            {
                ImGui::SeparatorText("GLTF / GLB");
                ImGui::Text("Mesh: %s", gltfMesh->name.c_str());
                ImGui::Text("Vertices: %d", gltfMesh->vertexCount());
                ImGui::Text("Indices: %d", gltfMesh->indexCount());
                ImGui::Text("Surfaces: %d", (int)gltfMesh->surfaces.size());
                ImGui::Text("Materials: %d", (int)gltfMesh->materials.size());
            }
            else
            {
                ImGui::SeparatorText("GLTF / GLB");
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/gltf/idle.glb");
            }

            if (gltfAnimatedNode && gltfAnimatedMesh)
            {
                ImGui::SeparatorText("GLTF Animated");
                ImGui::Text("Mesh: %s", gltfAnimatedMesh->name.c_str());
                ImGui::Text("Bones: %d", (int)gltfAnimatedMesh->bones.size());
                ImGui::Text("Animations: %d", (int)gltfAnimatedMesh->animations.size());
                ImGui::Text("Surfaces: %d", (int)gltfAnimatedMesh->surfaces.size());
                if (gltfAnimatedNode->animator && gltfAnimatedNode->animator->layerCount() > 0)
                {
                    AnimationLayer *layer = gltfAnimatedNode->animator->getLayer(0);
                    const std::string currentAnim = layer ? layer->currentName() : std::string();
                    ImGui::Text("Current: %s", currentAnim.empty() ? "<none>" : currentAnim.c_str());
                }
            }
            else
            {
                ImGui::SeparatorText("GLTF Animated");
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load animated assets/gltf/idle.glb");
            }

            if (iqmAnimatedNode && iqmAnimatedMesh)
            {
                ImGui::SeparatorText("IQM Animated");
                ImGui::Text("Mesh: %s", iqmAnimatedMesh->name.c_str());
                ImGui::Text("Bones: %d", (int)iqmAnimatedMesh->bones.size());
                ImGui::Text("Animations: %d", (int)iqmAnimatedMesh->animations.size());
                ImGui::Text("Surfaces: %d", (int)iqmAnimatedMesh->surfaces.size());
                if (iqmAnimatedNode->animator && iqmAnimatedNode->animator->layerCount() > 0)
                {
                    AnimationLayer *layer = iqmAnimatedNode->animator->getLayer(0);
                    const std::string currentAnim = layer ? layer->currentName() : std::string();
                    ImGui::Text("Current: %s", currentAnim.empty() ? "<none>" : currentAnim.c_str());
                    if (ImGui::Button("Next IQM Animation") && layer && !iqmAnimatedMesh->animations.empty())
                    {
                        int nextIndex = findAnimationIndex(iqmAnimatedMesh, currentAnim);
                        nextIndex = (nextIndex + 1) % (int)iqmAnimatedMesh->animations.size();
                        Animation *nextAnim = iqmAnimatedMesh->animations[nextIndex];
                        if (nextAnim)
                            layer->play(nextAnim->name);
                    }
                }


                ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f);
                ImGui::SliderFloat("Pitch", &pitch, -180.0f, 180.0f);
                ImGui::SliderFloat("Roll", &roll, -180.0f, 180.0f);

                iqmAnimatedNode->setRotationEuler(glm::vec3(pitch, yaw, roll));

            }
            else
            {
                ImGui::SeparatorText("IQM Animated");
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/iqm/erebus/erebus.iqm");
            }

            if (bspNode && bspMesh)
            {
                ImGui::SeparatorText("BSP");
                ImGui::Text("Mesh: %s", bspMesh->name.c_str());
                ImGui::Text("Vertices: %d", bspMesh->vertexCount());
                ImGui::Text("Indices: %d", bspMesh->indexCount());
                ImGui::Text("Surfaces: %d", (int)bspMesh->surfaces.size());
                ImGui::Text("Materials: %d", (int)bspMesh->materials.size());
            }
            else
            {
                ImGui::SeparatorText("BSP");
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/maps/oa_rpg3dm/maps/oa_rpg3dm2.bsp");
            }
        }
        ImGui::End();
        device.ImGuiEnd();
        
        scene.setCamera(camera);
        
        scene.beginPass();
        
        scene.setShader(staticShader);
        setupLighting(staticShader);
        scene.render(RenderType::Solid);
        
        scene.setShader(vertexAnimShader);
        setupLighting(vertexAnimShader);
        scene.render(RenderType::Special);

        scene.setShader(skinnedShader);
        setupLighting(skinnedShader);
        scene.render(RenderType::Skinning);

        scene.setShader(lightmapShader);
        scene.render(RenderType::Lightmap);

        scene.setShader(staticShader);
        setupLighting(staticShader);
        scene.render(RenderType::Transparent);

        scene.endPass();

        if (md3Actor && md3Mesh && showMd3Tags)
        {
            tagBatch.SetMatrix(camera->viewProjection);
            Material::applyDefaultStates();
            drawTagAxes(tagBatch, md3Actor, tagAxisSize);
            drawTagAxes(tagBatch, md3ActorTorso, tagAxisSize);
            tagBatch.Render();
        }
        device.Flip();

    }

    scene.clear();
    AnimatedMeshManager::instance().unloadAll();
    VertexAnimatedMeshManager::instance().unloadAll();
    MeshManager::instance().unloadAll();
    TextureManager::instance().unloadAll();
    ShaderManager::instance().unloadAll();
    device.Close();
    return 0;
}
