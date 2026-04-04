#pragma once
#include "DemoBase.hpp"
#include "Vertex.hpp"
#include "RenderState.hpp"
#include "Input.hpp"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

class DemoVertexArray : public DemoBase
{
public:
    const char *name() override { return "VertexArray Test"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 2.f, 7.f});
        camera->lookAt({0.f, 0.f, 0.f});
        camera->nearPlane = 0.1f;
        camera->farPlane  = 100.f;
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 12.f;

        shader_ = shaders().load("vertex_array_test",
            "assets/shaders/vertex_array_test.vert",
            "assets/shaders/vertex_array_test.frag");
        if (!shader_)
            return false;

        buildQuad();
        uploadAll(true, true);
        VertexArray::ResetStats();
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time_ += dt;

        if (animateVertices_)
        {
            rebuildVertices();
            vb_->SetSubData(0, vertexCount_, vertices_);
        }
    }

    void render() override
    {
        auto &rs = RenderState::instance();
        rs.setDepthTest(false);
        rs.setDepthWrite(false);
        rs.setBlend(false);
        rs.setCull(false);


        rs.useProgram(shader_->getId());

        shader_->setVec3("a_color", color_);

        if (resetStatsEveryFrame_)
            VertexArray::ResetStats();

        vao_.Render(GL_TRIANGLES, indexCount_);

        if (drawWire_)
            vao_.Render(GL_LINE_LOOP, 4);

        onImGui();
    }

    void release() override
    {
        vao_.Release();
        DemoBase::release();
    }

private:
    struct TestVertex
    {
        glm::vec3 position;
        
        glm::vec2 uv;
    };

    void buildQuad()
    {
        vertices_[0] = TestVertex{{-0.75f, -0.60f, 0.0f}, {0.f, 1.f}};
        vertices_[1] = TestVertex{{ 0.75f, -0.60f, 0.0f}, {1.f, 1.f}};
        vertices_[2] = TestVertex{{ 0.75f,  0.60f, 0.0f}, {1.f, 0.f}};
        vertices_[3] = TestVertex{{-0.75f,  0.60f, 0.0f}, {0.f, 0.f}};

        for (u32 i = 0; i < 4; ++i)
            baseVertices_[i] = vertices_[i];

        indices_[0] = 0;
        indices_[1] = 1;
        indices_[2] = 2;
        indices_[3] = 2;
        indices_[4] = 3;
        indices_[5] = 0;

        vertexCount_ = 4;
        indexCount_  = 6;
    }

    void rebuildVertices()
    {
        for (u32 i = 0; i < vertexCount_; ++i)
            vertices_[i] = baseVertices_[i];
        for (u32 i = 0; i < vertexCount_; ++i)
        {
            float phase = time_ * speed_ + static_cast<float>(i) * 0.7f;
            vertices_[i].position.z = std::sin(phase) * amplitude_;
        }
    }

    void uploadAll(bool createBuffers, bool setDecl)
    {
        if (createBuffers)
        {
            vb_ = vao_.AddVertexBuffer(sizeof(TestVertex), vertexCount_, true);
            ib_ = vao_.CreateIndexBuffer(indexCount_, true, false);
        }

        if (setDecl)
        {
            auto *decl = vao_.GetVertexDeclaration();
            decl->Clear();
            decl->AddElement(0, 0, VET_FLOAT3, VES_POSITION);
            decl->AddElement(0, 3 * sizeof(float), VET_FLOAT2, VES_TEXCOORD, 0);
        }


        vb_->SetData(vertices_);
        ib_->SetData(indices_);
        vao_.Build();
    }

    void onImGui()
    {
        const VertexDrawStats &stats = VertexArray::GetStats();

        ImGui::SetNextWindowPos({10, 100}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({360, 320}, ImGuiCond_Once);
        ImGui::Begin("VertexArray Test");

        ImGui::Checkbox("Animate vertices", &animateVertices_);
        ImGui::Checkbox("Reset stats every frame", &resetStatsEveryFrame_);
        ImGui::Checkbox("Draw wire overlay", &drawWire_);
        ImGui::SliderFloat("Amplitude", &amplitude_, 0.f, 1.5f);
        ImGui::SliderFloat("Speed", &speed_, 0.f, 8.f);
        ImGui::ColorEdit3("Color", &color_.x);

        if (ImGui::Button("Reset stats"))
            VertexArray::ResetStats();

        ImGui::SeparatorText("Buffers");
        ImGui::Text("Vertices: %u", vertexCount_);
        ImGui::Text("Indices: %u", indexCount_);
        ImGui::Text("Dynamic VB updates: %s", animateVertices_ ? "yes" : "no");

        ImGui::SeparatorText("Draw Stats");
        ImGui::Text("drawCalls: %u", stats.drawCalls);
        ImGui::Text("indexedDrawCalls: %u", stats.indexedDrawCalls);
        ImGui::Text("instancedCalls: %u", stats.instancedCalls);
        ImGui::Text("triangles: %u", stats.triangles);
        ImGui::Text("lines: %u", stats.lines);
        ImGui::Text("points: %u", stats.points);
        ImGui::Text("submittedVertices: %u", stats.submittedVertices);
        ImGui::Text("submittedIndices: %u", stats.submittedIndices);
        ImGui::Text("instances: %u", stats.instances);

        ImGui::End();
    }

    VertexArray vao_;
    VertexBuffer *vb_ = nullptr;
    IndexBuffer  *ib_ = nullptr;
    Shader *shader_ = nullptr;
    TestVertex baseVertices_[4];
    TestVertex vertices_[4];
    u32 indices_[6];
    u32 vertexCount_ = 0;
    u32 indexCount_ = 0;
    float time_ = 0.f;
    float amplitude_ = 0.45f;
    float speed_ = 2.0f;
    bool animateVertices_ = true;
    bool resetStatsEveryFrame_ = true;
    bool drawWire_ = false;
    glm::vec3 color_ = {0.95f, 0.55f, 0.2f};
};
