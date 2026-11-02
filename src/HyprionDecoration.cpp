#include "HyprionDecoration.hpp"
#include "HyprionConfig.hpp"
#include "HyprionGradeRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <unordered_map>

#include <drm_fourcc.h>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

using namespace Hyprutils::Memory;
using namespace Render;
using namespace Render::GL;

namespace {
    constexpr uint64_t EDGES = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;


    bool outsetUsesSharedBlur(Render::CRenderContext& ctx, const PHLWINDOW& window) {
        return g_pHyprRenderer && ctx.m_data.pMonitor && window && g_pHyprRenderer->shouldUseNewBlurOptimizations(ctx, nullptr, window);
    }
}

class CHyprionPassElement final : public IPassElement {
  public:
    CHyprionPassElement(CHyprionDecoration* decoration, float alpha, CBox box, bool outset, bool snapshotOnly = false) :
        m_decoration(decoration), m_alpha(alpha), m_box(box), m_outset(outset), m_snapshotOnly(snapshotOnly) {}

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override {
        if (m_decoration)
            m_decoration->drawPass(ctx, m_alpha, m_box, m_outset, m_snapshotOnly);
        return {};
    }

    bool needsLiveBlur(Render::CRenderContext& ctx) override { return snapshotUsesBlur() && !outsetSnapshotUsesCache(ctx); }
    bool needsPrecomputeBlur(Render::CRenderContext& ctx) override { return snapshotUsesBlur() && outsetSnapshotUsesCache(ctx); }
    const char* passName() override { return "hyprion"; }
    ePassElementType type() override { return EK_CUSTOM; }
    std::optional<CBox> boundingBox(Render::CRenderContext&) override { return m_box; }
    bool disableSimplification(Render::CRenderContext&) override { return true; }

  private:
    bool snapshotUsesBlur() const { return m_outset && m_snapshotOnly && hyprionConfig().outsetBlur; }
    bool outsetSnapshotUsesCache(Render::CRenderContext& ctx) const { return m_decoration && outsetUsesSharedBlur(ctx, m_decoration->m_window.lock()); }

    CHyprionDecoration* m_decoration = nullptr;
    float            m_alpha = 1.F;
    CBox             m_box;
    bool             m_outset;
    bool             m_snapshotOnly;
};

CHyprionDecoration::CHyprionDecoration(PHLWINDOW window) :
    IHyprWindowDecoration(window), m_window(window),
    m_animations{CHyprionAnimation(Desktop::focusState()->window() == window, (window->m_hints & Desktop::View::WINDOW_HINT_URGENT) != Desktop::View::WINDOW_HINT_NONE),
        CHyprionAnimation(Desktop::focusState()->window() == window, (window->m_hints & Desktop::View::WINDOW_HINT_URGENT) != Desktop::View::WINDOW_HINT_NONE)} {
    for (auto& animation : m_animations)
        animation.bind(window);
}

CHyprionDecoration::~CHyprionDecoration() {
    damageEntire();
}

void CHyprionDecoration::stateChanged(bool active, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    for (size_t i = 0; i < m_animations.size(); ++i) {
        if (config.enabled && profiles[i]->enabled)
            m_animations[i].stateChanged(active, urgent, *profiles[i], now);
        else
            m_animations[i].reset(active, urgent, now);
    }
    damageBorder();
}

void CHyprionDecoration::refreshAnimation(bool active, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    for (size_t i = 0; i < m_animations.size(); ++i) {
        if (config.enabled && profiles[i]->enabled)
            m_animations[i].refresh(active, urgent, *profiles[i], now);
        else
            m_animations[i].reset(active, urgent, now);
    }
    damageBorder();
}

bool CHyprionDecoration::animate(const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    bool running = false;
    for (size_t i = 0; i < m_animations.size(); ++i)
        running = m_animations[i].advance(*profiles[i], now) || running;
    if (running)
        damageBorder();
    return running;
}

void CHyprionDecoration::resetAnimation(bool active, bool urgent, CHyprionAnimation::Time now) {
    for (auto& animation : m_animations)
        animation.reset(active, urgent, now);
    damageBorder();
}

SDecorationPositioningInfo CHyprionDecoration::getPositioningInfo() {
    const auto window = m_window.lock();
    const auto config = hyprionConfig();
    const bool decorated = window && Fullscreen::controller()->getFullscreenModes(window).internal != Fullscreen::FSMODE_FULLSCREEN && window->m_ruleApplicator->decorate().valueOrDefault() &&
        !window->backend().traits().suggestsNoBorder;
    const int extent = decorated && config.enabled && config.outset.enabled ? config.outset.size : 0;
    SDecorationPositioningInfo info;
    info.policy = DECORATION_POSITION_ABSOLUTE;
    info.reserved = extent > 0;
    // Before the native border (10000), so it wraps our outset ring.
    info.priority = 10010;
    info.edges = EDGES;
    info.desiredExtents = {{extent, extent}, {extent, extent}};
    m_outsetExtent = extent;
    return info;
}
void CHyprionDecoration::onPositioningReply(const SDecorationPositioningReply&) {
    damageEntire();
}

CBox CHyprionDecoration::monitorLocalBox(PHLMONITOR monitor, const Render::SWindowRenderPresentation& presentation) const {
    const auto window = m_window.lock();
    if (!window || !monitor)
        return {};

    // Decoration assignments include preceding outer outlines; sample the surface itself.
    CBox box = window->getWindowMainSurfaceBox();
    box.translate(presentation.floatingOffset + presentation.workspaceOffset - monitor->m_position);
    return box;
}
void CHyprionDecoration::updateOrbitArcTable(float power) {
    if (m_orbitArcPower == power)
        return;

    constexpr int tableSize = 64;
    constexpr int subdivisions = 4;
    constexpr double halfPi = 1.57079632679489661923;
    const double exponent = 2.0 / static_cast<double>(power);
    double previousX = 1.0;
    double previousY = 0.0;
    double length = 0.0;
    m_orbitArcTable[0] = 0.F;
    for (int index = 1; index <= tableSize; ++index) {
        for (int subdivision = 1; subdivision <= subdivisions; ++subdivision) {
            const int sample = (index - 1) * subdivisions + subdivision;
            const double theta = halfPi * static_cast<double>(sample) / static_cast<double>(tableSize * subdivisions);
            const double x = std::pow(std::max(std::cos(theta), 0.0), exponent);
            const double y = std::pow(std::max(std::sin(theta), 0.0), exponent);
            length += std::hypot(x - previousX, y - previousY);
            previousX = x;
            previousY = y;
        }
        m_orbitArcTable[static_cast<std::size_t>(index)] = static_cast<float>(length);
    }
    m_orbitArcPower = power;
}


void CHyprionDecoration::draw(Render::CRenderContext& ctx, PHLMONITOR monitor, float const& alpha, const Render::SWindowRenderPresentation& presentation) {
    const auto window = m_window.lock();
    const auto config = hyprionConfig();
    if (!window || !validMapped(window) || !monitor || !config.enabled || !config.inset.enabled || !std::isfinite(alpha) || alpha <= 0.0F)
        return;
    const auto box = monitorLocalBox(monitor, presentation);
    if (box.width <= 0.0 || box.height <= 0.0)
        return;
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box, false));
}

void CHyprionDecoration::drawOutset(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha,
                                   const Render::SWindowRenderPresentation& presentation, bool snapshotOnly) {
    const auto window = m_window.lock();
    const auto config = hyprionConfig();
    if (!window || !validMapped(window) || !monitor || !config.enabled || !config.outset.enabled || !std::isfinite(alpha) || alpha <= 0.F)
        return;
    auto box = monitorLocalBox(monitor, presentation);
    box.expand(config.outset.size);
    if (box.width > 0.0 && box.height > 0.0)
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box, true, snapshotOnly));
}
void CHyprionDecoration::drawPass(Render::CRenderContext& ctx, float alpha, const CBox& monitorLocalBox_, bool outset, bool snapshotOnly) {
    if (snapshotOnly)
        m_outsetSnapshotReady = false;
    else if (outset && !m_outsetSnapshotReady)
        return;

    const auto monitor = ctx.m_data.pMonitor.lock();
    if (!monitor || !g_pHyprRenderer)
        return;

    auto& renderData = ctx.m_data;
    const auto window = m_window.lock();
    if (!window || !validMapped(window) || renderData.damage.empty() || !renderData.currentFB)
        return;
    const auto config = hyprionConfig();
    const auto& profile = outset ? config.outset : config.inset;
    if (!config.enabled || !profile.enabled || !std::isfinite(alpha) || alpha <= 0.0F)
        return;
    const auto frameStyle = m_animations[outset ? 1 : 0].style(profile, CHyprionAnimation::Clock::now());
    const auto& style = frameStyle.border;
    const auto& orbit = frameStyle.orbit;
    const bool baseTransformVisible = style.grade.lightness != 0.F || style.grade.saturation != 1.F || style.grade.chroma != 0.F || style.grade.hue != 0.F || style.grade.colorize.strength > 0.F;
    const bool baseVisible = style.opacity > 0.F && (baseTransformVisible || (outset && config.outsetBlur));
    const bool orbitVisible = orbit.amount > 0.F && orbit.length > 0.F;
    if (!baseVisible && !orbitVisible)
        return;

    const auto framebuffer = renderData.currentFB;
    const double rawWidth = framebuffer->m_size.x;
    const double rawHeight = framebuffer->m_size.y;
    if (!std::isfinite(rawWidth) || !std::isfinite(rawHeight) || rawWidth < 1 || rawHeight < 1 ||
        rawWidth > std::numeric_limits<int>::max() || rawHeight > std::numeric_limits<int>::max())
        return;
    const int targetWidth = static_cast<int>(rawWidth);
    const int targetHeight = static_cast<int>(rawHeight);
    const int maximumStrip = std::min({targetWidth, targetHeight, std::numeric_limits<int>::max() / 2});
    if (maximumStrip < 1)
        return;

    CBox renderBox = monitorLocalBox_;
    renderBox.scale(monitor->m_scale).round();
    renderData.renderModif.applyToBox(renderBox);
    if (renderBox.width <= 0.0 || renderBox.height <= 0.0 || !std::isfinite(renderBox.width) || !std::isfinite(renderBox.height))
        return;

    const double monitorScale = monitor->m_scale;
    const double modificationScale = renderData.renderModif.combinedScale();
    if (!std::isfinite(monitorScale) || monitorScale <= 0.0 || !std::isfinite(modificationScale) || modificationScale <= 0.0)
        return;

    const double requestedThickness = std::round(std::round(static_cast<double>(profile.size) * monitorScale) * modificationScale);
    const double boundedThickness = std::clamp(requestedThickness, 1.0, static_cast<double>(maximumStrip));
    const int thickness = static_cast<int>(boundedThickness);

    const double surfaceRounding = std::max(0.0, static_cast<double>(window->presentation().rounding()) * monitorScale - 1.0) * modificationScale;
    const float roundingPower = std::clamp(std::isfinite(window->presentation().roundingPower()) ? window->presentation().roundingPower() : 2.0F, 1.0F, 10.0F);
    const double baseRounding = static_cast<double>(window->presentation().rounding()) * monitorScale * modificationScale;
    const double correction = thickness * (std::sqrt(2.0) - 1.0) * std::max(2.0F - roundingPower, 0.0F);
    const double nativeRounding = outset ? (baseRounding > 0.0 ? baseRounding + thickness - correction : 0.0) : surfaceRounding;
    if (!std::isfinite(nativeRounding))
        return;
    const double halfSize = std::min(renderBox.width, renderBox.height) * 0.5;
    const int rounding = static_cast<int>(std::clamp(nativeRounding, 0.0, halfSize));
    const int innerRounding = static_cast<int>(std::clamp(outset ? baseRounding : std::max(nativeRounding - thickness, 0.0), 0.0, halfSize));
    if (orbitVisible)
        updateOrbitArcTable(roundingPower);

    const double requestedSnapshotExtent = std::ceil(std::max(static_cast<double>(thickness), static_cast<double>(rounding) + 1.0));
    const int snapshotExtent = static_cast<int>(std::clamp(requestedSnapshotExtent, 1.0, static_cast<double>(maximumStrip)));

    auto source = SHyprionGradeSource::CURRENT_FRAMEBUFFER;
    if (outset && config.outsetBlur) {
        if (!monitor->resources()->m_blurFB || !monitor->resources()->m_blurFB->getTexture())
            return;
        source = outsetUsesSharedBlur(ctx, window) ? SHyprionGradeSource::BLUR_CACHE : SHyprionGradeSource::LIVE_BLUR;
        if (snapshotOnly && source == SHyprionGradeSource::BLUR_CACHE && monitor->m_blurFBDirty)
            return;
    }

    const bool success = hyprionDrawGrade(ctx, monitor,
                                          SHyprionGradeRequest{
                                              .renderBox = renderBox,
                                              .area = outset ? SHyprionGradeArea::OUTSET : SHyprionGradeArea::INSET,
                                              .mode = outset ? SHyprionGradeMode::OVER : SHyprionGradeMode::IN_PLACE,
                                              .source = source,
                                              .thickness = thickness,
                                              .rounding = rounding,
                                              .innerRounding = innerRounding,
                                              .snapshotExtent = snapshotExtent,
                                              .roundingPower = roundingPower,
                                              .style = style,
                                              .orbit = orbit,
                                              .cornerArcTable = m_orbitArcTable,
                                              .alpha = alpha,
                                              .fullSnapshot = false,
                                              .snapshotOnly = snapshotOnly,
                                              .clipToDamageBox = true,
                                          });
    if (snapshotOnly)
        m_outsetSnapshotReady = success;
    else if (outset)
        m_outsetSnapshotReady = false;
}

eDecorationType CHyprionDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CHyprionDecoration::updateWindow(PHLWINDOW window) {
    m_window = window;
    // Rebinding also picks up the animation tree a config reload rebuilt.
    for (auto& animation : m_animations)
        animation.bind(window);
    damageEntire();
    const int previousExtent = m_outsetExtent;
    getPositioningInfo();
    if (previousExtent != m_outsetExtent)
        g_pDecorationPositioner->repositionDeco(this);
}

void CHyprionDecoration::damageEntire() {
    const auto window = m_window.lock();
    if (window && validMapped(window) && g_pHyprRenderer)
        g_pHyprRenderer->damageWindow(window);
}
void CHyprionDecoration::damageBorder() {
    const auto window = m_window.lock();
    if (!window || !validMapped(window) || !g_pHyprRenderer)
        return;

    CBox box = window->getWindowMainSurfaceBox();
    box.translate(window->presentation().floatingOffset() + window->presentation().renderPresentation().workspaceOffset);

    const auto config = hyprionConfig();
    const double borderExtent = static_cast<double>(std::max(config.inset.size, config.outset.size)) + static_cast<double>(window->presentation().rounding());
    if (!std::isfinite(borderExtent) || borderExtent <= 0.0 || box.width <= 0.0 || box.height <= 0.0)
        return;

    if (config.enabled && config.outset.enabled)
        box.expand(config.outset.size);
    CRegion borderRegion(box);
    if (box.width > 2.0 * borderExtent && box.height > 2.0 * borderExtent)
        borderRegion.subtract(box.copy().expand(-borderExtent));
    borderRegion.expand(2);
    g_pHyprRenderer->damageRegion(borderRegion);
}

eDecorationLayer CHyprionDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CHyprionDecoration::getDecorationFlags() {
    return DECORATION_PART_OF_MAIN_WINDOW;
}

std::string CHyprionDecoration::getDisplayName() {
    return "hyprion";
}
