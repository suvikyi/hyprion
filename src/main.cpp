#define WLR_USE_UNSTABLE

#include "HyprionConfig.hpp"
#include "HyprionDecoration.hpp"
#include "HyprionGradeProtocol.hpp"
#include "HyprionHooks.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>

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
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace {
    using namespace Hyprutils::Memory;

    constexpr auto ANIMATION_TICK = std::chrono::milliseconds(16);

    HANDLE                            pluginHandle = nullptr;
    bool                              pluginActive = false;
    std::vector<CHyprSignalListener>   listeners;
    SP<CEventLoopTimer>               animationTimer;
    PHLWINDOWREF                      lastFocusedWindow;
    bool windowUrgent(const PHLWINDOW& window) {
        return window && (window->m_hints & Desktop::View::WINDOW_HINT_URGENT) != Desktop::View::WINDOW_HINT_NONE;
    }

    void onAnimationTimer(SP<CEventLoopTimer> timer, void*) {
        if (!pluginActive)
            return;

        const auto config = hyprionConfig();
        const auto now = CHyprionAnimation::Clock::now();
        const auto focused = Desktop::focusState()->window();
        bool running = false;
        for (const auto& window : Desktop::windowState()->windows()) {
            auto* decoration = Hyprion::Hooks::findDecoration(window);
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
        if (auto* decoration = Hyprion::Hooks::findDecoration(previous)) {
            if (effectsEnabled) {
                decoration->stateChanged(false, windowUrgent(previous), config, now);
                running = decoration->animate(config, now) || running;
            } else {
                decoration->resetAnimation(false, windowUrgent(previous), now);
            }
        }
        if (auto* decoration = Hyprion::Hooks::findDecoration(focusedWindow)) {
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
        auto* decoration = Hyprion::Hooks::findDecoration(urgentWindow);
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
        if (!window || !validMapped(window) || Hyprion::Hooks::findDecoration(window))
            return;

        for (const auto& deco : window->presentation().decorations()) {
            if (auto* shadow = dynamic_cast<CHyprDropShadowDecoration*>(deco.get()))
                Hyprion::Hooks::registerShadowWindow(shadow, window);
        }
        HyprlandAPI::addWindowDecoration(pluginHandle, window, makeShared<CHyprionDecoration>(window));
        auto* decoration = Hyprion::Hooks::findDecoration(window);
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
            if (auto* decoration = Hyprion::Hooks::findDecoration(window)) {
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
    if (g_pHyprRenderer && !initializeHyprionGradeRenderer())
        throw std::runtime_error("hyprion: failed to initialize the grade renderer");

    if (!Hyprion::Hooks::install(handle))
        throw std::runtime_error("hyprion: failed to install Hyprland hooks");

    Hyprion::gradeProtocol = makeUnique<CHyprionGradeProtocol>();

    if (g_pEventLoopManager) {
        animationTimer = makeShared<CEventLoopTimer>(std::nullopt, onAnimationTimer, nullptr);
        g_pEventLoopManager->addTimer(animationTimer);
    }

    listeners.emplace_back(Event::bus()->m_events.window.open.listen([](const PHLWINDOW& window) { attachDecoration(window, true); }));
    listeners.emplace_back(Event::bus()->m_events.window.close.listen([](const PHLWINDOW& window) {
        Hyprion::Hooks::unregisterWindow(window);
    }));
    listeners.emplace_back(Event::bus()->m_events.window.active.listen(onFocusChanged));
    listeners.emplace_back(Event::bus()->m_events.window.urgent.listen(onUrgencyChanged));
    listeners.emplace_back(Event::bus()->m_events.monitor.destroyMon.listen([](const PHLMONITOR& monitor) {
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
    Hyprion::Hooks::setActive(true);
    lastFocusedWindow = Desktop::focusState()->window();
    HyprlandAPI::reloadConfig();

    for (const auto& window : Desktop::windowState()->windows())
        attachDecoration(window, false);

    return {"hyprion", "Hyprion visual effects for Hyprland", "Suvikyi", "0.4.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    pluginActive = false;
    Hyprion::Hooks::setActive(false);
    if (animationTimer) {
        animationTimer->updateTimeout(std::nullopt);
        if (g_pEventLoopManager)
            g_pEventLoopManager->removeTimer(animationTimer);
        animationTimer.reset();
    }

    listeners.clear();
    Hyprion::Hooks::uninstall();
    lastFocusedWindow = PHLWINDOWREF{};
    if (g_pHyprRenderer) {
        g_pHyprRenderer->context().m_pass.removeAllOfType("hyprion-grade");
        g_pHyprRenderer->context().m_pass.removeAllOfType("hyprion");
    }
    Hyprion::gradeProtocol.reset();
    destroyHyprionGradeRenderer();
    pluginHandle = nullptr;
}

