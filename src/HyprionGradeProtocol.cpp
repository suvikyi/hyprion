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
        if ((requested & SUPPORTED_MODES) == 0) {
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

void CHyprionGrade::draw(Render::CRenderContext& ctx, PHLMONITOR monitor, const SP<Render::IFramebuffer>& snapshot) {
    if (!m_resource || !monitor || !m_enabled || !m_supported || m_width <= 0 || m_height <= 0 || m_areas == 0)
        return;

    const auto box = boxFor(monitor);
    if (box.width <= 0 || box.height <= 0)
        return;

    const auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
    const auto view = surface ? surface->view() : nullptr;
    const auto layer = view ? Desktop::View::CLayerSurface::fromView(view) : nullptr;
    if (!layer)
        return;

    const float alpha = layer->alpha()[Desktop::View::LS_ALPHA_FADE]->value();
    const auto  draw = [&](SHyprionGradeArea area, const CBox& areaBox, float thickness) {
        const auto mode = m_mode >= 0 ? static_cast<SHyprionGradeMode>(m_mode)
                                     : (area == SHyprionGradeArea::OUTSET ? SHyprionGradeMode::OVER : SHyprionGradeMode::IN_PLACE);

        auto&      renderData = ctx.m_data;
        const auto modificationScale = renderData.renderModif.combinedScale();
        const auto requestedThickness = std::round(std::round(static_cast<double>(thickness) * monitor->m_scale) * modificationScale);
        const int  thicknessPx = static_cast<int>(std::clamp(requestedThickness, 0.0, 4096.0));
        const auto baseRounding = std::max(0.0, static_cast<double>(m_radius) * monitor->m_scale - 1.0) * modificationScale;
        const float roundingPower = std::clamp(std::isfinite(m_roundingPower) ? m_roundingPower : 2.0F, 1.0F, 10.0F);
        if (!std::isfinite(baseRounding) || baseRounding < 0.0)
            return;

        CBox renderBox = areaBox;
        renderBox.scale(monitor->m_scale).round();
        renderData.renderModif.applyToBox(renderBox);
        if (renderBox.width <= 0.0 || renderBox.height <= 0.0 || !std::isfinite(renderBox.width) || !std::isfinite(renderBox.height))
            return;

        double rounding = baseRounding;
        double innerRounding = 0.0;
        if (area == SHyprionGradeArea::OUTSET) {
            const double correction = static_cast<double>(thicknessPx) * (std::sqrt(2.0) - 1.0) * std::max(2.0 - roundingPower, 0.0);
            rounding = baseRounding > 0.0 ? baseRounding + thicknessPx - correction : 0.0;
            innerRounding = baseRounding;
        } else if (area == SHyprionGradeArea::INSET) {
            innerRounding = std::max(baseRounding - thicknessPx, 0.0);
        }

        const double halfSize = std::min(renderBox.width, renderBox.height) * 0.5;
        const int    roundingPx = static_cast<int>(std::clamp(rounding, 0.0, halfSize));
        const int    innerRoundingPx = static_cast<int>(std::clamp(innerRounding, 0.0, halfSize));

        hyprionDrawGrade(ctx, monitor,
                         SHyprionGradeRequest{
                             .renderBox = renderBox,
                             .area = area,
                             .mode = mode,
                             .source = SHyprionGradeSource::CURRENT_FRAMEBUFFER,
                             .thickness = thicknessPx,
                             .rounding = roundingPx,
                             .innerRounding = innerRoundingPx,
                             .snapshotExtent = 0,
                             .roundingPower = roundingPower,
                             .style = m_style,
                             .orbit = {},
                             .cornerArcTable = {},
                             .alpha = alpha,
                             .fullSnapshot = true,
                             .snapshotOnly = false,
                             .snapshot = snapshot,
                             .clipToDamageBox = false,
                         });
    };

    if (m_areas & HYPRION_GRADE_V1_AREA_FILL)
        draw(SHyprionGradeArea::FILL, box, 0.F);
    if ((m_areas & HYPRION_GRADE_V1_AREA_OUTSET) && m_outset > 0)
        draw(SHyprionGradeArea::OUTSET, box.copy().expand(m_outset), static_cast<float>(m_outset));
    if ((m_areas & HYPRION_GRADE_V1_AREA_INSET) && m_inset > 0)
        draw(SHyprionGradeArea::INSET, box, static_cast<float>(m_inset));
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
    if (grade->m_surfacePtr) {
        const auto it = m_bySurface.find(grade->m_surfacePtr);
        if (it != m_bySurface.end()) {
            std::erase(it->second, grade);
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

    const auto grade = m_grades.emplace_back(makeUnique<CHyprionGrade>(makeShared<CHyprionGradeV1>(manager->client(), manager->version(), id), surface)).get();
    if (!grade->good()) {
        manager->noMemory();
        m_grades.pop_back();
        return;
    }
    m_bySurface[surface.get()].push_back(grade);
}

class CHyprionGradeGroupPassElement final : public IPassElement {
  public:
    CHyprionGradeGroupPassElement(std::vector<CHyprionGrade*> grades, PHLMONITOR monitor) : m_grades(std::move(grades)), m_monitor(monitor) {}

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override {
        SP<Render::IFramebuffer> snapshot;
        if (!hyprionCaptureGradeSnapshot(ctx, m_monitor, snapshot))
            return {};
        for (auto* grade : m_grades)
            if (grade)
                grade->draw(ctx, m_monitor, snapshot);
        return {};
    }

    bool needsLiveBlur(Render::CRenderContext&) override {
        return false;
    }

    bool needsPrecomputeBlur(Render::CRenderContext&) override {
        return false;
    }

    const char* passName() override {
        return "hyprion-grade-group";
    }

    ePassElementType type() override {
        return EK_CUSTOM;
    }

    std::optional<CBox> boundingBox(Render::CRenderContext&) override {
        return std::nullopt;
    }

    bool disableSimplification(Render::CRenderContext&) override {
        return true;
    }

  private:
    std::vector<CHyprionGrade*> m_grades;
    PHLMONITOR                  m_monitor;
};

void CHyprionGradeProtocol::queue(Render::CRenderContext& ctx, const PHLLS& layer, PHLMONITOR monitor) {
    if (!layer || !layer->mapped() || !monitor || !layer->wlSurface())
        return;
    const auto it = m_bySurface.find(layer->wlSurface()->resource().get());
    if (it == m_bySurface.end() || it->second.empty())
        return;
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionGradeGroupPassElement>(it->second, monitor));
}
