#define WLR_USE_UNSTABLE

#include "HyprionConfig.hpp"
#include "HyprionDecoration.hpp"
#include "HyprionGradeProtocol.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/CHyprDropShadowDecoration.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace {
    using namespace Hyprutils::Memory;

    constexpr auto ANIMATION_TICK = std::chrono::milliseconds(16);

    HANDLE                            pluginHandle = nullptr;
    bool                              pluginActive = false;
    std::vector<CHyprSignalListener>   listeners;
    SP<CEventLoopTimer>               animationTimer;
    PHLWINDOWREF                      lastFocusedWindow;
    CFunctionHook* shadowHook = nullptr;
    CFunctionHook* shadowDrawHook = nullptr;
    CFunctionHook* gradeLayerHook = nullptr;
    std::unordered_map<CHyprDropShadowDecoration*, PHLWINDOWREF> shadowWindows;

    SShadowRenderData shadowRenderData(CHyprDropShadowDecoration* shadow, Render::CRenderContext& ctx, PHLMONITOR monitor, const float& alpha,
                                       const Render::SWindowRenderPresentation& presentation) {
        using Original = SShadowRenderData (*)(CHyprDropShadowDecoration*, Render::CRenderContext&, PHLMONITOR, const float&, const Render::SWindowRenderPresentation&);
        auto data = reinterpret_cast<Original>(shadowHook->m_original)(shadow, ctx, monitor, alpha, presentation);
        if (!data.valid || data.rounding <= 0.F)
            return data;
        const auto config = hyprionConfig();
        if (!config.enabled || !config.outset.enabled)
            return data;
        const auto owner = shadowWindows.find(shadow);
        if (owner == shadowWindows.end())
            return data;
        const auto window = owner->second.lock();
        if (!window || !validMapped(window))
            return data;
        // The native shadow box already includes our reserved outset, but its
        // radius only includes the native border. Expand both by the same amount.
        const float correction = (std::sqrt(2.F) - 1.F) * std::max(2.F - data.roundingPower, 0.F);
        data.rounding += config.outset.size * (1.F - correction);
        return data;
    }

    bool windowUrgent(const PHLWINDOW& window) {
        return window && (window->m_hints & Desktop::View::WINDOW_HINT_URGENT) != Desktop::View::WINDOW_HINT_NONE;
    }

    CHyprionDecoration* hyprionDecoration(const PHLWINDOW& window) {
        if (!window || !validMapped(window))
            return nullptr;
        for (const auto& decoration : window->presentation().decorations()) {
            if (auto* hyprion = dynamic_cast<CHyprionDecoration*>(decoration.get()))
                return hyprion;
        }
        return nullptr;
    }

    void renderLayerHook(Render::IHyprRenderer* self, Render::CRenderContext& ctx, PHLLS layer, PHLMONITOR monitor, const Time::steady_tp& time, bool popups,
                         bool lockscreen, bool workspaceScene) {
        using Original = void (*)(Render::IHyprRenderer*, Render::CRenderContext&, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool, bool);
        reinterpret_cast<Original>(gradeLayerHook->m_original)(self, ctx, layer, monitor, time, popups, lockscreen, workspaceScene);
        if (!pluginActive || popups || !layer || !monitor || !Hyprion::gradeProtocol)
            return;
        Hyprion::gradeProtocol->queue(ctx, layer, monitor);
    }

    void drawShadow(CHyprDropShadowDecoration* shadow, Render::CRenderContext& ctx, PHLMONITOR monitor, const float& alpha, const Render::SWindowRenderPresentation& presentation) {
        const auto owner = shadowWindows.find(shadow);
        if (owner != shadowWindows.end()) {
            if (auto* decoration = hyprionDecoration(owner->second.lock()))
                decoration->drawOutset(ctx, monitor, alpha, presentation);
        }
        using Original = void (*)(CHyprDropShadowDecoration*, Render::CRenderContext&, PHLMONITOR, const float&, const Render::SWindowRenderPresentation&);
        reinterpret_cast<Original>(shadowDrawHook->m_original)(shadow, ctx, monitor, alpha, presentation);
    }

    void onAnimationTimer(SP<CEventLoopTimer> timer, void*) {
        if (!pluginActive)
            return;

        const auto config = hyprionConfig();
        const auto now = CHyprionAnimation::Clock::now();
        const auto focused = Desktop::focusState()->window();
        bool running = false;
        for (const auto& window : Desktop::windowState()->windows()) {
            auto* decoration = hyprionDecoration(window);
            if (!decoration)
                continue;
            if (!config.enabled || (!config.inset.enabled && !config.outset.enabled))
                decoration->resetAnimation(window == focused, windowUrgent(window), now);
            else
                running = decoration->animate(config, now) || running;
        }
        timer->updateTimeout(running ? std::optional{ANIMATION_TICK} : std::nullopt);
    }

    void onFocusChanged(const PHLWINDOW& focusedWindow, const Desktop::eFocusReason&) {
        const auto previous = lastFocusedWindow.lock();
        if (previous == focusedWindow)
            return;
        lastFocusedWindow = focusedWindow;

        const auto config = hyprionConfig();
        const auto now = CHyprionAnimation::Clock::now();
        const bool effectsEnabled = config.enabled && (config.inset.enabled || config.outset.enabled);
        bool running = false;
        if (auto* decoration = hyprionDecoration(previous)) {
            if (effectsEnabled) {
                decoration->stateChanged(false, windowUrgent(previous), config, now);
                running = decoration->animate(config, now) || running;
            } else {
                decoration->resetAnimation(false, windowUrgent(previous), now);
            }
        }
        if (auto* decoration = hyprionDecoration(focusedWindow)) {
            if (effectsEnabled) {
                decoration->stateChanged(true, windowUrgent(focusedWindow), config, now);
                running = decoration->animate(config, now) || running;
            } else {
                decoration->resetAnimation(true, windowUrgent(focusedWindow), now);
            }
        }
        if (running && animationTimer)
            animationTimer->updateTimeout(ANIMATION_TICK);
    }

    void onUrgencyChanged(const PHLWINDOW& urgentWindow) {
        auto* decoration = hyprionDecoration(urgentWindow);
        if (!decoration)
            return;

        const auto config = hyprionConfig();
        const auto now = CHyprionAnimation::Clock::now();
        const bool focused = urgentWindow == Desktop::focusState()->window();
        if (!config.enabled || (!config.inset.enabled && !config.outset.enabled)) {
            decoration->resetAnimation(focused, windowUrgent(urgentWindow), now);
            return;
        }

        decoration->stateChanged(focused, windowUrgent(urgentWindow), config, now);
        if (animationTimer && decoration->animate(config, now))
            animationTimer->updateTimeout(ANIMATION_TICK);
    }

    void attachDecoration(const PHLWINDOW& window, bool animateFocus) {
        if (!window || !validMapped(window) || hyprionDecoration(window))
            return;

        for (const auto& deco : window->presentation().decorations()) {
            if (auto* shadow = dynamic_cast<CHyprDropShadowDecoration*>(deco.get()))
                shadowWindows.insert_or_assign(shadow, window);
        }
        HyprlandAPI::addWindowDecoration(pluginHandle, window, makeShared<CHyprionDecoration>(window));
        auto* decoration = hyprionDecoration(window);
        if (!decoration)
            return;

        const auto config = hyprionConfig();
        const auto focused = Desktop::focusState()->window();
        const bool isFocused = window == focused;
        const auto now = CHyprionAnimation::Clock::now();
        if (!config.enabled || (!config.inset.enabled && !config.outset.enabled)) {
            decoration->resetAnimation(isFocused, windowUrgent(window), now);
            return;
        }
        if (animateFocus && isFocused) {
            decoration->resetAnimation(false, false, now);
            decoration->stateChanged(true, windowUrgent(window), config, now);
        } else {
            decoration->stateChanged(isFocused, windowUrgent(window), config, now);
        }
        if (animationTimer && decoration->animate(config, now))
            animationTimer->updateTimeout(ANIMATION_TICK);
    }

    void onConfigReloaded() {
        invalidateHyprionConfig();
        if (animationTimer)
            animationTimer->updateTimeout(std::nullopt);
        const auto config = hyprionConfig();
        const auto now = CHyprionAnimation::Clock::now();
        const auto focused = Desktop::focusState()->window();
        lastFocusedWindow = focused;
        bool running = false;
        for (const auto& window : Desktop::windowState()->windows()) {
            if (auto* decoration = hyprionDecoration(window)) {
                decoration->updateWindow(window);
                if (!config.enabled || (!config.inset.enabled && !config.outset.enabled))
                    decoration->resetAnimation(window == focused, windowUrgent(window), now);
                else {
                    decoration->refreshAnimation(window == focused, windowUrgent(window), config, now);
                    running = decoration->animate(config, now) || running;
                }
            }
        }
        if (running && animationTimer)
            animationTimer->updateTimeout(ANIMATION_TICK);
    }

}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    pluginHandle = handle;
    pluginActive = false;

    const std::string serverHash = __hyprland_api_get_hash();
    const std::string clientHash = __hyprland_api_get_client_hash();
    if (serverHash != clientHash) {
        const std::string message = "hyprion: API mismatch (server=" + serverHash + ", client=" + clientHash + ")";
        HyprlandAPI::addNotification(handle, message, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error(message);
    }

    if (!registerHyprionConfig(handle))
        throw std::runtime_error("hyprion: failed to register configuration values");

    if (g_pHyprRenderer && g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL)
        throw std::runtime_error("hyprion requires Hyprland's OpenGL renderer");
    if (g_pHyprRenderer && !initializeHyprionRenderer())
        throw std::runtime_error("hyprion: failed to initialize the GPU sampling shader");
    if (g_pHyprRenderer && !initializeHyprionGradeRenderer())
        throw std::runtime_error("hyprion: failed to initialize the grade renderer");

    auto shadowMethods = HyprlandAPI::findFunctionsByName(handle, "getRenderData");
    std::erase_if(shadowMethods, [](const auto& match) { return !match.demangled.starts_with("CHyprDropShadowDecoration::getRenderData("); });
    if (shadowMethods.size() != 1)
        throw std::runtime_error("hyprion: cannot locate native shadow rendering");
    shadowHook = HyprlandAPI::createFunctionHook(handle, shadowMethods.front().address, reinterpret_cast<void*>(&shadowRenderData));
    if (!shadowHook || !shadowHook->hook())
        throw std::runtime_error("hyprion: cannot hook native shadow rendering");

    auto shadowDrawMethods = HyprlandAPI::findFunctionsByName(handle, "draw");
    std::erase_if(shadowDrawMethods, [](const auto& match) { return !match.demangled.starts_with("CHyprDropShadowDecoration::draw("); });
    if (shadowDrawMethods.size() != 1)
        throw std::runtime_error("hyprion: cannot locate native shadow pass scheduling");
    shadowDrawHook = HyprlandAPI::createFunctionHook(handle, shadowDrawMethods.front().address, reinterpret_cast<void*>(&drawShadow));
    if (!shadowDrawHook || !shadowDrawHook->hook())
        throw std::runtime_error("hyprion: cannot hook native shadow pass scheduling");

    auto layerMethods = HyprlandAPI::findFunctionsByName(handle, "renderLayer");
    std::erase_if(layerMethods, [](const auto& match) { return !match.demangled.starts_with("Render::IHyprRenderer::renderLayer("); });
    if (layerMethods.size() != 1)
        throw std::runtime_error("hyprion: cannot locate layer rendering");
    gradeLayerHook = HyprlandAPI::createFunctionHook(handle, layerMethods.front().address, reinterpret_cast<void*>(&renderLayerHook));
    if (!gradeLayerHook || !gradeLayerHook->hook())
        throw std::runtime_error("hyprion: cannot hook layer rendering");

    Hyprion::gradeProtocol = makeUnique<CHyprionGradeProtocol>();

    if (g_pEventLoopManager) {
        animationTimer = makeShared<CEventLoopTimer>(std::nullopt, onAnimationTimer, nullptr);
        g_pEventLoopManager->addTimer(animationTimer);
    }

    listeners.emplace_back(Event::bus()->m_events.window.open.listen([](const PHLWINDOW& window) { attachDecoration(window, true); }));
    listeners.emplace_back(Event::bus()->m_events.window.close.listen([](const PHLWINDOW& window) {
        std::erase_if(shadowWindows, [&](const auto& entry) { return entry.second.expired() || entry.second.lock() == window; });
    }));
    listeners.emplace_back(Event::bus()->m_events.window.active.listen(onFocusChanged));
    listeners.emplace_back(Event::bus()->m_events.window.urgent.listen(onUrgencyChanged));
    listeners.emplace_back(Event::bus()->m_events.monitor.destroyMon.listen([](const PHLMONITOR& monitor) {
        destroyHyprionMonitor(monitor);
        destroyHyprionGradeMonitor(monitor);
    }));
    listeners.emplace_back(Event::bus()->m_events.config.reloaded.listen(onConfigReloaded));
    listeners.emplace_back(Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
        if (!pluginActive || !monitor || !monitor->m_blurFBDirty || monitor->m_blurFBShouldRender || !hyprionConfig().outsetBlur)
            return;
        // Native preRender skips opaque windows. Hyprion is an independent
        // consumer, so request the same shared cache even without window blur.
        for (const auto& window : Desktop::windowState()->windows()) {
            if (!validMapped(window) || window->isHidden() || window->backend().traits().suggestsNoBorder ||
                !window->m_ruleApplicator->decorate().valueOrDefault() ||
                Fullscreen::controller()->getFullscreenModes(window).internal == Fullscreen::FSMODE_FULLSCREEN ||
                !g_pHyprRenderer->shouldRenderWindow(window, monitor))
                continue;
            monitor->m_blurFBShouldRender = true;
            g_pHyprRenderer->damageMonitor(monitor);
            break;
        }
    }));

    pluginActive = true;
    lastFocusedWindow = Desktop::focusState()->window();
    HyprlandAPI::reloadConfig();

    for (const auto& window : Desktop::windowState()->windows())
        attachDecoration(window, false);

    return {"hyprion", "Hyprion visual effects for Hyprland", "Suvikyi", "0.2.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    pluginActive = false;
    if (animationTimer) {
        animationTimer->updateTimeout(std::nullopt);
        if (g_pEventLoopManager)
            g_pEventLoopManager->removeTimer(animationTimer);
        animationTimer.reset();
    }

    listeners.clear();
    if (shadowDrawHook) {
        HyprlandAPI::removeFunctionHook(pluginHandle, shadowDrawHook);
        shadowDrawHook = nullptr;
    }
    if (shadowHook) {
        HyprlandAPI::removeFunctionHook(pluginHandle, shadowHook);
        shadowHook = nullptr;
    }
    if (gradeLayerHook) {
        HyprlandAPI::removeFunctionHook(pluginHandle, gradeLayerHook);
        gradeLayerHook = nullptr;
    }
    shadowWindows.clear();
    lastFocusedWindow = PHLWINDOWREF{};
    Hyprion::gradeProtocol.reset();
    if (g_pHyprRenderer)
        g_pHyprRenderer->context().m_pass.removeAllOfType("hyprion");
    destroyHyprionRenderer();
    destroyHyprionGradeRenderer();
    pluginHandle = nullptr;
}

