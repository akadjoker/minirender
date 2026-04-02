#pragma once
#include "DemoBase.hpp"
#include "Batch.hpp"
#include "Color.hpp"
#include "Input.hpp"
#include "Device.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// ============================================================
//  DemoBatch — showcase de primitivas 2D e 3D do RenderBatch
//  [1] 3D shapes   [2] 2D shapes   [3] 2D curves
// ============================================================
class DemoBatch : public DemoBase
{
public:
    const char *name() override { return "Batch Demo"; }

    bool init() override
    {
        DemoBase::init();
        camera->setPosition({0.f, 8.f, 22.f});
        camera->lookAt({0.f, 2.f, 0.f});
        static_cast<FreeCameraController *>(camera->getController())->moveSpeed = 15.f;

        batch.Init();
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
        time += dt;

        if (Input::IsKeyPressed(KEY_ONE))   page = 0;
        if (Input::IsKeyPressed(KEY_TWO))   page = 1;
        if (Input::IsKeyPressed(KEY_THREE)) page = 2;
    }

    void render() override
    {
        // No scene geometry — batch only
        int W = Device::Instance().GetWidth();
        int H = Device::Instance().GetHeight();

        glm::mat4 viewProj = camera->getProjection() * camera->getView();
        glm::mat4 ortho    = glm::ortho(0.f, (float)W, (float)H, 0.f, -1.f, 1.f);

        if (page == 0)      render3D(viewProj);
        else if (page == 1) render2D(ortho, W, H);
        else                renderCurves(ortho, W, H);
    }

    void release() override
    {
        batch.Release();
        DemoBase::release();
    }

private:
    // ── helpers ─────────────────────────────────────────────
    void hsv(float h, float s, float v)
    {
        float c = v * s;
        float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
        float m = v - c;
        float r = 0, g = 0, b = 0;
        if      (h < 60)  { r=c; g=x; }
        else if (h < 120) { r=x; g=c; }
        else if (h < 180) { g=c; b=x; }
        else if (h < 240) { g=x; b=c; }
        else if (h < 300) { r=x; b=c; }
        else              { r=c; b=x; }
        batch.SetColor((u8)((r+m)*255), (u8)((g+m)*255), (u8)((b+m)*255), 255);
    }

    // ── PAGE 0 — 3D ─────────────────────────────────────────
    void render3D(const glm::mat4 &vp)
    {
        batch.SetMatrix(vp);

        // Grid + axes
        batch.SetColor(80, 80, 80, 255);
        batch.Grid(14, 1.f, true);

        // Animated cube (wire)
        batch.SetColor(100, 200, 255, 255);
        {
            glm::mat4 t = glm::translate(glm::mat4(1.f), {-8.f, 1.f, 0.f});
            t = glm::rotate(t, time * 0.9f, {0,1,0});
            batch.BeginTransform(t);
            batch.Cube({0,0,0}, 2.f, 2.f, 2.f, true);
            batch.EndTransform();
        }

        // Solid cube
        batch.SetColor(255, 180, 60, 200);
        {
            glm::mat4 t = glm::translate(glm::mat4(1.f), {-5.f, 1.f, 0.f});
            t = glm::rotate(t, time * 0.7f, {0,1,0});
            batch.BeginTransform(t);
            batch.Cube({0,0,0}, 2.f, 2.f, 2.f, false);
            batch.EndTransform();
        }

        // Sphere (wire)
        batch.SetColor(80, 255, 120, 255);
        batch.Sphere({-1.5f, 1.5f, 0.f}, 1.5f, 8, 12, true);

        // Sphere (solid)
        batch.SetColor(60, 180, 255, 200);
        batch.Sphere({1.5f, 1.5f, 0.f}, 1.5f, 8, 12, false);

        // Cylinder (wire)
        batch.SetColor(255, 100, 100, 255);
        batch.Cylinder({4.5f, 0.f, 0.f}, 0.8f, 2.5f, 16, true);

        // Cone (solid)
        batch.SetColor(200, 100, 255, 200);
        batch.Cone({7.f, 0.f, 0.f}, 0.9f, 2.5f, 16, false);

        // Capsule
        batch.SetColor(255, 220, 80, 255);
        batch.Capsule({9.5f, 0.5f, 0.f}, 0.7f, 2.f, 12, true);

        // Circle3D (facing Y)
        batch.SetColor(255, 80, 200, 255);
        batch.CircleXZ({0.f, 0.01f, -5.f}, 3.f, 32);

        // Circle3D (facing arbitrary normal)
        batch.SetColor(80, 255, 220, 255);
        batch.Circle3D({0.f, 4.f, -5.f},
                       2.f,
                       glm::normalize(glm::vec3(0.f, 0.f, 1.f)),
                       32);

        // 3D lines connecting shapes
        batch.SetColor(160, 160, 160, 180);
        batch.Line3D({-8.f, 2.f, 0.f}, {9.5f, 2.f, 0.f});

        batch.Render();
    }

    // ── PAGE 1 — 2D shapes ──────────────────────────────────
    void render2D(const glm::mat4 &ortho, int W, int H)
    {
        batch.SetMatrix(ortho);
        const int cols  = 4;
        const int cellW = W / cols;
        const int cellH = H / 3;

        // helper: centre of cell (col, row)
        #define CELL_X(col) ((col) * cellW + cellW/2)
        #define CELL_Y(row) ((row) * cellH + cellH/2)

        // Row 0 ─────────────────────────────────────────────
        // 0,0 — circle fill
        batch.SetColor(100, 180, 255, 255);
        batch.Circle(CELL_X(0), CELL_Y(0), 60.f, true);
        batch.SetColor(200, 230, 255, 255);
        batch.Circle(CELL_X(0), CELL_Y(0), 60.f, false);

        // 1,0 — rectangle fill + outline
        batch.SetColor(255, 160, 60, 255);
        { int rx = CELL_X(1), ry = CELL_Y(0);
          batch.Rectangle(rx-55, ry-40, 110, 80, true);
          batch.SetColor(255, 220, 120, 255);
          batch.Rectangle(rx-55, ry-40, 110, 80, false); }

        // 2,0 — rounded rectangle
        batch.SetColor(120, 255, 140, 255);
        { int rx = CELL_X(2), ry = CELL_Y(0);
          batch.RoundedRectangle(rx-60, ry-45, 120, 90, 0.35f, 10, true);
          batch.SetColor(200, 255, 210, 255);
          batch.RoundedRectangle(rx-60, ry-45, 120, 90, 0.35f, 10, false); }

        // 3,0 — triangle
        batch.SetColor(255, 80, 120, 255);
        { int tx = CELL_X(3), ty = CELL_Y(0);
          batch.Triangle((float)tx, (float)(ty-60), (float)(tx-55), (float)(ty+50),
                         (float)(tx+55), (float)(ty+50), true);
          batch.SetColor(255, 180, 200, 255);
          batch.Triangle((float)tx, (float)(ty-60), (float)(tx-55), (float)(ty+50),
                         (float)(tx+55), (float)(ty+50), false); }

        // Row 1 ─────────────────────────────────────────────
        // 0,1 — ellipse
        batch.SetColor(180, 120, 255, 255);
        batch.Ellipse(CELL_X(0), CELL_Y(1), 80.f, 45.f, true);
        batch.SetColor(220, 190, 255, 255);
        batch.Ellipse(CELL_X(0), CELL_Y(1), 80.f, 45.f, false);

        // 1,1 — polygon (hexagon)
        batch.SetColor(60, 220, 200, 255);
        batch.Polygon(CELL_X(1), CELL_Y(1), 6, 60.f, time * 30.f, true);
        batch.SetColor(200, 255, 250, 255);
        batch.Polygon(CELL_X(1), CELL_Y(1), 6, 60.f, time * 30.f, false);

        // 2,1 — circle sector
        batch.SetColor(255, 200, 60, 255);
        batch.CircleSector(CELL_X(2), CELL_Y(1), 65.f, 0.f, 270.f, 24, true);
        batch.SetColor(255, 230, 160, 255);
        batch.CircleSector(CELL_X(2), CELL_Y(1), 65.f, 0.f, 270.f, 24, false);

        // 3,1 — ring + arc
        batch.SetColor(80, 160, 255, 255);
        batch.Ring(CELL_X(3), CELL_Y(1), 35.f, 60.f, 0.f, 360.f, 32, true);
        batch.SetColor(255, 100, 80, 255);
        batch.Arc(CELL_X(3), CELL_Y(1), 70.f, 45.f, 315.f, 6.f, 32);

        // Row 2 ─────────────────────────────────────────────
        // 0,2 — thick line
        batch.SetColor(255, 200, 80, 255);
        { int lx = CELL_X(0), ly = CELL_Y(2);
          batch.ThickLine2D((float)(lx-70), (float)(ly-30),
                            (float)(lx+70), (float)(ly+30), 8.f); }

        // 1,2 — polyline
        batch.SetColor(80, 220, 255, 255);
        { int plx = CELL_X(1), ply = CELL_Y(2);
          glm::vec2 pts[5] = {
              {(float)(plx-70),(float)(ply+30)},
              {(float)(plx-35),(float)(ply-40)},
              {(float)(plx),   (float)(ply+20)},
              {(float)(plx+35),(float)(ply-40)},
              {(float)(plx+70),(float)(ply+30)},
          };
          batch.Polyline(pts, 5); }

        // 2,2 — 2D grid
        batch.SetColor(120, 120, 120, 200);
        { int gx = CELL_X(2), gy = CELL_Y(2);
          batch.Grid(gx-70, gy-50, 140, 100, 20, 20); }

        // 3,2 — animated star
        batch.SetColor(255, 220, 40, 255);
        { float pulse = 50.f + 15.f * std::sinf(time * 2.f);
          batch.Polygon(CELL_X(3), CELL_Y(2), 5, (int)pulse, time * 45.f, true);
          batch.SetColor(255, 255, 200, 255);
          batch.Polygon(CELL_X(3), CELL_Y(2), 5, (int)pulse, time * 45.f, false); }

        #undef CELL_X
        #undef CELL_Y

        batch.Render();
    }

    // ── PAGE 2 — curves ─────────────────────────────────────
    void renderCurves(const glm::mat4 &ortho, int W, int H)
    {
        batch.SetMatrix(ortho);
        const int hh = H / 3;

        // ── Bezier quadratic ─────────────────────────────────
        batch.SetColor(100, 200, 255, 255);
        batch.BezierQuadratic(
            {80.f,  (float)(hh*0 + hh - 40)},
            {(float)(W/2), (float)(hh*0 + 40)},
            {(float)(W-80), (float)(hh*0 + hh - 40)},
            40);

        // control points
        batch.SetColor(255, 100, 100, 180);
        batch.Circle(80, hh - 40, 5.f, true);
        batch.Circle(W/2, 40, 5.f, true);
        batch.Circle(W-80, hh - 40, 5.f, true);
        batch.SetColor(80, 80, 80, 120);
        batch.Line2D({80.f, (float)(hh - 40)}, {(float)(W/2), 40.f});
        batch.Line2D({(float)(W/2), 40.f}, {(float)(W-80), (float)(hh - 40)});

        // ── Bezier cubic ─────────────────────────────────────
        float y0 = (float)(hh + 40);
        float y1 = (float)(hh + hh / 2);
        batch.SetColor(120, 255, 140, 255);
        batch.BezierCubic(
            {80.f, y1},
            {(float)(W/3), y0},
            {(float)(W*2/3), y1 + (hh/2 - 40)},
            {(float)(W-80), y0},
            40);

        // ── CatmullRom spline ────────────────────────────────
        float base = (float)(hh * 2 + 20);
        glm::vec2 cr[6] = {
            {80.f,           base + 80.f},
            {(float)(W/5),   base + 10.f},
            {(float)(W*2/5), base + 100.f},
            {(float)(W*3/5), base + 10.f},
            {(float)(W*4/5), base + 80.f},
            {(float)(W-80),  base + 20.f},
        };
        batch.SetColor(255, 160, 60, 255);
        batch.CatmullRomSpline(cr, 6, 20);

        // dots on control points
        batch.SetColor(255, 220, 120, 255);
        for (auto &p : cr) batch.Circle((int)p.x, (int)p.y, 4.f, true);

        // ── ThickSpline ──────────────────────────────────────
        float tbase = base + 100.f;
        glm::vec2 ts[5] = {
            {100.f,           tbase + 40.f},
            {(float)(W/4),    tbase - 20.f},
            {(float)(W/2),    tbase + 60.f},
            {(float)(W*3/4),  tbase - 20.f},
            {(float)(W-100),  tbase + 40.f},
        };
        batch.SetColor(200, 80, 255, 180);
        batch.ThickSpline(ts, 5, 6.f, 20);

        batch.Render();
    }

    RenderBatch batch;
    float       time = 0.f;
    int         page = 0;   // 0=3D  1=2D  2=curves
};
