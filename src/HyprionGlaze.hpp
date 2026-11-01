#pragma once

#include "HyprionShader.hpp"

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/protocols/WaylandProtocol.hpp>

#include "hyprion-glaze-v1.hpp"

class CHyprionGlazeManagerV1;
class CHyprionGlazeV1;
class CWLSurfaceResource;

class CHyprionGlaze {
  public:
    CHyprionGlaze(SP<CHyprionGlazeV1> resource, WP<CWLSurfaceResource> surface);
    ~CHyprionGlaze();

    bool good() const;
    void draw(Render::CRenderContext& ctx, PHLMONITOR monitor);
    bool matches(const PHLLS& layer) const;
    CBox boxFor(PHLMONITOR monitor) const;

  private:
    void setResource(SP<CHyprionGlazeV1> resource);
    void destroy();
    void damage();

    SP<CHyprionGlazeV1>    m_resource;
    WP<CWLSurfaceResource> m_surface;

    int   m_x = 0;
    int   m_y = 0;
    int   m_width = 0;
    int   m_height = 0;
    float m_radius = 0.F;
    float m_roundingPower = 2.F;
    int   m_inset = 0;
    int   m_outset = 0;

    SHyprionStyle m_style;
    uint32_t      m_sources = HYPRION_GLAZE_V1_SOURCE_INSET_FILL;

    CHyprSignalListener m_surfaceDestroyed;

    friend class CHyprionGlazeProtocol;
};

class CHyprionGlazeProtocol : public IWaylandProtocol {
  public:
    CHyprionGlazeProtocol();

    void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) override;
    void queue(Render::CRenderContext& ctx, const PHLLS& layer, PHLMONITOR monitor);

  private:
    void destroyManager(CHyprionGlazeManagerV1* manager);
    void destroyGlaze(CHyprionGlaze* glaze);
    void getGlaze(CHyprionGlazeManagerV1* manager, uint32_t id, wl_resource* surface);

    std::vector<UP<CHyprionGlazeManagerV1>> m_managers;
    std::vector<UP<CHyprionGlaze>>          m_glazes;

    friend class CHyprionGlaze;
};

namespace Hyprion {
    inline UP<CHyprionGlazeProtocol> glazeProtocol;
}
