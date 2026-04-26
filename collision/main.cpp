#include "CollisionSystem.hpp"
#include "CollisionWorld.hpp"

#include "Core.hpp"
#include "Device.hpp"
#include "Input.hpp"
#include "imgui.h"

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace cs = collision_study;

namespace
{
Shader *createLitShader()
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

    return ShaderManager::instance().loadFromSource("collision_visual_shader", vert, frag);
}

void setupLighting(Shader *shader)
{
    if (!shader)
        return;

    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(0.45f, -1.0f, 0.2f)));
    shader->setVec3("u_ambient", glm::vec3(0.28f, 0.30f, 0.34f));
}

struct SceneCollisionParams
{
    glm::vec3 wallCenter = glm::vec3(0.0f, 1.5f, -4.0f);
    glm::vec2 wallHalfSize = glm::vec2(2.5f, 1.5f);
    float wallHalfThickness = 0.09f;

    glm::vec3 sphereObstacleCenter = glm::vec3(1.5f, 1.0f, -1.5f);
    float sphereObstacleRadius = 0.8f;

    glm::vec3 boxObstacleCenter = glm::vec3(-4.5f, 1.0f, -0.9f);
    glm::vec3 boxObstacleHalfExtents = glm::vec3(0.7f, 1.0f, 0.6f);

    glm::vec3 capsuleCenter = glm::vec3(3.8f, 1.25f, -1.0f);
    float capsuleRadius = 0.45f;
    float capsuleHeight = 2.2f;
};

void appendMeshNodeTriangles(cs::CollisionSystem &mesh,
                             const MeshNode *node,
                             bool flipWinding)
{
    if (!node || !node->mesh)
        return;

    const auto &verts = node->mesh->buffer.vertices;
    const auto &idx = node->mesh->buffer.indices;
    if (verts.size() < 3)
        return;

    const glm::mat4 world = node->worldMatrix();
    const auto xform = [&](const glm::vec3 &p) -> glm::vec3
    {
        return glm::vec3(world * glm::vec4(p, 1.0f));
    };

    const auto addTri = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c)
    {
        if (!flipWinding)
            mesh.addTriangle({a, b, c});
        else
            mesh.addTriangle({a, c, b});
    };

    if (!idx.empty())
    {
        const size_t triIndexCount = idx.size() - (idx.size() % 3);
        for (size_t i = 0; i < triIndexCount; i += 3)
        {
            const uint32_t i0 = idx[i + 0];
            const uint32_t i1 = idx[i + 1];
            const uint32_t i2 = idx[i + 2];
            if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
                continue;

            addTri(xform(verts[i0].position),
                   xform(verts[i1].position),
                   xform(verts[i2].position));
        }
        return;
    }

    const size_t triVertexCount = verts.size() - (verts.size() % 3);
    for (size_t i = 0; i < triVertexCount; i += 3)
    {
        addTri(xform(verts[i + 0].position),
               xform(verts[i + 1].position),
               xform(verts[i + 2].position));
    }
}

void rebuildWorld(cs::CollisionWorld &world,
                  cs::CollisionSystem &mesh,
                  const SceneCollisionParams &params,
                  const MeshNode *castleNode,
                  const MeshNode *boxNode)
{
    const bool flipWinding = false;
    mesh.clear();
    appendMeshNodeTriangles(mesh, castleNode, flipWinding);

    world.clearTargets();

    cs::CollisionTarget poly;
    poly.dstType = 1;
    poly.method = cs::COLLISION_METHOD_POLYGON;
    poly.response = cs::COLLISION_RESPONSE_SLIDE;
    poly.mesh = &mesh;
    world.addTarget(poly);

    cs::CollisionTarget sphere;
    sphere.dstType = 1;
    sphere.method = cs::COLLISION_METHOD_SPHERE;
    sphere.response = cs::COLLISION_RESPONSE_SLIDE;
    sphere.sphereCenter = params.sphereObstacleCenter;
    sphere.sphereRadius = params.sphereObstacleRadius;
    world.addTarget(sphere);

    if (boxNode && boxNode->mesh)
    {
        BoundingBox boxWorld;
        const BoundingBox &local = boxNode->mesh->aabb;
        const glm::mat4 worldM = boxNode->worldMatrix();

        const glm::vec3 corners[8] = {
            {local.min.x, local.min.y, local.min.z},
            {local.max.x, local.min.y, local.min.z},
            {local.min.x, local.max.y, local.min.z},
            {local.max.x, local.max.y, local.min.z},
            {local.min.x, local.min.y, local.max.z},
            {local.max.x, local.min.y, local.max.z},
            {local.min.x, local.max.y, local.max.z},
            {local.max.x, local.max.y, local.max.z},
        };

        for (const glm::vec3 &c : corners)
            boxWorld.expand(glm::vec3(worldM * glm::vec4(c, 1.0f)));

        cs::CollisionTarget box;
        box.dstType = 1;
        box.method = cs::COLLISION_METHOD_BOX;
        box.response = cs::COLLISION_RESPONSE_SLIDE;
        box.box = boxWorld;
        world.addTarget(box);
    }

    cs::CollisionTarget capsule;
    capsule.dstType = 1;
    capsule.method = cs::COLLISION_METHOD_CAPSULE;
    capsule.response = cs::COLLISION_RESPONSE_SLIDE;
    const float halfSegment = glm::max(params.capsuleHeight * 0.5f - params.capsuleRadius, 0.0f);
    capsule.capsuleA = params.capsuleCenter + glm::vec3(0.0f, -halfSegment, 0.0f);
    capsule.capsuleB = params.capsuleCenter + glm::vec3(0.0f, halfSegment, 0.0f);
    capsule.capsuleRadius = params.capsuleRadius;
    world.addTarget(capsule);
}

void syncObstacleTransforms(const SceneCollisionParams &params,
                            MeshNode *wallNode,
                            MeshNode *obstacleSphereNode,
                            MeshNode *obstacleBoxNode)
{
    if (wallNode)
    {
        const float zThickness = Max(params.wallHalfThickness * 2.0f, 0.001f);
        wallNode->setPosition(params.wallCenter);
        wallNode->setScale(glm::vec3(params.wallHalfSize.x * 2.0f, params.wallHalfSize.y * 2.0f, zThickness));
    }

    if (obstacleSphereNode)
    {
        obstacleSphereNode->setPosition(params.sphereObstacleCenter);
        obstacleSphereNode->setScale(glm::vec3(params.sphereObstacleRadius));
    }

    if (obstacleBoxNode)
    {
        obstacleBoxNode->setPosition(params.boxObstacleCenter);
        obstacleBoxNode->setScale(params.boxObstacleHalfExtents * 2.0f);
    }
}

float axis(bool positive, bool negative)
{
    return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
}

void drawCollisionDebug(RenderBatch &batch,
                        const cs::CollisionSystem &mesh,
                        const SceneCollisionParams &params,
                        const glm::vec3 &moveFrom,
                        const glm::vec3 &moveTo,
                        const glm::vec3 &resolvedTo,
                        const glm::vec3 &facingDir,
                        float playerRadius,
                        float playerHeight,
                        bool playerCapsule,
                        bool collisionActive,
                        bool drawCollisionAabbs,
                        bool drawNormals,
                        float normalSize)
{
    batch.SetColor(230, 230, 30, 255);
    for (const Triangle &tri : mesh.triangles())
        batch.TriangleLines(tri.v0, tri.v1, tri.v2);

    if (drawNormals)
    {
        batch.SetColor(30, 200, 255, 255);
        for (const Triangle &tri : mesh.triangles())
        {
            const glm::vec3 center = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
            const glm::vec3 n = tri.normal();
            batch.Line3D(center, center + n * normalSize);
        }
    }

    batch.SetColor(255, 120, 30, 255);
    batch.Sphere(params.sphereObstacleCenter, params.sphereObstacleRadius, 12, 12, true);

    batch.SetColor(255, 90, 200, 255);
    batch.Cube(params.boxObstacleCenter,
               params.boxObstacleHalfExtents.x * 2.0f,
               params.boxObstacleHalfExtents.y * 2.0f,
               params.boxObstacleHalfExtents.z * 2.0f,
               true);

    batch.SetColor(80, 255, 180, 255);
    batch.Capsule(params.capsuleCenter, params.capsuleRadius, params.capsuleHeight, 16, true);

    batch.SetColor(40, 210, 255, 255);
    batch.Line3D(moveFrom, moveTo);
    batch.SetColor(40, 255, 120, 255);
    batch.Line3D(moveFrom, resolvedTo);
    batch.CircleXZ(resolvedTo, 0.10f, 16);

    batch.SetColor(255, 255, 255, 255);
    batch.Line3D(resolvedTo + glm::vec3(0.0f, playerHeight * 0.45f, 0.0f),
                 resolvedTo + glm::vec3(0.0f, playerHeight * 0.45f, 0.0f) + facingDir * 1.8f);

    if (playerCapsule)
    {
        batch.SetColor(255, 40, 40, 255);
        batch.Capsule(resolvedTo, playerRadius, playerHeight, 16, true);
    }

    if (drawCollisionAabbs && collisionActive)
    {
        BoundingBox obstacleBox;
        obstacleBox.min = params.boxObstacleCenter - params.boxObstacleHalfExtents;
        obstacleBox.max = params.boxObstacleCenter + params.boxObstacleHalfExtents;

        BoundingBox playerBox;
        playerBox.min = resolvedTo - glm::vec3(playerRadius);
        playerBox.max = resolvedTo + glm::vec3(playerRadius);

        batch.SetColor(255, 40, 40, 255);
        batch.Box(playerBox);
        batch.SetColor(255, 30, 255, 255);
        batch.Box(obstacleBox);
    }
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    Device &device = Device::Instance();
    if (!device.Create(1280, 720, "Collision Visual Lab", true,1))
        return 1;

    device.ImGuiInit();

    Shader *shader = createLitShader();
    if (!shader)
    {
        device.Close();
        return 2;
    }

    Scene scene;
    Camera *camera = scene.createCamera("collision_camera");

    if (!camera)
    {
        device.Close();
        return 3;
    }

    camera->setViewport(0, 0, device.GetWidth(), device.GetHeight());
    camera->setPosition(glm::vec3(0.0f, 16.4f, 14.0f));
    camera->setEulerAngles(glm::vec3(-8.0f, 0.0f, 0.0f));
    camera->clearColorVal = glm::vec4(0.70f, 0.78f, 0.90f, 1.0f);

    Mesh *castleMesh = MeshManager::instance().load("collision_castel_b3d",
                                                    "../assets/b3d/castel/castel.b3d",
                                                    "../assets/b3d/castel/textures");
    Mesh *groundMesh = MeshManager::instance().create_plane("collision_ground", 24.0f, 24.0f, 12);
    Mesh *wallMesh = MeshManager::instance().create_cube("collision_wall", 1.0f);
    Mesh *sphereMesh = MeshManager::instance().create_sphere("collision_player", 1.0f, 16);
    Mesh *playerCapsuleMesh = MeshManager::instance().create_capsule("collision_player_capsule", 1.0f, 2.0f, 16);
    Mesh *obstacleSphereMesh = MeshManager::instance().create_sphere("collision_obstacle_sphere", 1.0f, 14);
    Mesh *obstacleBoxMesh = MeshManager::instance().create_cube("collision_obstacle_box", 1.0f);

    if (!castleMesh || !groundMesh || !wallMesh || !sphereMesh || !playerCapsuleMesh || !obstacleSphereMesh || !obstacleBoxMesh)
    {
        device.Close();
        return 4;
    }

    // sphereMesh->compute_normals();
    // obstacleSphereMesh->compute_normals();
    // wallMesh->compute_normals();
    // obstacleBoxMesh->compute_normals();

    Material groundMat;
    groundMat.name = "collision_ground_mat";
    groundMat.type = MaterialType::Custom;
    groundMat.setVec4("u_color", glm::vec4(0.70f, 0.74f, 0.76f, 1.0f));
    groundMat.setTexture("u_albedo", TextureManager::instance().getWhite());

    Material wallMat;
    wallMat.name = "collision_wall_mat";
    wallMat.type = MaterialType::Custom;
    wallMat.setVec4("u_color", glm::vec4(0.82f, 0.56f, 0.42f, 1.0f));
    wallMat.setTexture("u_albedo", TextureManager::instance().getWhite());

    Material playerMat;
    playerMat.name = "collision_player_mat";
    playerMat.type = MaterialType::Custom;
    playerMat.setVec4("u_color", glm::vec4(0.18f, 0.92f, 0.36f, 1.0f));
    playerMat.setTexture("u_albedo", TextureManager::instance().getWhite());
    playerMat.setBlend(false);
    playerMat.setDepthWrite(true);
    playerMat.setCullFace(true);

    Material obstacleMat;
    obstacleMat.name = "collision_obstacle_mat";
    obstacleMat.type = MaterialType::Custom;
    obstacleMat.setVec4("u_color", glm::vec4(0.88f, 0.25f, 0.25f, 1.0f));
    obstacleMat.setTexture("u_albedo", TextureManager::instance().getWhite());
    obstacleMat.setBlend(false);
    obstacleMat.setDepthWrite(true);
    obstacleMat.setCullFace(true);

    MeshNode *castleNode = scene.createMeshNode("castle_b3d", castleMesh);
    MeshNode *groundNode = scene.createMeshNode("ground", groundMesh);
    MeshNode *wallNode = scene.createMeshNode("wall", wallMesh);
    MeshNode *playerNode = scene.createMeshNode("player", sphereMesh);
    MeshNode *playerCapsuleNode = scene.createMeshNode("player_capsule", playerCapsuleMesh);
    MeshNode *obstacleSphereNode = scene.createMeshNode("obstacle_sphere", obstacleSphereMesh);
    MeshNode *obstacleBoxNode = scene.createMeshNode("obstacle_box", obstacleBoxMesh);

    groundNode->setMaterial(0, &groundMat);
    wallNode->setMaterial(0, &wallMat);
    playerNode->setMaterial(0, &playerMat);
    playerCapsuleNode->setMaterial(0, &playerMat);
    obstacleSphereNode->setMaterial(0, &obstacleMat);
    obstacleBoxNode->setMaterial(0, &obstacleMat);

    castleNode->renderType = RenderType::Solid;
    groundNode->renderType = RenderType::Solid;
    wallNode->renderType = RenderType::Solid;
    playerNode->renderType = RenderType::Solid;
    playerCapsuleNode->renderType = RenderType::Solid;
    obstacleSphereNode->renderType = RenderType::Solid;
    obstacleBoxNode->renderType = RenderType::Solid;

    const float castleScale = 0.02f;
    castleNode->setScale(glm::vec3(castleScale));
    castleNode->setPosition(glm::vec3(0.0f, -castleMesh->aabb.min.y * castleScale, 0.0f));

    groundNode->setPosition(0.0f, 0.0f, 0.0f);
    groundNode->visible = false;
    wallNode->setPosition(0.0f, 1.5f, -4.0f);
    wallNode->setScale(glm::vec3(5.0f, 3.0f, 0.18f));
    wallNode->visible = false;

    RenderBatch debugBatch;
    debugBatch.Init();

    float playerRadius = 0.45f;
    float playerHeight = 1.8f;
    bool playerCapsule = true;
    const glm::vec3 startPlayerPos(0.0f, 15.0f, 14.0f);
    glm::vec3 playerPos = startPlayerPos;
    float playerYaw = 180.0f;
    float verticalSpeed = 0.0f;
    bool grounded = false;
    const float gravity = 18.0f;
    const float jumpSpeed = 7.0f;
    float moveSpeed = 6.0f;
    float turnSpeed = 160.0f;
    glm::vec3 cameraPos(0.0f, 15.0f, 14.0f);
    float cameraYaw = 0.0f;
    float cameraPitch = -8.0f;
    float cameraVerticalSpeed = 0.0f;
    bool cameraGrounded = false;
    float cameraRadius = 0.65f;
    float cameraMoveSpeed = 7.0f;
    float cameraJumpSpeed = 7.0f;
    float cameraGravity = 18.0f;
    float cameraHeight = 1.8f;
    float cameraEyeHeight = 1.4f;
    float cameraGroundSnap = 0.25f;
    SceneCollisionParams sceneParams;
    bool showDebugBatch = true;
    bool showTriangleNormals = true;
    bool showCollisionAabbs = true;
    float normalSize = 0.35f;
    glm::vec3 lastDesired = playerPos;

    cs::CollisionSystem meshCollision;
    cs::CollisionWorld world;
    world.config().epsilon = 0.001f;
    world.config().defaultMaxHits = 10;
    syncObstacleTransforms(sceneParams, wallNode, obstacleSphereNode, obstacleBoxNode);
    rebuildWorld(world, meshCollision, sceneParams, castleNode, obstacleBoxNode);

    int lastHits = 0;
    std::string movementHint = "Camera: WASD + rato esquerdo, Space salta. Capsule debug: setas";

    while (device.Run())
    {
        const float dt = device.GetFrameTime();

        if (device.IsResize())
            camera->setViewport(0, 0, device.GetWidth(), device.GetHeight());

        if (Input::IsMouseDown(MouseButton::LEFT))
        {
            cameraYaw += -Input::GetMouseDelta().x * 0.15f;
            cameraPitch += -Input::GetMouseDelta().y * 0.12f;
            cameraPitch = std::clamp(cameraPitch, -85.0f, 85.0f);
        }
        // Apply rotation before reading camera axes for movement.
        camera->setEulerAngles(glm::vec3(cameraPitch, cameraYaw, 0.0f));

        const float cameraInputX = axis(Input::IsKeyDown(KEY_D), Input::IsKeyDown(KEY_A));
        const float cameraInputZ = axis(Input::IsKeyDown(KEY_W), Input::IsKeyDown(KEY_S));
        glm::vec3 cameraWish(cameraInputX, 0.0f, cameraInputZ);
        if (glm::length2(cameraWish) > 1e-8f)
            cameraWish = glm::normalize(cameraWish);

        glm::vec3 cameraForward = camera->forward();
        cameraForward.y = 0.0f;
        if (glm::length2(cameraForward) <= 1e-8f)
            cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
        else
            cameraForward = glm::normalize(cameraForward);

        glm::vec3 cameraRight = camera->right();
        cameraRight.y = 0.0f;
        if (glm::length2(cameraRight) <= 1e-8f)
            cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            cameraRight = glm::normalize(cameraRight);
        float cameraSpeed = cameraMoveSpeed;
        if (Input::IsKeyDown(KEY_LEFT_SHIFT))
            cameraSpeed *= 1.8f;

        glm::vec3 cameraHorizontalMove = (cameraRight * cameraWish.x + cameraForward * cameraWish.z) * (cameraSpeed * dt);

        if (cameraGrounded && cameraVerticalSpeed < 0.0f)
            cameraVerticalSpeed = 0.0f;

        if (Input::IsKeyPressed(KEY_SPACE) && cameraGrounded)
        {
            cameraVerticalSpeed = cameraJumpSpeed;
            cameraGrounded = false;
        }

        if (!cameraGrounded)
            cameraVerticalSpeed -= cameraGravity * dt;

        const float turnInput = axis(Input::IsKeyDown(KEY_RIGHT), Input::IsKeyDown(KEY_LEFT));
        const float moveInput = axis(Input::IsKeyDown(KEY_UP), Input::IsKeyDown(KEY_DOWN));
        playerYaw += turnInput * turnSpeed * dt;

        const float playerYawRadians = glm::radians(playerYaw);
        const glm::vec3 playerForward = glm::normalize(glm::vec3(std::sin(playerYawRadians), 0.0f, -std::cos(playerYawRadians)));
        glm::vec3 horizontalMove = playerForward * (moveInput * moveSpeed * dt);

        if (Input::IsKeyDown(KEY_LEFT_SHIFT))
            horizontalMove *= 1.8f;

        if (Input::IsKeyPressed(KEY_SPACE) && grounded)
        {
            verticalSpeed = jumpSpeed;
            grounded = false;
        }

        verticalSpeed -= gravity * dt;
        const glm::vec3 verticalMove(0.0f, verticalSpeed * dt, 0.0f);

        syncObstacleTransforms(sceneParams, wallNode, obstacleSphereNode, obstacleBoxNode);
        rebuildWorld(world, meshCollision, sceneParams, castleNode, obstacleBoxNode);

        const float yBeforeMove = cameraPos.y;
        const glm::vec3 cameraFullDelta = cameraHorizontalMove + glm::vec3(0.0f, cameraVerticalSpeed * dt, 0.0f);
        const cs::CollisionMoveResult cameraMove = world.moveCameraCapsule(
            cameraPos,
            cameraPos + cameraFullDelta,
            cameraRadius,
            cameraHeight,
            1,
            0);
        cameraPos = cameraMove.finalPosition;
        const float yRisen = cameraPos.y - yBeforeMove;
        const bool movedUp = yRisen > 0.005f || cameraVerticalSpeed > 0.1f;

        auto cameraOnGround = [&](const glm::vec3 &pos) -> bool
        {
            PickResult groundHit;
            const Ray ray(pos + glm::vec3(0.0f, 0.05f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
            return meshCollision.rayCast(ray, cameraHeight * 0.5f + 0.20f, groundHit) && groundHit.normal.y > 0.45f;
        };

        cameraGrounded = cameraVerticalSpeed <= 0.0f && cameraOnGround(cameraPos);
        if (!cameraGrounded && !movedUp && cameraVerticalSpeed > -2.0f && cameraGroundSnap > 0.0f)
        {
            const cs::CollisionMoveResult snapMove = world.moveCameraCapsule(
                cameraPos,
                cameraPos - glm::vec3(0.0f, cameraGroundSnap, 0.0f),
                cameraRadius,
                cameraHeight,
                1,
                0);
            if (snapMove.hitCount > 0)
            {
                cameraPos = snapMove.finalPosition;
                cameraGrounded = cameraOnGround(cameraPos);
            }
        }

        if (cameraGrounded)
            cameraVerticalSpeed = 0.0f;

        camera->setPosition(cameraPos + glm::vec3(0.0f, cameraEyeHeight, 0.0f));
        camera->setEulerAngles(glm::vec3(cameraPitch, cameraYaw, 0.0f));

        const glm::vec3 moveStart = playerPos;
        const glm::vec3 desired = playerPos + horizontalMove + verticalMove;
        const cs::CollisionMoveResult move = playerCapsule
            ? world.moveCapsule(playerPos, desired, playerRadius, playerHeight, 1, 0)
            : world.moveSphere(playerPos, desired, playerRadius, 1, 0);
        lastDesired = desired;
        playerPos = move.finalPosition;
        lastHits = move.hitCount;
        grounded = move.hitCount > 0 && move.lastHit.normal.y > 0.55f && verticalSpeed <= 0.0f;
        if (grounded)
            verticalSpeed = 0.0f;

        playerNode->setPosition(playerPos);
        playerNode->setScale(glm::vec3(playerRadius));
        playerNode->visible = !playerCapsule;

        playerCapsuleNode->setPosition(playerPos);
        playerCapsuleNode->setScale(glm::vec3(playerRadius, playerHeight * 0.5f, playerRadius));
        playerCapsuleNode->visible = playerCapsule;

        scene.update(dt);

        device.ImGuiBegin();
        ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_Once);
        if (ImGui::Begin("Collision Visual"))
        {
            ImGui::Text("Triangulos mesh collision: %d", meshCollision.triangleCount());
            ImGui::TextUnformatted("Mesh: assets/b3d/castel/castel.b3d");
            ImGui::Text("Hits ultimo step: %d", lastHits);
            ImGui::Text("Camera pos: %.2f %.2f %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
            ImGui::Text("Camera grounded: %s  VSpeed: %.2f", cameraGrounded ? "sim" : "nao", cameraVerticalSpeed);
            ImGui::Text("Capsule pos: %.2f %.2f %.2f", playerPos.x, playerPos.y, playerPos.z);
            ImGui::Text("Grounded: %s  Vertical speed: %.2f", grounded ? "sim" : "nao", verticalSpeed);
            ImGui::TextWrapped("%s", movementHint.c_str());
            ImGui::Separator();

            bool rebuild = false;
            ImGui::Checkbox("Player capsule", &playerCapsule);
            rebuild |= ImGui::SliderFloat("Player radius", &playerRadius, 0.2f, 1.2f, "%.2f");
            rebuild |= ImGui::SliderFloat("Player height", &playerHeight, playerRadius * 2.0f, 3.5f, "%.2f");
            rebuild |= ImGui::SliderFloat("Move speed", &moveSpeed, 1.0f, 20.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("Turn speed", &turnSpeed, 30.0f, 360.0f, "%.1f");
            rebuild |= ImGui::SliderFloat("Camera radius", &cameraRadius, 0.1f, 1.2f, "%.2f");
            rebuild |= ImGui::SliderFloat("Camera speed", &cameraMoveSpeed, 1.0f, 20.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("Camera jump", &cameraJumpSpeed, 1.0f, 20.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("Camera gravity", &cameraGravity, 0.0f, 60.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("Camera height", &cameraHeight, cameraRadius * 2.0f, 3.5f, "%.2f");
            rebuild |= ImGui::SliderFloat("Camera eye height", &cameraEyeHeight, 0.0f, 3.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("Camera ground snap", &cameraGroundSnap, 0.0f, 1.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("World epsilon", &world.config().epsilon, 0.0001f, 0.02f, "%.4f");
            rebuild |= ImGui::SliderInt("Max hits", &world.config().defaultMaxHits, 1, 30);
            ImGui::Separator();
            ImGui::Text("Mover obstaculos");
            rebuild |= ImGui::SliderFloat3("Sphere center", &sceneParams.sphereObstacleCenter.x, -8.0f, 8.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("Sphere radius", &sceneParams.sphereObstacleRadius, 0.2f, 2.0f, "%.2f");
            rebuild |= ImGui::SliderFloat3("Box center", &sceneParams.boxObstacleCenter.x, -8.0f, 8.0f, "%.2f");
            rebuild |= ImGui::SliderFloat3("Box half extents", &sceneParams.boxObstacleHalfExtents.x, 0.2f, 3.0f, "%.2f");
            rebuild |= ImGui::SliderFloat3("Capsule center", &sceneParams.capsuleCenter.x, -8.0f, 8.0f, "%.2f");
            rebuild |= ImGui::SliderFloat("Capsule radius", &sceneParams.capsuleRadius, 0.1f, 1.5f, "%.2f");
            rebuild |= ImGui::SliderFloat("Capsule height", &sceneParams.capsuleHeight, 0.3f, 4.0f, "%.2f");
            ImGui::Separator();
            ImGui::Checkbox("Show batch debug", &showDebugBatch);
            ImGui::Checkbox("Show tri normals", &showTriangleNormals);
            ImGui::Checkbox("Show collision AABBs (on hit)", &showCollisionAabbs);
            ImGui::SliderFloat("Normal size", &normalSize, 0.05f, 1.0f, "%.2f");

            if (ImGui::Button("Reset player"))
            {
                playerPos = startPlayerPos;
                playerYaw = 180.0f;
                verticalSpeed = 0.0f;
                grounded = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset camera"))
            {
                cameraPos = glm::vec3(0.0f, 15.0f, 14.0f);
                cameraYaw = 0.0f;
                cameraPitch = -8.0f;
                cameraVerticalSpeed = 0.0f;
                cameraGrounded = false;
            }

            if (rebuild)
            {
                cameraHeight = Max(cameraHeight, cameraRadius * 2.0f);
                playerHeight = Max(playerHeight, playerRadius * 2.0f);
                sceneParams.sphereObstacleRadius = Max(sceneParams.sphereObstacleRadius, 0.01f);
                sceneParams.boxObstacleHalfExtents = glm::max(sceneParams.boxObstacleHalfExtents, glm::vec3(0.01f));
                sceneParams.capsuleRadius = Max(sceneParams.capsuleRadius, 0.01f);
                sceneParams.capsuleHeight = Max(sceneParams.capsuleHeight, sceneParams.capsuleRadius * 2.0f);
            }
        }
        ImGui::End();
        device.ImGuiEnd();
        // auto &rs = RenderState::instance();
        // rs.setDepthTest(true);
        // rs.setDepthWrite(true);
        // rs.setBlend(false);
        // rs.setCull(true);

        scene.setCamera(camera);
        scene.beginPass();
        scene.setShader(shader);
        setupLighting(shader);
        scene.render(RenderType::Solid);
     //   scene.render(RenderType::Transparent);
        scene.endPass();

        if (showDebugBatch)
        {
            debugBatch.SetMatrix(camera->viewProjection);
            Material::applyDefaultStates();
            drawCollisionDebug(debugBatch,
                               meshCollision,
                               sceneParams,
                               moveStart,
                               lastDesired,
                               move.finalPosition,
                               playerForward,
                               playerRadius,
                               playerHeight,
                               playerCapsule,
                               lastHits > 0,
                               showCollisionAabbs,
                               showTriangleNormals,
                               normalSize);
            debugBatch.Render();
        }

        device.Flip();
    }

    scene.clear();
    device.Close();
    return 0;
}
