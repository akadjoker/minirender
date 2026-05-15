#pragma once

#include <Widget.hpp>
#include <Signal.hpp>
#include "RenderTarget.hpp"
#include "Material.hpp"   // for Texture::id

// ─────────────────────────────────────────────────────────────────────────────
//  ViewportWidget — BuGUI widget that displays an off-screen 3D render.
//
//  Lifecycle:
//    1. Add to a DockPanel / layout via createChild<ViewportWidget>()
//    2. In your render phase (BEFORE BuGUIEnd), check isReady() then:
//         vp->renderTarget()->bind();
//         ... draw 3D scene ...
//         vp->renderTarget()->unbind();
//    3. BuGUIEnd / paint() blits the FBO colour texture into the widget rect.
//
//  The RenderTarget is rebuilt automatically whenever the widget is resized.
//  Connect onResized to update your camera aspect ratio.
//
//  Note: GL FBO origin is bottom-left, so the UV is flipped on Y by default.
// ─────────────────────────────────────────────────────────────────────────────
class ViewportWidget : public BuGUI::Widget
{
public:
    ViewportWidget()           = default;
    ~ViewportWidget() override { delete rt_; }

    ViewportWidget(const ViewportWidget&)            = delete;
    ViewportWidget& operator=(const ViewportWidget&) = delete;

    // ── Virtuals ──────────────────────────────────────────────────────────
    void layout() override;
    void paint(BuGUI::PaintContext& ctx) override;

    // ── Accessors ─────────────────────────────────────────────────────────
    RenderTarget*         renderTarget() const { return rt_;      }
    BuGUI::TextureHandle  texHandle()    const { return texHandle_; }
    bool                  isReady()      const { return rt_ && rt_->valid(); }
    int                   rtWidth()      const { return rtW_;     }
    int                   rtHeight()     const { return rtH_;     }

    // ── Signals ───────────────────────────────────────────────────────────
    /// Fired after the RenderTarget is (re)created. w, h = new pixel size.
    BuGUI::Signal<int, int> onResized;

private:
    void rebuildRT(int w, int h);

    RenderTarget*        rt_        = nullptr;
    BuGUI::TextureHandle texHandle_ = {};
    int                  rtW_       = 0;
    int                  rtH_       = 0;
};
