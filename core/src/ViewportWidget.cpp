#include "ViewportWidget.hpp"

// ─── layout ──────────────────────────────────────────────────────────────────
void ViewportWidget::layout()
{
    BuGUI::Widget::layout();

    const int w = static_cast<int>(rect_.w);
    const int h = static_cast<int>(rect_.h);

    if (w < 1 || h < 1)
        return;

    if (w != rtW_ || h != rtH_)
        rebuildRT(w, h);
}

// ─── paint ───────────────────────────────────────────────────────────────────
void ViewportWidget::paint(BuGUI::PaintContext& ctx)
{
    if (!texHandle_)
        return;

    // GL FBO origin is bottom-left; flip UV on Y so the image appears correct.
    const BuGUI::Rect uvFlipped = {0.0f, 1.0f, 1.0f, -1.0f};
    ctx.drawImage(texHandle_, rect_, uvFlipped);
}

// ─── rebuildRT ───────────────────────────────────────────────────────────────
void ViewportWidget::rebuildRT(int w, int h)
{
    delete rt_;
    rt_        = nullptr;
    texHandle_ = {};
    rtW_       = 0;
    rtH_       = 0;

    rt_ = new RenderTarget();
    if (!rt_->create(w, h)       ||
        !rt_->addColorAttachment() ||
        !rt_->addDepthAttachment() ||
        !rt_->finalize())
    {
        delete rt_;
        rt_ = nullptr;
        return;
    }

    rtW_       = w;
    rtH_       = h;
    texHandle_ = BuGUI::TextureHandle{static_cast<uintptr_t>(rt_->colorTex()->id)};

    onResized.emit(w, h);
}
