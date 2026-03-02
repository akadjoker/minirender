#pragma once
#include "DemoBase.hpp"
#include "Collision.hpp"
#include "RenderState.hpp"
#include <random>

// ============================================================
//  DemoCannonball
//  Fires cannonballs that collide with a set of walls/columns.
//  When a projectile's lifetime expires it is marked for removal.
//  Demonstrates: picking, deferred-remove queue, CollisionSystem.
//
//  Controls:
//    Space          — fire cannonball in camera look direction
//    Left click     — pick mesh under cursor (logs hit info)
//    F              — toggle wire-frame
// ============================================================
class DemoCannonball : public DemoBase
{
public:
    const char *name() override { return "Cannonball"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 5.f, 30.f});
        camera->lookAt({0.f, 3.f, 0.f});

        auto *litShader = shaders().load("lit_shadow",
            "assets/shaders/lit_shadow.vert","assets/shaders/lit_shadow.frag");
        if (!litShader) return false;

        litShader_ = litShader;

        auto *white = textures().getWhite();
        auto *texWall = textures().load("tex_wall","assets/wall.jpg");

        matGround_ = materials().create("cb_ground");
        matGround_->setShader(litShader)->setTexture("u_albedo", texWall ? texWall : white);

        matWall_ = materials().create("cb_wall");
        matWall_->setShader(litShader)->setTexture("u_albedo", texWall ? texWall : white);

        matBall_ = materials().create("cb_ball");
        matBall_->setShader(litShader)->setTexture("u_albedo", white);

        buildArena();
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt;

        // Update projectiles — gravity + collision + deferred remove
        for (auto it = projectiles_.begin(); it != projectiles_.end(); )
        {
            auto &p = *it;
            p.lifetime -= dt;

            // Gravity
            p.velocity.y -= 18.f * dt;

            // Collide
            bool grounded = false;
            glm::vec3 newPos = collision_.sphereSlide(
                static_cast<Node3D*>(p.node)->position, p.velocity * dt, p.radius,
                {0.f, -9.8f * dt, 0.f}, grounded);

            if (grounded || p.lifetime <= 0.f)
            {
                scene.markForRemoval(p.node);
                it = projectiles_.erase(it);
                continue;
            }

            p.node->setPosition(newPos);
            ++it;
        }

        // Fire on SPACE (debounce)
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_SPACE] && fireTimer_ <= 0.f)
        {
            fireBall();
            fireTimer_ = 0.4f;
        }
        if (fireTimer_ > 0.f) fireTimer_ -= dt;

        // Toggle wireframe on F  (OpenGL desktop only)
        if (keys[SDL_SCANCODE_F] && wireframeTimer_ <= 0.f)
        {
            wireframe_ = !wireframe_;
            wireframeTimer_ = 0.3f;
        }
        if (wireframeTimer_ > 0.f) wireframeTimer_ -= dt;
    }

    void render()  override { DemoBase::render(); }

    void release() override
    {
        DemoBase::release();
    }

private:
    struct Projectile
    {
        ManualMeshNode *node   = nullptr;
        glm::vec3       velocity;
        float           radius = 0.3f;
        float           lifetime = 2.f;
    };

    std::vector<Projectile> projectiles_;
    CollisionSystem         collision_;
    Shader                 *litShader_    = nullptr;
    Material               *matGround_   = nullptr;
    Material               *matWall_     = nullptr;
    Material               *matBall_     = nullptr;
    float                   time_        = 0.f;
    float                   fireTimer_   = 0.f;
    float                   wireframeTimer_ = 0.f;
    bool                    wireframe_   = false;

    // ── Build scene ────────────────────────────────────────
    void buildArena()
    {
        // Ground
        {
            auto *node = new ManualMeshNode();
            node->name = "ground";
            node->material = matGround_;
            node->begin(GL_TRIANGLES);
            float s = 20.f;
            node->normal(0,1,0).texCoord(0,0).position(-s,0,-s);
            node->normal(0,1,0).texCoord(4,0).position( s,0,-s);
            node->normal(0,1,0).texCoord(4,4).position( s,0, s);
            node->normal(0,1,0).texCoord(0,4).position(-s,0, s);
            node->triangle(2,1,0).triangle(0,3,2);
            node->end();
            // Add ground triangles to collision
            Triangle t1; t1.v0={-s,0,-s}; t1.v1={s,0,-s};  t1.v2={s,0,s};
            Triangle t2; t2.v0={s,0,s};   t2.v1={-s,0,s};  t2.v2={-s,0,-s};
            collision_.addTriangle(t1);
            collision_.addTriangle(t2);
            scene.add(node);
        }

        // Obstruction columns
        static const glm::vec3 cols[] = {
            {-6,0,-4}, {6,0,-4}, {0,0,-8}, {-10,0,0}, {10,0,0}
        };
        for (const auto &pos : cols)
            addColumn(pos, 1.f, 6.f);
    }

    void addColumn(glm::vec3 base, float r, float h)
    {
        auto *node = new ManualMeshNode();
        node->name = "column";
        node->material = matWall_;
        buildBox(node, base + glm::vec3(0, h*0.5f, 0), r*2, h, r*2);
        scene.add(node);

        // Add box triangles to collision
        float hw = r, hd = r, hh = h*0.5f;
        glm::vec3 c = base + glm::vec3(0, hh, 0);
        // simplified: 2 tris per face × 6 faces
        auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 d)
        {
            Triangle t1; t1.v0=a;  t1.v1=b;  t1.v2=cc;
            Triangle t2; t2.v0=cc; t2.v1=d;  t2.v2=a;
            collision_.addTriangle(t1);
            collision_.addTriangle(t2);
        };
        addFace(c+glm::vec3(-hw,-hh,-hd),c+glm::vec3(hw,-hh,-hd),c+glm::vec3(hw,hh,-hd),c+glm::vec3(-hw,hh,-hd));
        addFace(c+glm::vec3(hw,-hh,hd), c+glm::vec3(-hw,-hh,hd),c+glm::vec3(-hw,hh,hd),c+glm::vec3(hw,hh,hd));
        addFace(c+glm::vec3(-hw,-hh,hd),c+glm::vec3(-hw,-hh,-hd),c+glm::vec3(-hw,hh,-hd),c+glm::vec3(-hw,hh,hd));
        addFace(c+glm::vec3(hw,-hh,-hd),c+glm::vec3(hw,-hh,hd), c+glm::vec3(hw,hh,hd), c+glm::vec3(hw,hh,-hd));
        addFace(c+glm::vec3(-hw,hh,-hd),c+glm::vec3(hw,hh,-hd), c+glm::vec3(hw,hh,hd), c+glm::vec3(-hw,hh,hd));
        addFace(c+glm::vec3(-hw,-hh,hd),c+glm::vec3(hw,-hh,hd), c+glm::vec3(hw,-hh,-hd),c+glm::vec3(-hw,-hh,-hd));
    }

    void fireBall()
    {
        glm::vec3 dir   = camera->forward();
        glm::vec3 start = camera->worldPosition() + dir * 1.5f;

        float r = 0.3f;
        auto *node = buildSphere(start, r);
        node->name = "ball";
        node->material = matBall_;
        scene.add(node);

        Projectile p;
        p.node     = node;
        p.velocity = dir * 28.f;
        p.radius   = r;
        p.lifetime = 6.f;
        projectiles_.push_back(p);
    }

    ManualMeshNode* buildSphere(glm::vec3 centre, float r)
    {
        auto *node = new ManualMeshNode();
        node->begin(GL_TRIANGLES);
        const int stacks = 10, slices = 14;
        for (int i = 0; i <= stacks; ++i)
        {
            float phi = (float)i / stacks * glm::pi<float>();
            for (int j = 0; j <= slices; ++j)
            {
                float theta = (float)j / slices * glm::two_pi<float>();
                glm::vec3 n = {std::sin(phi)*std::cos(theta),
                               std::cos(phi),
                               std::sin(phi)*std::sin(theta)};
                node->normal(n.x,n.y,n.z)
                     .texCoord((float)j/slices, (float)i/stacks)
                     .position(centre.x+r*n.x, centre.y+r*n.y, centre.z+r*n.z);
            }
        }
        for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j)
        {
            uint32_t a = i*(slices+1)+j;
            node->triangle(a, a+slices+1, a+1)
                 .triangle(a+1, a+slices+1, a+slices+2);
        }
        node->end();
        return node;
    }

    void buildBox(ManualMeshNode *node, glm::vec3 centre, float w, float h, float d)
    {
        float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;
        node->begin(GL_TRIANGLES);
        struct Face { glm::vec3 n; glm::vec3 v[4]; glm::vec2 uv[4]; };
        std::vector<Face> faces {
            {{0,0,-1},{{ -hw,-hh,-hd},{hw,-hh,-hd},{hw,hh,-hd},{-hw,hh,-hd}},{{0,0},{1,0},{1,1},{0,1}}},
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
                     .texCoord(f.uv[i].x,f.uv[i].y)
                     .position(centre.x+f.v[i].x,centre.y+f.v[i].y,centre.z+f.v[i].z);
            node->triangle(base+2,base+1,base).triangle(base,base+3,base+2);
        }
        node->end();
    }

};
