#pragma once
#include "DemoBase.hpp"
#include "Vertex.hpp"
#include "RenderState.hpp"
#include "Input.hpp"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
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
            vb_->SetSubData(0, vertexCount_, vertices_.data());
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
        vertices_ = {
            TestVertex{{-0.75f, -0.60f, 0.0f}, {0.f, 1.f}},
            TestVertex{{ 0.75f, -0.60f, 0.0f},  {1.f, 1.f}},
            TestVertex{{ 0.75f,  0.60f, 0.0f},   {1.f, 0.f}},
            TestVertex{{-0.75f,  0.60f, 0.0f},   {0.f, 0.f}},
        };
        baseVertices_ = vertices_;
        indices_ = {0, 1, 2, 2, 3, 0};
        vertexCount_ = static_cast<u32>(vertices_.size());
        indexCount_  = static_cast<u32>(indices_.size());
    }

    void rebuildVertices()
    {
        vertices_ = baseVertices_;
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


        vb_->SetData(vertices_.data());
        ib_->SetData(indices_.data());
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
    std::array<TestVertex, 4> baseVertices_ = {};
    std::array<TestVertex, 4> vertices_ = {};
    std::array<u32, 6> indices_ = {};
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
