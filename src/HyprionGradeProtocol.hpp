#pragma once

#include "HyprionGradeRenderer.hpp"

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/protocols/WaylandProtocol.hpp>

#include <unordered_map>
#include <unordered_set>

#include "hyprion-grade-v1.hpp"

class CHyprionGradeManagerV1;
class CHyprionGradeV1;
class CWLSurfaceResource;

class CHyprionGrade {
  public:
    CHyprionGrade(SP<CHyprionGradeV1> resource, WP<CWLSurfaceResource> surface);
    ~CHyprionGrade();

    bool good() const;
    void draw(Render::CRenderContext& ctx, PHLMONITOR monitor, const SP<Render::IFramebuffer>& snapshot);
    CBox boxFor(PHLMONITOR monitor) const;
    CBox snapshotBounds(Render::CRenderContext& ctx, PHLMONITOR monitor) const;
    PHLMONITOR monitor() const;

  private:
    void setResource(SP<CHyprionGradeV1> resource);
    void destroy();
    void damage();
    void damageSourceBand(PHLMONITOR monitor);
    void updateSupport();
    CBox surfaceLocalBox() const;

    SP<CHyprionGradeV1>    m_resource;
    WP<CWLSurfaceResource> m_surface;
    CWLSurfaceResource*    m_surfacePtr = nullptr;

    int   m_x = 0;
    int   m_y = 0;
    int   m_width = 0;
    int   m_height = 0;
    float m_radius = 0.F;
    float m_roundingPower = 2.F;
    int   m_inset = 0;
    int   m_outset = 0;

    SHyprionStyle m_style;
    uint32_t      m_areas = HYPRION_GRADE_V1_AREA_INSET;
    bool          m_enabled = true;
    int           m_mode = -1; // auto: over for outset, in_place otherwise
    bool          m_supported = false;
    bool          m_supportSent = false;

    CHyprSignalListener m_surfaceDestroyed;
    CHyprSignalListener m_surfaceCommitted;

    friend class CHyprionGradeProtocol;
};

class CHyprionGradeProtocol : public IWaylandProtocol {
  public:
    CHyprionGradeProtocol();
    ~CHyprionGradeProtocol() override;

    void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) override;
    void queue(Render::CRenderContext& ctx, const PHLLS& layer, PHLMONITOR monitor);
    void damageDirtyBands(PHLMONITOR monitor);

  private:
    struct SSourceListener {
        CHyprSignalListener commit;
        CHyprSignalListener destroy;
    };

    void destroyManager(CHyprionGradeManagerV1* manager);
    void destroyGrade(CHyprionGrade* grade);
    void getGrade(CHyprionGradeManagerV1* manager, uint32_t id, wl_resource* surface);
    void markSourceDirty(PHLMONITOR monitor);
    void syncSourceListeners(PHLMONITOR monitor);

    std::vector<UP<CHyprionGradeManagerV1>> m_managers;
    std::vector<UP<CHyprionGrade>>          m_grades;
    std::unordered_map<CWLSurfaceResource*, std::vector<CHyprionGrade*>> m_bySurface;
    std::unordered_map<CWLSurfaceResource*, SSourceListener>              m_sourceListeners;
    std::unordered_set<uint64_t>                                         m_dirtyMonitors;

    friend class CHyprionGrade;
};

namespace Hyprion {
    inline UP<CHyprionGradeProtocol> gradeProtocol;
}
