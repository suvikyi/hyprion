#include "HyprionGradeProtocol.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/View.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>

namespace {
    constexpr uint32_t SUPPORTED_AREAS = HYPRION_GRADE_V1_AREA_INSET | HYPRION_GRADE_V1_AREA_OUTSET | HYPRION_GRADE_V1_AREA_FILL;
    constexpr uint32_t SUPPORTED_MODES = HYPRION_GRADE_V1_MODE_IN_PLACE | HYPRION_GRADE_V1_MODE_OVER;

    SHyprionStyle styleFromWire(wl_fixed_t lightness, wl_fixed_t saturation, wl_fixed_t chroma, wl_fixed_t hue, wl_fixed_t opacity,
                                wl_fixed_t colorizeStrength) {
        SHyprionStyle style;
        style.grade.lightness = static_cast<float>(wl_fixed_to_double(lightness));
        style.grade.saturation = static_cast<float>(wl_fixed_to_double(saturation));
        style.grade.chroma = static_cast<float>(wl_fixed_to_double(chroma));
        style.grade.hue = static_cast<float>(wl_fixed_to_double(hue));
        style.opacity = static_cast<float>(wl_fixed_to_double(opacity));
        style.grade.colorize.strength = static_cast<float>(wl_fixed_to_double(colorizeStrength));
        return style;
    }

    CBox expandBox(const CBox& box, double amount) {
        return CBox{box.x - amount, box.y - amount, box.width + 2.0 * amount, box.height + 2.0 * amount};
    }
}

CHyprionGrade::CHyprionGrade(SP<CHyprionGradeV1> resource, WP<CWLSurfaceResource> surface) : m_surface(surface), m_surfacePtr(surface.get()) {
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
        if (requested == 0 || (requested & ~SUPPORTED_AREAS) != 0) {
            m_resource->error(HYPRION_GRADE_V1_ERROR_UNSUPPORTED, "unsupported grade area");
            return;
        }
        m_areas = requested;
        m_inset = std::max(0, inset);
        m_outset = std::max(0, outset);
        damage();
    });

    m_resource->setSetMode([this](CHyprionGradeV1*, hyprionGradeV1Mode mode) {
        const uint32_t requested = static_cast<uint32_t>(mode);
        if (requested != HYPRION_GRADE_V1_MODE_IN_PLACE && requested != HYPRION_GRADE_V1_MODE_OVER) {
            m_resource->error(HYPRION_GRADE_V1_ERROR_UNSUPPORTED, "unsupported grade mode");
            return;
        }
        m_mode = static_cast<int>(requested);
        damage();
    });

    m_resource->setSetStyle([this](CHyprionGradeV1*, wl_fixed_t lightness, wl_fixed_t saturation, wl_fixed_t chroma, wl_fixed_t hue, wl_fixed_t opacity,
                                   wl_fixed_t colorizeStrength) {
        auto style = styleFromWire(lightness, saturation, chroma, hue, opacity, colorizeStrength);
        style.grade.colorize.gradient = m_style.grade.colorize.gradient;
        m_style = style;
        damage();
    });

    m_resource->setSetColorizeGradient([this](CHyprionGradeV1*, wl_array* colors, wl_fixed_t angle) {
        if (!colors || colors->size == 0 || colors->size % sizeof(uint32_t) != 0 || colors->size / sizeof(uint32_t) > 10) {
            m_resource->error(HYPRION_GRADE_V1_ERROR_INVALID_GRADIENT, "invalid colorize gradient");
            return;
        }
        const auto count = colors->size / sizeof(uint32_t);
        const auto* data = static_cast<const uint32_t*>(colors->data);
        m_style.grade.colorize.gradient.colors.clear();
        m_style.grade.colorize.gradient.colors.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            m_style.grade.colorize.gradient.colors.push_back({
                static_cast<float>((data[i] >> 16) & 0xFF) / 255.F,
                static_cast<float>((data[i] >> 8) & 0xFF) / 255.F,
                static_cast<float>(data[i] & 0xFF) / 255.F,
                1.F,
            });
        }
        m_style.grade.colorize.gradient.angle = static_cast<float>(wl_fixed_to_double(angle) * std::numbers::pi / 180.0);
        damage();
    });

    if (auto surface = m_surface.lock()) {
        m_surfaceDestroyed = surface->m_events.destroy.listen([this] {
            if (Hyprion::gradeProtocol)
                Hyprion::gradeProtocol->destroyGrade(this);
        });
        m_surfaceCommitted = surface->m_events.commit.listen([this] {
            updateSupport();
            damage(false);
        });
        updateSupport();
    }
}

void CHyprionGrade::destroy() {
    if (Hyprion::gradeProtocol)
        Hyprion::gradeProtocol->destroyGrade(this);
}


CBox CHyprionGrade::damageBox() const {
    if (!m_enabled || !m_supported || m_width <= 0 || m_height <= 0)
        return {};
    const auto resource = m_surface.lock();
    const auto surface = Desktop::View::CWLSurface::fromResource(resource);
    const auto surfaceBox = surface ? surface->getSurfaceBoxGlobal() : std::nullopt;
    if (!surfaceBox || !resource || resource->m_current.size.x <= 0 || resource->m_current.size.y <= 0)
        return {};
    const auto scale = surfaceBox->size() / resource->m_current.size;
    CBox box{surfaceBox->pos() + Vector2D{m_x * scale.x, m_y * scale.y}, Vector2D{m_width * scale.x, m_height * scale.y}};
    if (m_areas & HYPRION_GRADE_V1_AREA_OUTSET)
        box = expandBox(box, m_outset * std::max(scale.x, scale.y));
    return expandBox(box, 1.0).intersection(*surfaceBox);
}

void CHyprionGrade::damage(bool force) {
    const auto next = damageBox();
    if (!force && next == m_lastDamageBox)
        return;
    if (g_pHyprRenderer) {
        if (!m_lastDamageBox.empty())
            g_pHyprRenderer->damageBox(m_lastDamageBox);
        if (!next.empty())
            g_pHyprRenderer->damageBox(next);
    }
    m_lastDamageBox = next;
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

    const auto type = view->type();
    const bool supported = type == Desktop::View::VIEW_TYPE_LAYER_SURFACE || type == Desktop::View::VIEW_TYPE_WINDOW ||
        type == Desktop::View::VIEW_TYPE_SUBSURFACE || type == Desktop::View::VIEW_TYPE_POPUP;
    if (m_supportSent && supported == m_supported)
        return;

    m_supported = supported;
    m_supportSent = true;
    m_resource->sendSupported(supported ? 1 : 0);
}

void CHyprionGrade::appendRequests(Render::CRenderContext& ctx, PHLMONITOR monitor, const CBox& surfaceBox, const CBox& clipBox, float alpha,
                                  std::vector<SHyprionGradeRequest>& requests) const {
    const auto surface = m_surface.lock();
    if (!surface || !m_enabled || !m_supported || m_width <= 0 || m_height <= 0 || surface->m_current.size.x <= 0 || surface->m_current.size.y <= 0)
        return;

    const auto scale = surfaceBox.size() / surface->m_current.size;
    const double shapeScale = std::min(scale.x, scale.y);
    const CBox box{surfaceBox.pos() + Vector2D{m_x * scale.x, m_y * scale.y}, Vector2D{m_width * scale.x, m_height * scale.y}};
    CBox surfaceClip = surfaceBox.copy().scale(monitor->m_scale).round();
    if (!clipBox.empty())
        surfaceClip = surfaceClip.intersection(clipBox);
    ctx.m_data.renderModif.applyToBox(surfaceClip);
    if (surfaceClip.empty())
        return;

    const auto draw = [&](SHyprionGradeArea area, double thickness) {
        const auto mode = m_mode >= 0 ? static_cast<SHyprionGradeMode>(m_mode)
                                     : (area == SHyprionGradeArea::OUTSET ? SHyprionGradeMode::OVER : SHyprionGradeMode::IN_PLACE);
        const double modificationScale = ctx.m_data.renderModif.combinedScale();
        const double pixelScale = monitor->m_scale * modificationScale;
        const int thicknessPx = static_cast<int>(std::clamp(std::round(thickness * pixelScale), 0.0, 4096.0));
        if (area != SHyprionGradeArea::FILL && thicknessPx == 0)
            return;
        const double baseRounding = std::max(0.0, m_radius * shapeScale * monitor->m_scale - 1.0) * modificationScale;
        CBox renderBox = box.copy().scale(monitor->m_scale).round();
        ctx.m_data.renderModif.applyToBox(renderBox);
        // Expand by the same integer thickness that the shader uses. This keeps
        // fractional-scale coverage, source copies and clipping in agreement.
        if (area == SHyprionGradeArea::OUTSET)
            renderBox = expandBox(renderBox, thicknessPx);
        if (renderBox.empty())
            return;
        double rounding = baseRounding;
        double innerRounding = 0.0;
        if (area == SHyprionGradeArea::OUTSET) {
            const double correction = thicknessPx * (std::sqrt(2.0) - 1.0) * std::max(2.0 - m_roundingPower, 0.0);
            rounding = baseRounding > 0.0 ? baseRounding + thicknessPx - correction : 0.0;
            innerRounding = baseRounding;
        } else if (area == SHyprionGradeArea::INSET)
            innerRounding = std::max(baseRounding - thicknessPx, 0.0);
        const double halfSize = std::min(renderBox.width, renderBox.height) * 0.5;
        requests.push_back(SHyprionGradeRequest{
            .renderBox = renderBox,
            .area = area,
            .mode = mode,
            .thickness = thicknessPx,
            .rounding = static_cast<int>(std::clamp(rounding, 0.0, halfSize)),
            .innerRounding = static_cast<int>(std::clamp(innerRounding, 0.0, halfSize)),
            .roundingPower = m_roundingPower,
            .style = m_style,
            .alpha = alpha,
            .fullSnapshot = true,
            .clipBox = surfaceClip,
        });
    };
    if (m_areas & HYPRION_GRADE_V1_AREA_FILL)
        draw(SHyprionGradeArea::FILL, 0.0);
    if ((m_areas & HYPRION_GRADE_V1_AREA_OUTSET) && m_outset > 0)
        draw(SHyprionGradeArea::OUTSET, m_outset * shapeScale);
    if ((m_areas & HYPRION_GRADE_V1_AREA_INSET) && m_inset > 0)
        draw(SHyprionGradeArea::INSET, m_inset * shapeScale);
}

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
    manager->sendCapabilities(SUPPORTED_AREAS, SUPPORTED_MODES);
}

void CHyprionGradeProtocol::destroyManager(CHyprionGradeManagerV1* manager) {
    std::erase_if(m_managers, [manager](const auto& other) { return other.get() == manager; });
}

void CHyprionGradeProtocol::destroyGrade(CHyprionGrade* grade) {
    if (!grade)
        return;
    grade->m_enabled = false;
    grade->damage();
    if (grade->m_surfacePtr) {
        const auto it = m_bySurface.find(grade->m_surfacePtr);
        if (it != m_bySurface.end()) {
            std::erase_if(it->second, [grade](const auto& other) { return other.get() == grade; });
            if (it->second.empty())
                m_bySurface.erase(it);
        }
    }
    std::erase_if(m_grades, [grade](const auto& other) { return other.get() == grade; });
}


void CHyprionGradeProtocol::getGrade(CHyprionGradeManagerV1* manager, uint32_t id, wl_resource* surfaceResource) {
    const auto surface = CWLSurfaceResource::fromResource(surfaceResource);
    if (!surface) {
        manager->error(HYPRION_GRADE_V1_ERROR_SURFACE_DESTROYED, "invalid wl_surface");
        return;
    }

    const auto grade = m_grades.emplace_back(makeShared<CHyprionGrade>(makeShared<CHyprionGradeV1>(manager->client(), manager->version(), id), surface));
    if (!grade->good()) {
        manager->noMemory();
        m_grades.pop_back();
        return;
    }
    m_bySurface[surface.get()].push_back(grade);
}

class CHyprionGradeGroupPassElement final : public IPassElement {
  public:
    CHyprionGradeGroupPassElement(std::vector<SP<CHyprionGrade>> grades, CSurfacePassElement& surface) :
        m_grades(std::move(grades)), m_monitor(surface.m_data.pMonitor), m_surfaceBox(surface.getTexBox()), m_clipBox(surface.m_data.clipBox),
        m_alpha(surface.m_data.alpha * surface.m_data.fadeAlpha) {
        if (const auto wlSurface = Desktop::View::CWLSurface::fromResource(surface.m_data.surface))
            m_alpha *= wlSurface->m_alphaModifier * wlSurface->m_overallOpacity;
    }

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override {
        const auto monitor = m_monitor.lock();
        if (!monitor || m_alpha <= 0.F)
            return {};
        std::vector<SHyprionGradeRequest> requests;
        requests.reserve(m_grades.size());
        for (const auto& grade : m_grades)
            grade->appendRequests(ctx, monitor, m_surfaceBox, m_clipBox, m_alpha, requests);

        SHyprionGradeSnapshot snapshot;
        for (std::size_t begin = 0; begin < requests.size();) {
            CRegion sources;
            std::size_t end = begin;
            for (; end < requests.size(); ++end) {
                auto region = hyprionGradeRegion(requests[end]).intersect(ctx.m_data.damage);
                // A later overlapping grade must see the result of every earlier
                // grade. Disjoint requests can safely share an immutable capture.
                if (!sources.copy().intersect(region).empty())
                    break;
                sources.add(region);
            }
            if (!sources.empty() && hyprionCaptureGradeSnapshot(ctx, monitor, snapshot, hyprionGradeFramebufferRegion(ctx, sources))) {
                for (auto i = begin; i < end; ++i) {
                    requests[i].snapshot = &snapshot;
                    hyprionDrawGrade(ctx, monitor, requests[i]);
                }
            }
            begin = end;
        }
        return {};
    }

    bool needsLiveBlur(Render::CRenderContext&) override { return false; }
    bool needsPrecomputeBlur(Render::CRenderContext&) override { return false; }
    const char* passName() override { return "hyprion-grade"; }
    ePassElementType type() override { return EK_CUSTOM; }
    std::optional<CBox> boundingBox(Render::CRenderContext&) override { return m_surfaceBox; }

  private:
    std::vector<SP<CHyprionGrade>> m_grades;
    PHLMONITORREF m_monitor;
    CBox m_surfaceBox, m_clipBox;
    float m_alpha;
};

void CHyprionGradeProtocol::queue(Render::CRenderPass& pass, CSurfacePassElement& surface) {
    const auto it = m_bySurface.find(surface.m_data.surface.get());
    if (it == m_bySurface.end() || it->second.empty())
        return;
    pass.add(makeUnique<CHyprionGradeGroupPassElement>(it->second, surface));
}
