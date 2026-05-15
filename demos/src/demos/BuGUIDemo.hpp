#pragma once

#include "Demo.hpp"
#include <BuGUI.hpp>
#include <cmath>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  BuGUIDemo — desenha diretamente via BuGUI::GetDrawList(), sem WidgetApp.
//  O BuGUIRenderer em Device::Flip() trata do lado GL.
// ─────────────────────────────────────────────────────────────────────────────

class BuGUIDemo : public IDemo
{
public:
    const char* title()       const override { return "BuGUI Direct"; }
    const char* description() const override
    {
        return "Desenho direto via BuGUI::GetDrawList() sem WidgetApp. "
               "Mostra que o BuGUIRenderer esta integrado no Device::Flip().";
    }

    bool setup(Device& device) override
    {
        device_ = &device;
        return true;
    }

    void update(float dt) override
    {
        elapsed_ += dt;
    }

    void drawGui() override
    {
        const BuGUI::Font& font = device_->GetBuGUIFontAtlas()->defaultFont();
        BuGUI::DrawList&   dl   = BuGUI::GetDrawList();
        const BuGUI::IO&   io   = BuGUI::GetIO();

        const float W  = io.displayWidth;
        const float H  = io.displayHeight;
        const float lh = font.lineHeight();
        (void)H;

        // ── Painel central ────────────────────────────────────────────────
        const float pw  = 380.0f;
        const float px  = (W - pw) * 0.5f;
        const float py  = 80.0f;
        const float pad = 16.0f;

        float cy = py + pad;

        dl.addRoundRectFilled({px, py, pw, 280.0f}, 8.0f,
                              BuGUI::Color(30, 35, 45, 235));
        dl.addRoundRect({px, py, pw, 280.0f}, 8.0f,
                        BuGUI::Color(70, 80, 100, 200));

        // Titulo
        const char* titleStr = "BuGUI Direct Draw";
        float tw = dl.calcTextSize(font, titleStr).x;
        dl.addText(font, {px + (pw - tw) * 0.5f, cy + font.ascender()},
                   BuGUI::Color(220, 235, 255, 255), titleStr);
        cy += lh + 4.0f;

        dl.addLine({px + pad, cy + 4.0f}, {px + pw - pad, cy + 4.0f},
                   BuGUI::Color(70, 80, 100, 180));
        cy += 14.0f;

        // ── Slider ────────────────────────────────────────────────────────
        const float sliderX = px + pad + 60.0f;
        const float sliderW = pw - pad * 2.0f - 60.0f - 50.0f;
        const float sliderY = cy;
        const float sliderH = 18.0f;
        const float knobR   = 8.0f;

        dl.addText(font, {px + pad, sliderY + font.ascender()},
                   BuGUI::Color(180, 195, 215, 220), "Valor");

        dl.addRoundRectFilled({sliderX, sliderY + sliderH * 0.5f - 3.0f, sliderW, 6.0f},
                              3.0f, BuGUI::Color(55, 65, 80, 255));
        const float fillW = sliderW * (sliderVal_ / 100.0f);
        if (fillW > 0.0f)
            dl.addRoundRectFilled({sliderX, sliderY + sliderH * 0.5f - 3.0f, fillW, 6.0f},
                                  3.0f, BuGUI::Color(80, 160, 255, 255));

        const float knobX = sliderX + fillW;
        dl.addCircleFilled({knobX, sliderY + sliderH * 0.5f}, knobR,
                           sliderDrag_ ? BuGUI::Color(120, 190, 255, 255)
                                       : BuGUI::Color(200, 220, 255, 255));
        dl.addCircle({knobX, sliderY + sliderH * 0.5f}, knobR,
                     BuGUI::Color(80, 140, 210, 200));

        char valBuf[16];
        std::snprintf(valBuf, sizeof(valBuf), "%.1f", sliderVal_);
        dl.addText(font, {sliderX + sliderW + 8.0f, sliderY + font.ascender()},
                   BuGUI::Color(180, 200, 230, 220), valBuf);

        if (io.mouseDown[0])
        {
            const float mx = io.mouseX, my = io.mouseY;
            const bool overTrack = (mx >= sliderX - knobR && mx <= sliderX + sliderW + knobR &&
                                    my >= sliderY && my <= sliderY + sliderH);
            if (!sliderDrag_ && overTrack) sliderDrag_ = true;
        }
        else { sliderDrag_ = false; }
        if (sliderDrag_)
        {
            float t = (io.mouseX - sliderX) / sliderW;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            sliderVal_ = t * 100.0f;
        }

        cy += sliderH + 12.0f;

        dl.addLine({px + pad, cy}, {px + pw - pad, cy},
                   BuGUI::Color(70, 80, 100, 120));
        cy += 10.0f;

        // ── Botoes ────────────────────────────────────────────────────────
        auto drawButton = [&](BuGUI::Rect r, const char* label, bool& clicked)
        {
            const bool hov  = (io.mouseX >= r.x && io.mouseX <= r.x + r.w &&
                               io.mouseY >= r.y && io.mouseY <= r.y + r.h);
            const bool prsd = hov && io.mouseDown[0];
            const BuGUI::Color bg = prsd ? BuGUI::Color(50, 100, 180, 255)
                                         : hov  ? BuGUI::Color(60, 75, 100, 255)
                                                : BuGUI::Color(45, 58, 78, 255);
            dl.addRoundRectFilled(r, 5.0f, bg);
            dl.addRoundRect(r, 5.0f, BuGUI::Color(80, 100, 130, 200));
            float lw = dl.calcTextSize(font, label).x;
            dl.addText(font,
                       {r.x + (r.w - lw) * 0.5f,
                        r.y + (r.h - lh) * 0.5f + font.ascender()},
                       BuGUI::Color(220, 235, 255, 255), label);
            clicked = hov && !io.mouseDown[0] && prevMouse0_;
        };

        const float bw = (pw - pad * 2.0f - 8.0f) * 0.5f;
        bool clickInc = false, clickReset = false;
        drawButton({px + pad,              cy, bw, 30.0f}, "Incrementar", clickInc);
        drawButton({px + pad + bw + 8.0f,  cy, bw, 30.0f}, "Reset",      clickReset);
        if (clickInc)   ++counter_;
        if (clickReset) counter_ = 0;
        cy += 30.0f + 10.0f;

        // ── Status ────────────────────────────────────────────────────────
        dl.addLine({px + pad, cy}, {px + pw - pad, cy},
                   BuGUI::Color(70, 80, 100, 120));
        cy += 8.0f;

        const int fps = io.deltaTime > 0.0f ? static_cast<int>(1.0f / io.deltaTime) : 0;
        char statBuf[64];
        std::snprintf(statBuf, sizeof(statBuf), "Counter: %d     FPS: %d", counter_, fps);
        float sw = dl.calcTextSize(font, statBuf).x;
        dl.addText(font, {px + (pw - sw) * 0.5f, cy + font.ascender()},
                   BuGUI::Color(140, 175, 220, 200), statBuf);

        // ── Circulo animado ───────────────────────────────────────────────
        const float circX  = px + pw * 0.5f;
        const float circY  = py + 280.0f + 55.0f;
        const float radius = 20.0f + 10.0f * std::sin(elapsed_ * 2.0f);
        const uint8_t alpha = static_cast<uint8_t>(
            100 + 100 * (0.5f + 0.5f * std::sin(elapsed_ * 3.0f)));
        const uint8_t blue = static_cast<uint8_t>(sliderVal_ * 2.55f);
        dl.addCircleFilled({circX, circY}, radius,
                           BuGUI::Color(80, 160, blue, alpha));
        dl.addCircle({circX, circY}, radius,
                     BuGUI::Color(150, 200, 255, 180));

        const char* hint = "arrasta o slider para mudar a cor";
        float hw = dl.calcTextSize(font, hint).x;
        dl.addText(font,
                   {circX - hw * 0.5f, circY + radius + 8.0f + font.ascender()},
                   BuGUI::Color(120, 140, 170, 180), hint);

        prevMouse0_ = io.mouseDown[0];
    }

    void render() override
    {
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void shutdown() override
    {
    }

private:
    Device* device_     = nullptr;
    float   sliderVal_  = 50.0f;
    bool    sliderDrag_ = false;
    int     counter_    = 0;
    float   elapsed_    = 0.0f;
    bool    prevMouse0_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
