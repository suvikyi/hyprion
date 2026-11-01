#include "HyprionDecoration.hpp"
#include "HyprionConfig.hpp"
#include "HyprionGradeShader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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


    struct SUniforms {
        GLint horizontal = -1;
        GLint vertical = -1;
        GLint snapshot = -1;
        GLint fullSnapshot = -1;
        GLint areaMode = -1;
        GLint composite = -1;
        GLint windowBox = -1;
        GLint thickness = -1;
        GLint outset = -1;
        GLint snapshotExtent = -1;
        GLint rounding = -1;
        GLint innerRounding = -1;
        GLint roundingPower = -1;
        GLint strength = -1;
        GLint opacity = -1;
        GLint lightness = -1;
        GLint saturation = -1;
        GLint chroma = -1;
        GLint hue = -1;
        GLint tintChroma = -1;
        GLint tintStrength = -1;
        GLint orbitPhase = -1;
        GLint orbitLength = -1;
        GLint orbitLightness = -1;
        GLint orbitSaturation = -1;
        GLint orbitChroma = -1;
        GLint orbitHue = -1;
        GLint orbitStrength = -1;
        GLint orbitOpacity = -1;
        GLint orbitAmount = -1;
        GLint orbitTintChroma = -1;
        GLint orbitTintStrength = -1;
        GLint cornerArcTable = -1;
        GLint sourceTF = -1;
        GLint targetTF = -1;
        GLint sourceRange = -1;
        GLint targetRange = -1;
        GLint defaultReference = -1;
        GLint toDefaultPrimaries = -1;
        GLint fromDefaultPrimaries = -1;
    };


    struct SMonitorSnapshots {
        SP<IFramebuffer> horizontal;
        SP<IFramebuffer> vertical;
        int              width = 0;
        int              height = 0;
        int              maxStrip = 0;
        DRMFormat        format = DRM_FORMAT_INVALID;
        uint64_t         imageDescriptionId = 0;

        int              failedWidth = 0;
        int              failedHeight = 0;
        int              failedStrip = 0;
        DRMFormat        failedFormat = DRM_FORMAT_INVALID;
        uint64_t         failedImageDescriptionId = 0;
    };

    SP<CShader>                                shader;
    SUniforms                                  uniforms;
    std::unordered_map<uint64_t, SMonitorSnapshots> monitorSnapshots;

    std::array<GLfloat, 9> matrixFor(const std::array<std::array<double, 3>, 3>& matrix) {
        return {
            static_cast<GLfloat>(matrix[0][0]), static_cast<GLfloat>(matrix[1][0]), static_cast<GLfloat>(matrix[2][0]),
            static_cast<GLfloat>(matrix[0][1]), static_cast<GLfloat>(matrix[1][1]), static_cast<GLfloat>(matrix[2][1]),
            static_cast<GLfloat>(matrix[0][2]), static_cast<GLfloat>(matrix[1][2]), static_cast<GLfloat>(matrix[2][2]),
        };
    }

    std::array<GLfloat, 2> toOklabChroma(const std::array<float, 3>& srgbColor, const std::array<std::array<double, 3>, 3>& toDefault) {
        std::array<double, 3> linear{};
        for (size_t i = 0; i < linear.size(); ++i) {
            const double channel = std::clamp(static_cast<double>(srgbColor[i]), 0.0, 1.0);
            linear[i] = channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
        }

        std::array<double, 3> rgb{};
        for (size_t row = 0; row < rgb.size(); ++row) {
            for (size_t column = 0; column < linear.size(); ++column)
                rgb[row] += toDefault[row][column] * linear[column];
        }

        const auto cubeRoot = [](double value) { return std::cbrt(std::max(value, 0.0)); };
        const double l = cubeRoot(0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2]);
        const double m = cubeRoot(0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2]);
        const double s = cubeRoot(0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2]);
        return {
            static_cast<GLfloat>(1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s),
            static_cast<GLfloat>(0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s),
        };
    }

    bool configureTexture(const SP<ITexture>& texture) {
        if (!texture)
            return false;
        texture->bind();
        texture->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        texture->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        texture->setTexParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        texture->setTexParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        texture->unbind();
        return true;
    }

    // Hyprland picks the blur source for a window: the shared pre-window cache
    // (tiled windows with new_optimizations, or xray) or a live blur of the
    // framebuffer. Ask it so the outset matches whatever the window itself gets.
    bool outsetUsesSharedBlur(Render::CRenderContext& ctx, const PHLWINDOW& window) {
        return g_pHyprRenderer && ctx.m_data.pMonitor && window && g_pHyprRenderer->shouldUseNewBlurOptimizations(ctx, nullptr, window);
    }

    bool allocateFramebuffer(SP<IFramebuffer>& framebuffer, const std::string& name, int width, int height, DRMFormat format,
                             const NColorManagement::PImageDescription& imageDescription) {
        framebuffer = g_pHyprRenderer->createFB(name);
        if (!framebuffer || !framebuffer->alloc(width, height, format))
            return false;
        framebuffer->setImageDescription(imageDescription);
        return configureTexture(framebuffer->getTexture());
    }

    void copyClippedStrip(int targetWidth, int targetHeight, int textureWidth, int textureHeight, int64_t destinationX, int64_t destinationY, int64_t sourceX,
                          int64_t sourceY, int64_t width, int64_t height) {
        if (width <= 0 || height <= 0)
            return;

        const int64_t destinationLeft   = std::max<int64_t>(destinationX, 0);
        const int64_t destinationBottom = std::max<int64_t>(destinationY, 0);
        const int64_t destinationRight  = std::min<int64_t>(destinationX + width, textureWidth);
        const int64_t destinationTop    = std::min<int64_t>(destinationY + height, textureHeight);
        if (destinationRight <= destinationLeft || destinationTop <= destinationBottom)
            return;

        const int64_t requestedWidth  = destinationRight - destinationLeft;
        const int64_t requestedHeight = destinationTop - destinationBottom;
        const int64_t requestedSourceX = sourceX + destinationLeft - destinationX;
        const int64_t requestedSourceY = sourceY + destinationBottom - destinationY;
        const int64_t clippedLeft   = std::max<int64_t>(requestedSourceX, 0);
        const int64_t clippedBottom = std::max<int64_t>(requestedSourceY, 0);
        const int64_t clippedRight  = std::min<int64_t>(requestedSourceX + requestedWidth, targetWidth);
        const int64_t clippedTop    = std::min<int64_t>(requestedSourceY + requestedHeight, targetHeight);
        if (clippedRight <= clippedLeft || clippedTop <= clippedBottom)
            return;

        const int64_t copyWidth  = clippedRight - clippedLeft;
        const int64_t copyHeight = clippedTop - clippedBottom;
        const int64_t destX      = destinationLeft + clippedLeft - requestedSourceX;
        const int64_t destY      = destinationBottom + clippedBottom - requestedSourceY;
        if (destX < 0 || destY < 0 || destX + copyWidth > textureWidth || destY + copyHeight > textureHeight ||
            clippedLeft > std::numeric_limits<GLint>::max() || clippedBottom > std::numeric_limits<GLint>::max() || copyWidth > std::numeric_limits<GLsizei>::max() ||
            copyHeight > std::numeric_limits<GLsizei>::max())
            return;

        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(destX), static_cast<GLint>(destY), static_cast<GLint>(clippedLeft),
                            static_cast<GLint>(clippedBottom), static_cast<GLsizei>(copyWidth), static_cast<GLsizei>(copyHeight));
    }

    bool ensureSnapshots(PHLMONITOR monitor, const SP<IFramebuffer>& sourceFramebuffer, const NColorManagement::PImageDescription& imageDescription, int width,
                         int height, int strip) {
        auto& cache = monitorSnapshots[static_cast<uint64_t>(monitor->m_id)];
        const auto descriptionId = imageDescription->id();
        const bool sameTarget = cache.width == width && cache.height == height && cache.format == sourceFramebuffer->m_drmFormat &&
            cache.imageDescriptionId == descriptionId;
        const bool ready = sameTarget && cache.horizontal && cache.vertical && cache.maxStrip >= strip;
        if (ready)
            return true;

        const bool previouslyFailed = cache.failedWidth == width && cache.failedHeight == height && cache.failedStrip == strip &&
            cache.failedFormat == sourceFramebuffer->m_drmFormat && cache.failedImageDescriptionId == descriptionId;
        if (previouslyFailed)
            return false;

        const int allocationStrip = sameTarget ? std::max(cache.maxStrip, strip) : strip;
        if (allocationStrip < 1 || allocationStrip > std::numeric_limits<int>::max() / 2) {
            LOG(Log::ERR, "hyprion: invalid snapshot strip size for monitor {}", monitor->m_id);
            return false;
        }

        SP<IFramebuffer> horizontal;
        SP<IFramebuffer> vertical;
        const auto monitorName = std::to_string(monitor->m_id);
        const bool allocated = allocateFramebuffer(horizontal, "hyprion-horizontal-" + monitorName, width, allocationStrip * 2, sourceFramebuffer->m_drmFormat, imageDescription) &&
            allocateFramebuffer(vertical, "hyprion-vertical-" + monitorName, allocationStrip * 2, height, sourceFramebuffer->m_drmFormat, imageDescription);
        if (!allocated) {
            cache.failedWidth = width;
            cache.failedHeight = height;
            cache.failedStrip = strip;
            cache.failedFormat = sourceFramebuffer->m_drmFormat;
            cache.failedImageDescriptionId = descriptionId;
            LOG(Log::ERR, "hyprion: failed to allocate pixel snapshots for monitor {}", monitor->m_id);
            return false;
        }

        cache.horizontal = std::move(horizontal);
        cache.vertical = std::move(vertical);
        cache.width = width;
        cache.height = height;
        cache.maxStrip = allocationStrip;
        cache.format = sourceFramebuffer->m_drmFormat;
        cache.imageDescriptionId = descriptionId;
        cache.failedWidth = 0;
        cache.failedHeight = 0;
        cache.failedStrip = 0;
        cache.failedFormat = DRM_FORMAT_INVALID;
        cache.failedImageDescriptionId = 0;
        return true;
    }

    void copyWindowSnapshots(SMonitorSnapshots& cache, int targetWidth, int targetHeight, int left, int bottom, int width, int height, int strip) {
        const auto horizontal = cache.horizontal->getTexture();
        const auto vertical = cache.vertical->getTexture();
        if (!horizontal || !vertical)
            return;

        horizontal->bind();
        // Keep the long texture axis in framebuffer coordinates. Window-local
        // offsets can exceed these monitor-sized textures for offscreen windows.
        copyClippedStrip(targetWidth, targetHeight, targetWidth, cache.maxStrip * 2, left, 0, left, bottom, width, strip);
        copyClippedStrip(targetWidth, targetHeight, targetWidth, cache.maxStrip * 2, left, strip, left, static_cast<int64_t>(bottom) + height - strip, width, strip);
        horizontal->unbind();

        const int64_t middleHeight = std::max<int64_t>(0, static_cast<int64_t>(height) - 2LL * strip);
        if (middleHeight == 0)
            return;
        vertical->bind();
        const int64_t middleStart = static_cast<int64_t>(bottom) + strip;
        copyClippedStrip(targetWidth, targetHeight, cache.maxStrip * 2, targetHeight, 0, middleStart, left, middleStart, strip, middleHeight);
        copyClippedStrip(targetWidth, targetHeight, cache.maxStrip * 2, targetHeight, strip, middleStart, static_cast<int64_t>(left) + width - strip,
                         middleStart, strip, middleHeight);
        vertical->unbind();
    }


    bool safeRoundedInt(double value, int& result) {
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min() / 2) || value > static_cast<double>(std::numeric_limits<int>::max() / 2))
            return false;
        result = static_cast<int>(std::lround(value));
        return true;
    }

    void setMat3(GLint location, const std::array<GLfloat, 9>& matrix) {
        if (location >= 0)
            glUniformMatrix3fv(location, 1, GL_FALSE, matrix.data());
    }

    void setFloat(GLint location, GLfloat value) {
        if (location >= 0)
            glUniform1f(location, value);
    }

    void setInt(GLint location, GLint value) {
        if (location >= 0)
            glUniform1i(location, value);
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

void CHyprionDecoration::stateChanged(bool focused, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    for (size_t i = 0; i < m_animations.size(); ++i) {
        if (config.enabled && profiles[i]->enabled)
            m_animations[i].stateChanged(focused, urgent, *profiles[i], now);
        else
            m_animations[i].reset(focused, urgent, now);
    }
    damageBorder();
}

void CHyprionDecoration::refreshAnimation(bool focused, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    for (size_t i = 0; i < m_animations.size(); ++i) {
        if (config.enabled && profiles[i]->enabled)
            m_animations[i].refresh(focused, urgent, *profiles[i], now);
        else
            m_animations[i].reset(focused, urgent, now);
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

void CHyprionDecoration::resetAnimation(bool focused, bool urgent, CHyprionAnimation::Time now) {
    for (auto& animation : m_animations)
        animation.reset(focused, urgent, now);
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
    if (!window || !validMapped(window) || !monitor || !config.enabled || !std::isfinite(alpha) || alpha <= 0.0F)
        return;
    const auto box = monitorLocalBox(monitor, presentation);
    if (box.width <= 0.0 || box.height <= 0.0)
        return;
    // Consume the pre-shadow snapshot before inset rendering reuses the cache.
    if (config.outset.enabled)
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box.copy().expand(config.outset.size), true));
    if (config.inset.enabled)
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box, false));
}

void CHyprionDecoration::drawOutset(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha, const Render::SWindowRenderPresentation& presentation) {
    const auto window = m_window.lock();
    const auto config = hyprionConfig();
    if (!window || !validMapped(window) || !monitor || !config.enabled || !config.outset.enabled || !std::isfinite(alpha) || alpha <= 0.F)
        return;
    auto box = monitorLocalBox(monitor, presentation);
    box.expand(config.outset.size);
    if (box.width > 0.0 && box.height > 0.0)
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box, true, true));
}
void CHyprionDecoration::drawPass(Render::CRenderContext& ctx, float alpha, const CBox& monitorLocalBox_, bool outset, bool snapshotOnly) {
    if (snapshotOnly)
        m_outsetSnapshotReady = false;
    else if (outset && !m_outsetSnapshotReady)
        return;
    const auto monitor = ctx.m_data.pMonitor.lock();
    if (!monitor || !g_pHyprRenderer || !g_pHyprOpenGL)
        return;
    if (!shader && !initializeHyprionRenderer())
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
    const bool baseTransformVisible = style.strength > 0.F && (style.lightness != 0.F || style.saturation != 1.F || style.chroma != 0.F || style.hue != 0.F || style.tintStrength > 0.F);
    const bool baseVisible = style.opacity > 0.F && (baseTransformVisible || (outset && config.outsetBlur));
    const bool orbitVisible = orbit.amount > 0.F && orbit.length > 0.F && orbit.strength > 0.F;
    if (!baseVisible && !orbitVisible)
        return;
    const float overallStrength = style.strength;
    const float overallOpacity = std::clamp(style.opacity * alpha, 0.0F, 1.0F);
    const float overallOrbitStrength = orbit.strength;
    const float overallOrbitOpacity = std::clamp(orbit.opacity * alpha, 0.0F, 1.0F);

    const auto framebuffer = renderData.currentFB;
    const auto imageDescription = framebuffer->imageDescription();
    if (!imageDescription.valid() || imageDescription->value().icc.present)
        return;

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
    // Native borders expand the radius along with the box, with a correction for
    // powers below two (CHyprBorderDecoration::draw).
    const double baseRounding = static_cast<double>(window->presentation().rounding()) * monitorScale * modificationScale;
    const double correction = thickness * (std::sqrt(2.0) - 1.0) * std::max(2.0F - roundingPower, 0.0F);
    const double nativeRounding = outset ? (baseRounding > 0.0 ? baseRounding + thickness - correction : 0.0) : surfaceRounding;
    if (!std::isfinite(nativeRounding))
        return;
    const float rounding = static_cast<float>(std::clamp(nativeRounding, 0.0, std::min(renderBox.width, renderBox.height) * 0.5));
    if (orbitVisible)
        updateOrbitArcTable(roundingPower);

    const double requestedSnapshotExtent = std::ceil(std::max(static_cast<double>(thickness), static_cast<double>(rounding) + 1.0));
    const int snapshotExtent = static_cast<int>(std::clamp(requestedSnapshotExtent, 1.0, static_cast<double>(maximumStrip)));

    const auto defaultDescription = NColorManagement::getDefaultImageDescription();
    if (!defaultDescription.valid() || defaultDescription->value().icc.present)
        return;

    CBox targetBox = renderBox;
    targetBox.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)), monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    targetBox.round();

    // Hyprland's work framebuffer uses the transformed box coordinates directly,
    // as in Hyprland's native outline shader; do not flip Y to output-image coordinates.
    int left = 0, bottom = 0, width = 0, height = 0;
    if (!safeRoundedInt(targetBox.x, left) || !safeRoundedInt(targetBox.y, bottom) || !safeRoundedInt(targetBox.width, width) ||
        !safeRoundedInt(targetBox.height, height) || width <= 0 || height <= 0)
        return;

    CRegion borderRegion = renderData.damage.copy().intersect(renderBox);
    const double borderExtent = static_cast<double>(thickness) + rounding;
    if (renderBox.width > 2.0 * borderExtent && renderBox.height > 2.0 * borderExtent)
        borderRegion.subtract(renderBox.copy().expand(-borderExtent));
    if (renderData.clipBox.width != 0 && renderData.clipBox.height != 0)
        borderRegion.intersect(renderData.clipBox);
    if (borderRegion.empty())
        return;

    // The snapshot is only read where the border is damaged, so only now pick the
    // source: keeps the live blur below off windows that are not being redrawn.
    // Both passes must agree on the source description, or the shared pixel cache
    // is reallocated between them, so the cache framebuffer is the reference for
    // either branch and only the snapshot pass runs the blur.
    auto snapshotFramebuffer = framebuffer;
    SP<IFramebuffer> liveBlurFramebuffer;
    if (outset && config.outsetBlur) {
        // Hyprland's own choice for this window: tiled windows (with
        // new_optimizations) and xray rules read the shared pre-window cache,
        // everything else blurs the framebuffer as it stands.
        const bool sharedCache = outsetUsesSharedBlur(ctx, window);
        snapshotFramebuffer = monitor->resources()->m_blurFB;
        if (!snapshotFramebuffer || !snapshotFramebuffer->getTexture() || snapshotFramebuffer->m_size != framebuffer->m_size)
            return;
        if (snapshotOnly) {
            if (sharedCache) {
                if (monitor->m_blurFBDirty)
                    return;
            } else {
                CRegion blurDamage = renderData.damage.copy().intersect(renderBox);
                liveBlurFramebuffer = g_pHyprRenderer->blurMainFramebuffer(ctx, 1.F, blurDamage);
                if (!liveBlurFramebuffer || !liveBlurFramebuffer->getTexture() || liveBlurFramebuffer->m_size != framebuffer->m_size)
                    return;
            }
        }
    }
    const auto sourceDescription = snapshotFramebuffer->imageDescription();
    if (!sourceDescription.valid() || sourceDescription->value().icc.present)
        return;

    auto* const glFramebuffer = dynamic_cast<CGLFramebuffer*>(snapshotFramebuffer.get());
    if (!glFramebuffer || glFramebuffer->getFBID() == 0)
        return;

    glActiveTexture(GL_TEXTURE0);
    const bool snapshotsReady = ensureSnapshots(monitor, snapshotFramebuffer, sourceDescription, targetWidth, targetHeight, snapshotExtent);
    // Allocation can change the bound framebuffer, including on failure.
    framebuffer->bind();
    if (!snapshotsReady)
        return;
    auto& snapshots = monitorSnapshots.at(static_cast<uint64_t>(monitor->m_id));
    if (!outset || snapshotOnly) {
        GLint previousReadFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);

        // The live blur result lives in a recycled monitor work buffer, so read
        // it through a throwaway framebuffer before anything else reuses it.
        GLuint readFramebuffer = glFramebuffer->getFBID();
        GLuint temporaryFramebuffer = 0;
        const auto liveBlurTexture = liveBlurFramebuffer ? liveBlurFramebuffer->getTexture() : nullptr;
        if (liveBlurTexture && liveBlurTexture->m_texID != 0) {
            glGenFramebuffers(1, &temporaryFramebuffer);
            if (temporaryFramebuffer != 0) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, temporaryFramebuffer);
                glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, liveBlurTexture->m_texID, 0);
                readFramebuffer = temporaryFramebuffer;
            }
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        copyWindowSnapshots(snapshots, targetWidth, targetHeight, left, bottom, width, height, snapshotExtent);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        if (temporaryFramebuffer != 0)
            glDeleteFramebuffers(1, &temporaryFramebuffer);
    }
    if (snapshotOnly) {
        m_outsetSnapshotReady = true;
        return;
    }
    if (outset)
        m_outsetSnapshotReady = false;

    const auto inputSettings = g_pHyprRenderer->getCMSettings(ctx, sourceDescription, defaultDescription);
    const auto outputSettings = g_pHyprRenderer->getCMSettings(ctx, defaultDescription, imageDescription);
    const float reference = inputSettings.dstRefLuminance;
    if (!std::isfinite(reference) || reference <= 0.0F)
        return;

    const auto toDefault = matrixFor(inputSettings.convertMatrix);
    const auto fromDefault = matrixFor(outputSettings.convertMatrix);
    const auto projection = g_pHyprRenderer->projectBoxToTarget(ctx, renderBox);

    auto activeShader = g_pHyprOpenGL->useShader(shader);
    activeShader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, projection.getMatrix());
    glUniform1i(uniforms.horizontal, 0);
    glUniform1i(uniforms.vertical, 1);
    setInt(uniforms.fullSnapshot, 0);
    setInt(uniforms.areaMode, outset ? 1 : 0);
    setInt(uniforms.composite, outset ? 2 : 1);
    glUniform4f(uniforms.windowBox, static_cast<float>(left), static_cast<float>(bottom), static_cast<float>(width), static_cast<float>(height));
    setFloat(uniforms.thickness, static_cast<float>(thickness));
    setInt(uniforms.outset, outset ? 1 : 0);
    setInt(uniforms.snapshotExtent, snapshotExtent);
    setFloat(uniforms.rounding, rounding);
    setFloat(uniforms.innerRounding, outset ? static_cast<float>(baseRounding) : std::max(rounding - thickness, 0.F));
    setFloat(uniforms.roundingPower, roundingPower);
    setFloat(uniforms.strength, overallStrength);
    setFloat(uniforms.opacity, overallOpacity);
    setFloat(uniforms.lightness, style.lightness);
    setFloat(uniforms.saturation, style.saturation);
    setFloat(uniforms.chroma, style.chroma);
    setFloat(uniforms.hue, style.hue);
    if (style.tintStrength > 0.F) {
        const auto tintChroma = toOklabChroma(style.tintColor, inputSettings.convertMatrix);
        glUniform2f(uniforms.tintChroma, tintChroma[0], tintChroma[1]);
    }
    setFloat(uniforms.tintStrength, style.tintStrength);
    setFloat(uniforms.orbitPhase, orbit.phase);
    setFloat(uniforms.orbitLength, orbit.length);
    setFloat(uniforms.orbitLightness, orbit.lightness);
    setFloat(uniforms.orbitSaturation, orbit.saturation);
    setFloat(uniforms.orbitChroma, orbit.chroma);
    setFloat(uniforms.orbitHue, orbit.hue);
    setFloat(uniforms.orbitStrength, overallOrbitStrength);
    setFloat(uniforms.orbitOpacity, overallOrbitOpacity);
    setFloat(uniforms.orbitAmount, orbit.amount);
    if (orbit.tintStrength > 0.F) {
        const auto tintChroma = toOklabChroma(orbit.tintColor, inputSettings.convertMatrix);
        glUniform2f(uniforms.orbitTintChroma, tintChroma[0], tintChroma[1]);
    }
    setFloat(uniforms.orbitTintStrength, orbit.tintStrength);
    if (uniforms.cornerArcTable >= 0)
        glUniform1fv(uniforms.cornerArcTable, static_cast<GLsizei>(m_orbitArcTable.size()), m_orbitArcTable.data());
    setInt(uniforms.sourceTF, static_cast<GLint>(inputSettings.sourceTF));
    setInt(uniforms.targetTF, static_cast<GLint>(outputSettings.targetTF));
    if (uniforms.sourceRange >= 0)
        glUniform2f(uniforms.sourceRange, inputSettings.srcTFRange.min, inputSettings.srcTFRange.max);
    if (uniforms.targetRange >= 0)
        glUniform2f(uniforms.targetRange, outputSettings.dstTFRange.min, outputSettings.dstTFRange.max);
    setFloat(uniforms.defaultReference, reference);
    setMat3(uniforms.toDefaultPrimaries, toDefault);
    setMat3(uniforms.fromDefaultPrimaries, fromDefault);

    glActiveTexture(GL_TEXTURE0);
    snapshots.horizontal->getTexture()->bind();
    glActiveTexture(GL_TEXTURE1);
    snapshots.vertical->getTexture()->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(activeShader->getUniformLocation(SHADER_SHADER_VAO));

    const bool previousBlend = glIsEnabled(GL_BLEND);
    g_pHyprOpenGL->blend(outset);
    borderRegion.forEachRect([&](const auto& rect) {
        g_pHyprOpenGL->scissor(ctx, &rect, renderData.transformDamage);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });
    g_pHyprOpenGL->disableScissor();
    g_pHyprOpenGL->blend(previousBlend);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    snapshots.vertical->getTexture()->unbind();
    glActiveTexture(GL_TEXTURE0);
    snapshots.horizontal->getTexture()->unbind();
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


bool initializeHyprionRenderer() {
    if (shader)
        return true;
    if (!g_pHyprRenderer || g_pHyprRenderer->type() != IHyprRenderer::RT_GL || !g_pHyprOpenGL || !g_pHyprOpenGL->m_eglContext)
        return false;

    g_pHyprOpenGL->makeEGLCurrent();
    auto candidate = makeShared<CShader>();
    if (!candidate->createProgram(HyprionGradeShader::VERTEX, HyprionGradeShader::FRAGMENT, true, true)) {
        LOG(Log::ERR, "hyprion: failed to compile the sampling shader");
        return false;
    }

    const auto program = candidate->program();
    uniforms = {
        .horizontal = glGetUniformLocation(program, "uHorizontal"),
        .vertical = glGetUniformLocation(program, "uVertical"),
        .snapshot = glGetUniformLocation(program, "uSnapshot"),
        .fullSnapshot = glGetUniformLocation(program, "uFullSnapshot"),
        .areaMode = glGetUniformLocation(program, "uAreaMode"),
        .composite = glGetUniformLocation(program, "uComposite"),
        .windowBox = glGetUniformLocation(program, "uWindowBox"),
        .thickness = glGetUniformLocation(program, "uThickness"),
        .outset = glGetUniformLocation(program, "uOutset"),
        .snapshotExtent = glGetUniformLocation(program, "uSnapshotExtent"),
        .rounding = glGetUniformLocation(program, "uRounding"),
        .innerRounding = glGetUniformLocation(program, "uInnerRounding"),
        .roundingPower = glGetUniformLocation(program, "uRoundingPower"),
        .strength = glGetUniformLocation(program, "uStrength"),
        .opacity = glGetUniformLocation(program, "uOpacity"),
        .lightness = glGetUniformLocation(program, "uLightness"),
        .saturation = glGetUniformLocation(program, "uSaturation"),
        .chroma = glGetUniformLocation(program, "uChroma"),
        .hue = glGetUniformLocation(program, "uHue"),
        .tintChroma = glGetUniformLocation(program, "uTintChroma"),
        .tintStrength = glGetUniformLocation(program, "uTintStrength"),
        .orbitPhase = glGetUniformLocation(program, "uOrbitPhase"),
        .orbitLength = glGetUniformLocation(program, "uOrbitLength"),
        .orbitLightness = glGetUniformLocation(program, "uOrbitLightness"),
        .orbitSaturation = glGetUniformLocation(program, "uOrbitSaturation"),
        .orbitChroma = glGetUniformLocation(program, "uOrbitChroma"),
        .orbitHue = glGetUniformLocation(program, "uOrbitHue"),
        .orbitStrength = glGetUniformLocation(program, "uOrbitStrength"),
        .orbitOpacity = glGetUniformLocation(program, "uOrbitOpacity"),
        .orbitAmount = glGetUniformLocation(program, "uOrbitAmount"),
        .orbitTintChroma = glGetUniformLocation(program, "uOrbitTintChroma"),
        .orbitTintStrength = glGetUniformLocation(program, "uOrbitTintStrength"),
        .cornerArcTable = glGetUniformLocation(program, "uCornerArcTable[0]"),
        .sourceTF = glGetUniformLocation(program, "uSourceTF"),
        .targetTF = glGetUniformLocation(program, "uTargetTF"),
        .sourceRange = glGetUniformLocation(program, "uSourceRange"),
        .targetRange = glGetUniformLocation(program, "uTargetRange"),
        .defaultReference = glGetUniformLocation(program, "uDefaultReference"),
        .toDefaultPrimaries = glGetUniformLocation(program, "uToDefaultPrimaries"),
        .fromDefaultPrimaries = glGetUniformLocation(program, "uFromDefaultPrimaries"),
    };
    shader = std::move(candidate);
    return true;
}

void destroyHyprionRenderer() {
    if (g_pHyprOpenGL && g_pHyprOpenGL->m_eglContext)
        g_pHyprOpenGL->makeEGLCurrent();
    monitorSnapshots.clear();
    shader.reset();
    uniforms = {};
}

void destroyHyprionMonitor(PHLMONITOR monitor) {
    if (!monitor)
        return;
    const auto it = monitorSnapshots.find(static_cast<uint64_t>(monitor->m_id));
    if (it != monitorSnapshots.end()) {
        if (g_pHyprOpenGL && g_pHyprOpenGL->m_eglContext)
            g_pHyprOpenGL->makeEGLCurrent();
        monitorSnapshots.erase(it);
    }
}
