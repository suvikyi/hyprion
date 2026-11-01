#include "HyprionGlaze.hpp"

#include <algorithm>
#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

namespace {
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

CHyprionGlaze::CHyprionGlaze(SP<CHyprionGlazeV1> resource, WP<CWLSurfaceResource> surface) : m_surface(surface) {
    setResource(std::move(resource));
}

CHyprionGlaze::~CHyprionGlaze() {
    m_surfaceDestroyed.reset();
    if (m_resource) {
        m_resource->setOnDestroy([](CHyprionGlazeV1*) {});
        m_resource->setDestroy([](CHyprionGlazeV1*) {});
        m_resource.reset();
    }
}

bool CHyprionGlaze::good() const {
    return m_resource && m_resource->resource();
}

void CHyprionGlaze::setResource(SP<CHyprionGlazeV1> resource) {
    m_resource = std::move(resource);
    if (!m_resource || !m_resource->resource())
        return;

    m_resource->setDestroy([this](CHyprionGlazeV1*) { destroy(); });
    m_resource->setOnDestroy([this](CHyprionGlazeV1*) { destroy(); });

    m_resource->setSetBox([this](CHyprionGlazeV1*, int32_t x, int32_t y, int32_t width, int32_t height, wl_fixed_t radius, wl_fixed_t roundingPower) {
        if (width <= 0 || height <= 0 || !std::isfinite(wl_fixed_to_double(radius)) || !std::isfinite(wl_fixed_to_double(roundingPower))) {
            m_resource->error(HYPRION_GLAZE_V1_ERROR_INVALID_GEOMETRY, "invalid glaze box");
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

    m_resource->setSetBands([this](CHyprionGlazeV1*, int32_t inset, int32_t outset) {
        m_inset = std::max(0, inset);
        m_outset = std::max(0, outset);
        damage();
    });

    m_resource->setSetStyle([this](CHyprionGlazeV1*, wl_fixed_t lightness, wl_fixed_t saturation, wl_fixed_t chroma, wl_fixed_t hue, wl_fixed_t strength,
                                   wl_fixed_t opacity, uint32_t tint, wl_fixed_t tintStrength) {
        m_style = styleFromWire(lightness, saturation, chroma, hue, strength, opacity, tint, tintStrength);
        damage();
    });

    m_resource->setSetSource([this](CHyprionGlazeV1*, hyprionGlazeV1Source sources) {
        m_sources = sources;
        damage();
    });

    if (auto surface = m_surface.lock()) {
        m_surfaceDestroyed = surface->m_events.destroy.listen([this] {
            if (Hyprion::glazeProtocol)
                Hyprion::glazeProtocol->destroyGlaze(this);
        });
    }
}

void CHyprionGlaze::destroy() {
    if (Hyprion::glazeProtocol)
        Hyprion::glazeProtocol->destroyGlaze(this);
}

void CHyprionGlaze::damage() {
    const auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
    if (!surface || !g_pHyprRenderer)
        return;

    const auto box = surface->getSurfaceBoxGlobal();
    if (!box)
        return;

    CBox damageBox{box->pos() + Vector2D{m_x, m_y}, Vector2D{m_width, m_height}};
    damageBox.expand(m_radius + m_inset + m_outset + 2.F);
    g_pHyprRenderer->damageBox(damageBox);
}

void CHyprionGlaze::draw(Render::CRenderContext& ctx, PHLMONITOR monitor) {
    if (!m_resource || !monitor || m_width <= 0 || m_height <= 0 || m_inset <= 0)
        return;
    if (!(m_sources & HYPRION_GLAZE_V1_SOURCE_INSET_FILL))
        return;

    const auto box = boxFor(monitor);
    if (box.width <= 0 || box.height <= 0)
        return;

    const auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
    const auto view = surface ? surface->view() : nullptr;
    const auto layer = view ? Desktop::View::CLayerSurface::fromView(view) : nullptr;
    if (!layer)
        return;

    SHyprionGlazeParams params{
        .box = box,
        .thickness = static_cast<float>(m_inset),
        .radius = m_radius,
        .roundingPower = m_roundingPower,
        .style = m_style,
        .alpha = layer->alpha()[Desktop::View::LS_ALPHA_FADE]->value(),
    };
    hyprionDrawInsetGlaze(ctx, monitor, params);
}

bool CHyprionGlaze::matches(const PHLLS& layer) const {
    const auto surface = m_surface.lock();
    return surface && layer && layer->wlSurface() && layer->wlSurface()->resource() == surface;
}

CBox CHyprionGlaze::boxFor(PHLMONITOR monitor) const {
    const auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
    if (!surface || !monitor)
        return {0, 0, 0, 0};

    const auto surfaceBox = surface->getSurfaceBoxGlobal();
    if (!surfaceBox)
        return {0, 0, 0, 0};

    CBox box{surfaceBox->pos() + Vector2D{m_x, m_y}, Vector2D{m_width, m_height}};
    box.translate(-monitor->m_position);
    if (box.intersection(monitor->logicalBox()).empty())
        return {0, 0, 0, 0};
    return box;
}

class CHyprionGlazePassElement final : public IPassElement {
  public:
    CHyprionGlazePassElement(CHyprionGlaze* glaze, PHLMONITOR monitor) : m_glaze(glaze), m_monitor(monitor) {}

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override {
        if (m_glaze)
            m_glaze->draw(ctx, m_monitor);
        return {};
    }

    bool needsLiveBlur(Render::CRenderContext&) override {
        return false;
    }

    bool needsPrecomputeBlur(Render::CRenderContext&) override {
        return false;
    }

    const char* passName() override {
        return "hyprion-glaze";
    }

    ePassElementType type() override {
        return EK_CUSTOM;
    }

    std::optional<CBox> boundingBox(Render::CRenderContext&) override {
        if (!m_glaze)
            return std::nullopt;
        const auto box = m_glaze->boxFor(m_monitor);
        if (box.width <= 0 || box.height <= 0)
            return std::nullopt;
        return box;
    }

    bool disableSimplification(Render::CRenderContext&) override {
        return true;
    }

  private:
    CHyprionGlaze* m_glaze = nullptr;
    PHLMONITOR     m_monitor;
};

CHyprionGlazeProtocol::CHyprionGlazeProtocol() : IWaylandProtocol(&hyprion_glaze_manager_v1_interface, 1, "HyprionGlaze") {
    ;
}

void CHyprionGlazeProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto manager = m_managers.emplace_back(makeUnique<CHyprionGlazeManagerV1>(client, ver, id)).get();
    manager->setDestroy([this, manager](CHyprionGlazeManagerV1*) { destroyManager(manager); });
    manager->setOnDestroy([this, manager](CHyprionGlazeManagerV1*) { destroyManager(manager); });
    manager->setGetGlaze([this](CHyprionGlazeManagerV1* manager, uint32_t id, wl_resource* surface) { getGlaze(manager, id, surface); });
}

void CHyprionGlazeProtocol::destroyManager(CHyprionGlazeManagerV1* manager) {
    std::erase_if(m_managers, [manager](const auto& other) { return other.get() == manager; });
}

void CHyprionGlazeProtocol::destroyGlaze(CHyprionGlaze* glaze) {
    std::erase_if(m_glazes, [glaze](const auto& other) { return other.get() == glaze; });
}

void CHyprionGlazeProtocol::getGlaze(CHyprionGlazeManagerV1* manager, uint32_t id, wl_resource* surfaceResource) {
    const auto surface = CWLSurfaceResource::fromResource(surfaceResource);
    if (!surface) {
        manager->error(HYPRION_GLAZE_V1_ERROR_SURFACE_DESTROYED, "invalid wl_surface");
        return;
    }

    const auto glaze = m_glazes.emplace_back(makeUnique<CHyprionGlaze>(makeShared<CHyprionGlazeV1>(manager->client(), manager->version(), id), surface)).get();
    if (!glaze->good()) {
        manager->noMemory();
        m_glazes.pop_back();
    }
}

void CHyprionGlazeProtocol::queue(Render::CRenderContext& ctx, const PHLLS& layer, PHLMONITOR monitor) {
    if (!layer || !layer->mapped() || !monitor)
        return;
    for (const auto& glaze : m_glazes) {
        if (glaze->matches(layer))
            g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionGlazePassElement>(glaze.get(), monitor));
    }
}
