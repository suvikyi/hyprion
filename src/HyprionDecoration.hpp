#pragma once

#include "HyprionAnimation.hpp"

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

class CHyprionPassElement;

class CHyprionDecoration final : public IHyprWindowDecoration {
  public:
    explicit CHyprionDecoration(PHLWINDOW window);
    ~CHyprionDecoration() override;

    SDecorationPositioningInfo getPositioningInfo() override;
    void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                       draw(Render::CRenderContext& ctx, PHLMONITOR monitor, float const& alpha, const Render::SWindowRenderPresentation& presentation) override;
    void                       drawOutset(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha, const Render::SWindowRenderPresentation& presentation);
    eDecorationType            getDecorationType() override;
    void                       updateWindow(PHLWINDOW window) override;
    void                       damageEntire() override;
    eDecorationLayer           getDecorationLayer() override;
    uint64_t                   getDecorationFlags() override;
    std::string                getDisplayName() override;

    void stateChanged(bool active, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now);
    void refreshAnimation(bool active, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now);
    bool animate(const SHyprionConfig& config, CHyprionAnimation::Time now);
    void resetAnimation(bool active, bool urgent, CHyprionAnimation::Time now);

  private:
    CBox monitorLocalBox(PHLMONITOR monitor, const Render::SWindowRenderPresentation& presentation) const;
    void  drawPass(Render::CRenderContext& ctx, float alpha, const CBox& monitorLocalBox, bool outset, bool snapshotOnly);
    void  damageBorder();
    void  updateOrbitArcTable(float power);

    PHLWINDOWREF m_window;
    std::array<CHyprionAnimation, 2> m_animations;
    int m_outsetExtent = 0;
    bool m_outsetSnapshotReady = false;
    std::array<float, 65> m_orbitArcTable{};
    float m_orbitArcPower = -1.F;

    friend class CHyprionPassElement;
};
