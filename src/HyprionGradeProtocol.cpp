#include "HyprionGradeProtocol.hpp"

#include <algorithm>
#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/View.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

namespace {
    constexpr uint32_t SUPPORTED_AREAS   = HYPRION_GRADE_V1_AREA_INSET;
    constexpr uint32_t SUPPORTED_SOURCES = HYPRION_GRADE_V1_SOURCE_FILL;

    SHyprionStyle styleFromWire(wl_fixed_t lightness, wl_fixed_t saturation, wl_fixed_t chroma, wl_fixed_t hue, wl_fixed_t strength, wl_fixed_t opacity,
                                uint32_t tint, wl_fixed_t tintStrength) {
        SHyprionStyle style;
        style.lightness = static_cast<float>(wl_fixed_to_double(lightness));
        style.saturation = static_cast<float>(wl_fixed_to_double(saturation));
        style.chroma = static_cast<float>(wl_fixed_to_double(chroma));
        style.hue = static_cast<float>(wl_fixed_to_double(hue));
        style.strength = static_cast<float>(wl_fixed_to_double(strength));
        style.opacity = static_cast<float>(wl_fixed_to_double(opacity));
        style.tintColor = {
            static_cast<float>((tint >> 16) & 0xFF) / 255.F,
            static_cast<float>((tint >> 8) & 0xFF) / 255.F,
            static_cast<float>(tint & 0xFF) / 255.F,
        };
        style.tintStrength = static_cast<float>(wl_fixed_to_double(tintStrength));
        return style;
    }
}

CHyprionGrade::CHyprionGrade(SP<CHyprionGradeV1> resource, WP<CWLSurfaceResource> surface) : m_surface(surface) {
    setResource(std::move(resource));
}

CHyprionGrade::~CHyprionGrade() {
    m_surfaceDestroyed.reset();
    m_surfaceCommitted.reset();
    if (m_resource) {
        m_resource->setOnDestroy([](CHyprionGradeV1*) {});
        m_resource->setDestroy([](CHyprionGradeV1*) {});
        m_resource.reset();
    }
}

bool CHyprionGrade::good() const {
    return m_resource && m_resource->resource();
}

void CHyprionGrade::setResource(SP<CHyprionGradeV1> resource) {
    m_resource = std::move(resource);
    if (!m_resource || !m_resource->resource())
        return;

    m_resource->setDestroy([this](CHyprionGradeV1*) { destroy(); });
    m_resource->setOnDestroy([this](CHyprionGradeV1*) { destroy(); });

    m_resource->setSetEnabled([this](CHyprionGradeV1*, uint32_t enabled) {
        m_enabled = enabled != 0;
        damage();
    });

    m_resource->setSetBox([this](CHyprionGradeV1*, int32_t x, int32_t y, int32_t width, int32_t height, wl_fixed_t radius, wl_fixed_t roundingPower) {
        if (width <= 0 || height <= 0 || !std::isfinite(wl_fixed_to_double(radius)) || !std::isfinite(wl_fixed_to_double(roundingPower))) {
            m_resource->error(HYPRION_GRADE_V1_ERROR_INVALID_GEOMETRY, "invalid grade box");
            return;
        }

        m_x = x;
        m_y = y;
        m_width = width;
        m_height = height;
        m_radius = std::max(0.F, static_cast<float>(wl_fixed_to_double(radius)));
        m_roundingPower = std::clamp(static_cast<float>(wl_fixed_to_double(roundingPower)), 1.F, 10.F);
        damage();
    });

    m_resource->setSetArea([this](CHyprionGradeV1*, hyprionGradeV1Area areas, int32_t inset, int32_t outset) {
        const uint32_t requested = static_cast<uint32_t>(areas);
        if (!(requested & SUPPORTED_AREAS)) {
            m_resource->error(HYPRION_GRADE_V1_ERROR_UNSUPPORTED, "unsupported grade area");
            return;
        }
        m_areas = requested & SUPPORTED_AREAS;
        m_inset = std::max(0, inset);
        m_outset = std::max(0, outset);
        damage();
    });

    m_resource->setSetSource([this](CHyprionGradeV1*, hyprionGradeV1Source sources) {
        const uint32_t requested = static_cast<uint32_t>(sources);
        if (!(requested & SUPPORTED_SOURCES)) {
            m_resource->error(HYPRION_GRADE_V1_ERROR_UNSUPPORTED, "unsupported grade source");
            return;
        }
        m_sources = requested & SUPPORTED_SOURCES;
        damage();
    });

    m_resource->setSetStyle([this](CHyprionGradeV1*, wl_fixed_t lightness, wl_fixed_t saturation, wl_fixed_t chroma, wl_fixed_t hue, wl_fixed_t strength,
                                   wl_fixed_t opacity, uint32_t tint, wl_fixed_t tintStrength) {
        m_style = styleFromWire(lightness, saturation, chroma, hue, strength, opacity, tint, tintStrength);
        damage();
    });

    if (auto surface = m_surface.lock()) {
        m_surfaceDestroyed = surface->m_events.destroy.listen([this] {
            if (Hyprion::gradeProtocol)
                Hyprion::gradeProtocol->destroyGrade(this);
        });
        m_surfaceCommitted = surface->m_events.commit.listen([this] { updateSupport(); });
        updateSupport();
    }
}

void CHyprionGrade::destroy() {
    if (Hyprion::gradeProtocol)
        Hyprion::gradeProtocol->destroyGrade(this);
}

CBox CHyprionGrade::surfaceLocalBox() const {
    const auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
    if (!surface)
        return {0, 0, 0, 0};

    const auto surfaceBox = surface->getSurfaceBoxGlobal();
    if (!surfaceBox)
        return {0, 0, 0, 0};

    CBox box{surfaceBox->pos() + Vector2D{m_x, m_y}, Vector2D{m_width, m_height}};
    return box.intersection(*surfaceBox);
}

void CHyprionGrade::damage() {
    if (!g_pHyprRenderer)
        return;

    auto box = surfaceLocalBox();
    if (box.width <= 0 || box.height <= 0)
        return;

    box.expand(m_radius + m_inset + m_outset + 2.F);
    g_pHyprRenderer->damageBox(box);
}

void CHyprionGrade::updateSupport() {
    if (!m_resource)
        return;

    const auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
    if (!surface)
        return;

    const auto view = surface->view();
    if (!view)
        return;

    const bool supported = view->type() == Desktop::View::VIEW_TYPE_LAYER_SURFACE;
    if (m_supportSent && supported == m_supported)
        return;

    m_supported = supported;
    m_supportSent = true;
    m_resource->sendSupported(supported ? 1 : 0);
}

void CHyprionGrade::draw(Render::CRenderContext& ctx, PHLMONITOR monitor) {
    if (!m_resource || !monitor || !m_enabled || !m_supported || m_width <= 0 || m_height <= 0 || m_inset <= 0)
        return;
    if (!(m_areas & HYPRION_GRADE_V1_AREA_INSET) || !(m_sources & HYPRION_GRADE_V1_SOURCE_FILL))
        return;

    const auto box = boxFor(monitor);
    if (box.width <= 0 || box.height <= 0)
        return;

    const auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
    const auto view = surface ? surface->view() : nullptr;
    const auto layer = view ? Desktop::View::CLayerSurface::fromView(view) : nullptr;
    if (!layer)
        return;

    SHyprionGradeParams params{
        .box = box,
        .thickness = static_cast<float>(m_inset),
        .radius = m_radius,
        .roundingPower = m_roundingPower,
        .style = m_style,
        .alpha = layer->alpha()[Desktop::View::LS_ALPHA_FADE]->value(),
    };
    hyprionDrawGrade(ctx, monitor, params);
}

bool CHyprionGrade::matches(const PHLLS& layer) const {
    const auto surface = m_surface.lock();
    return surface && layer && layer->wlSurface() && layer->wlSurface()->resource() == surface;
}

CBox CHyprionGrade::boxFor(PHLMONITOR monitor) const {
    if (!monitor)
        return {0, 0, 0, 0};

    CBox box = surfaceLocalBox();
    if (box.width <= 0 || box.height <= 0)
        return {0, 0, 0, 0};

    box.translate(-monitor->m_position);
    if (box.intersection(monitor->logicalBox()).empty())
        return {0, 0, 0, 0};
    return box;
}

class CHyprionGradePassElement final : public IPassElement {
  public:
    CHyprionGradePassElement(CHyprionGrade* grade, PHLMONITOR monitor) : m_grade(grade), m_monitor(monitor) {}

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override {
        if (m_grade)
            m_grade->draw(ctx, m_monitor);
        return {};
    }

    bool needsLiveBlur(Render::CRenderContext&) override {
        return false;
    }

    bool needsPrecomputeBlur(Render::CRenderContext&) override {
        return false;
    }

    const char* passName() override {
        return "hyprion-grade";
    }

    ePassElementType type() override {
        return EK_CUSTOM;
    }

    std::optional<CBox> boundingBox(Render::CRenderContext&) override {
        if (!m_grade)
            return std::nullopt;
        const auto box = m_grade->boxFor(m_monitor);
        if (box.width <= 0 || box.height <= 0)
            return std::nullopt;
        return box;
    }

    bool disableSimplification(Render::CRenderContext&) override {
        return true;
    }

  private:
    CHyprionGrade* m_grade = nullptr;
    PHLMONITOR     m_monitor;
};

CHyprionGradeProtocol::CHyprionGradeProtocol() : IWaylandProtocol(&hyprion_grade_manager_v1_interface, 1, "HyprionGrade") {
    ;
}

CHyprionGradeProtocol::~CHyprionGradeProtocol() {
    for (auto& manager : m_managers) {
        manager->setDestroy([](CHyprionGradeManagerV1*) {});
        manager->setOnDestroy([](CHyprionGradeManagerV1*) {});
    }
}

void CHyprionGradeProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto manager = m_managers.emplace_back(makeUnique<CHyprionGradeManagerV1>(client, ver, id)).get();
    manager->setDestroy([this, manager](CHyprionGradeManagerV1*) { destroyManager(manager); });
    manager->setOnDestroy([this, manager](CHyprionGradeManagerV1*) { destroyManager(manager); });
    manager->setGetGrade([this](CHyprionGradeManagerV1* manager, uint32_t id, wl_resource* surface) { getGrade(manager, id, surface); });
    manager->sendCapabilities(SUPPORTED_AREAS, SUPPORTED_SOURCES);
}

void CHyprionGradeProtocol::destroyManager(CHyprionGradeManagerV1* manager) {
    std::erase_if(m_managers, [manager](const auto& other) { return other.get() == manager; });
}

void CHyprionGradeProtocol::destroyGrade(CHyprionGrade* grade) {
    std::erase_if(m_grades, [grade](const auto& other) { return other.get() == grade; });
}

void CHyprionGradeProtocol::getGrade(CHyprionGradeManagerV1* manager, uint32_t id, wl_resource* surfaceResource) {
    const auto surface = CWLSurfaceResource::fromResource(surfaceResource);
    if (!surface) {
        manager->error(HYPRION_GRADE_V1_ERROR_SURFACE_DESTROYED, "invalid wl_surface");
        return;
    }

    const auto grade = m_grades.emplace_back(makeUnique<CHyprionGrade>(makeShared<CHyprionGradeV1>(manager->client(), manager->version(), id), surface)).get();
    if (!grade->good()) {
        manager->noMemory();
        m_grades.pop_back();
    }
}

void CHyprionGradeProtocol::queue(Render::CRenderContext& ctx, const PHLLS& layer, PHLMONITOR monitor) {
    if (!layer || !layer->mapped() || !monitor)
        return;
    for (const auto& grade : m_grades) {
        if (grade->matches(layer))
            g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionGradePassElement>(grade.get(), monitor));
    }
}
