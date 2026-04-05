#include "Core.hpp"
#include "Device.hpp"
#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>

namespace
{

void drawTagAxes(RenderBatch &batch, VertexAnimatedMeshNode *node, float axisLength)
{
    if (!node || !node->mesh || node->mesh->tagsPerFrame <= 0)
        return;

    const glm::mat4 model = node->worldMatrix();
    for (int i = 0; i < node->mesh->tagsPerFrame; ++i)
    {
        glm::mat4 tagLocal;
        if (!node->mesh->sampleTag(i, node->frame, tagLocal))
            continue;

        const glm::mat4 tagWorld = model * tagLocal;
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
    if (!staticShader || !vertexAnimShader)
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

    
    VertexAnimatedMeshNode *actor = nullptr;
    VertexAnimatedMeshNode *md3Actor = nullptr;
    VertexAnimatedMeshNode *md3ActorTorso = nullptr;
    VertexAnimatedMeshNode *md3ActorHead = nullptr;

    if (md2Mesh)
    {
        actor = scene.createVertexAnimatedMeshNode("pknight", md2Mesh);
        actor->renderType = RenderType::Special;
        actor->setPosition(0.0f, 2.0f, 3.8f);
        actor->setScale(glm::vec3(0.10f));
        actor->yaw(180.0f);
        actor->fps = 6.0f;
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

        md3Actor->fps = 8.0f;
        md3Actor->playing = false;
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
        md3ActorTorso->fps = 8.0f;
        md3ActorTorso->playing = false;
        md3ActorTorso->setFrame(0.0f);

       md3ActorTorso->setParent(md3Actor->getTag(0));
     }

        if (md3MeshHead)
        {
            md3ActorHead = scene.createVertexAnimatedMeshNode("sarge_head", md3MeshHead);
            md3ActorHead->renderType = RenderType::Special;
            //md3ActorHead->setPosition(-2.5f, 1.0f, 3.8f);
            //md3ActorHead->setScale(glm::vec3(0.12f));
            //md3ActorHead->yaw(180.0f);
            md3ActorHead->fps = 8.0f;
            md3ActorHead->playing = false;
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
            actor->playing = play;
            actor->fps = fps;
            actor->setScale(glm::vec3(modelScale));
        }
  

        scene.update(dt);

        if (actor)
            frameSlider = actor->frame;
        if (md3Actor)
            md3FrameSlider = md3Actor->frame;
        if (md3ActorTorso)
            md3TorsoFrameSlider = md3ActorTorso->frame;

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

                           ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f);
                ImGui::SliderFloat("Pitch", &pitch, -180.0f, 180.0f);
                ImGui::SliderFloat("Roll", &roll, -180.0f, 180.0f);

                md3Actor->setRotationEuler(glm::vec3(pitch, yaw, roll));

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

        // scene.setShader(staticShader);
        // setupLighting(staticShader);
        // scene.render(RenderType::Transparent);
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

    device.Close();
    return 0;
}
