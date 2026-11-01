#include "HyprionHooks.hpp"

#include "HyprionConfig.hpp"
#include "HyprionDecoration.hpp"
#include "HyprionGradeProtocol.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/CHyprDropShadowDecoration.hpp>

using namespace Hyprutils::Memory;

namespace {
    bool                                                          active = false;
    HANDLE                                                        hookHandle = nullptr;
    CFunctionHook*                                                shadowHook = nullptr;
    CFunctionHook*                                                shadowDrawHook = nullptr;
    CFunctionHook*                                                gradeLayerHook = nullptr;
    std::unordered_map<CHyprDropShadowDecoration*, PHLWINDOWREF> shadowWindows;

    CHyprionDecoration* findDecoration(const PHLWINDOW& window) {
        if (!window || !validMapped(window))
            return nullptr;
        for (const auto& decoration : window->presentation().decorations()) {
            if (auto* hyprion = dynamic_cast<CHyprionDecoration*>(decoration.get()))
                return hyprion;
        }
        return nullptr;
    }

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
        const float correction = (std::sqrt(2.F) - 1.F) * std::max(2.F - data.roundingPower, 0.F);
        data.rounding += config.outset.size * (1.F - correction);
        return data;
    }

    void drawShadow(CHyprDropShadowDecoration* shadow, Render::CRenderContext& ctx, PHLMONITOR monitor, const float& alpha, const Render::SWindowRenderPresentation& presentation) {
        const auto owner = shadowWindows.find(shadow);
        if (owner != shadowWindows.end()) {
            if (auto* decoration = findDecoration(owner->second.lock()))
                decoration->drawOutset(ctx, monitor, alpha, presentation);
        }
        using Original = void (*)(CHyprDropShadowDecoration*, Render::CRenderContext&, PHLMONITOR, const float&, const Render::SWindowRenderPresentation&);
        reinterpret_cast<Original>(shadowDrawHook->m_original)(shadow, ctx, monitor, alpha, presentation);
    }

    void renderLayerHook(Render::IHyprRenderer* self, Render::CRenderContext& ctx, PHLLS layer, PHLMONITOR monitor, const Time::steady_tp& time, bool popups,
                         bool lockscreen, bool workspaceScene) {
        using Original = void (*)(Render::IHyprRenderer*, Render::CRenderContext&, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool, bool);
        reinterpret_cast<Original>(gradeLayerHook->m_original)(self, ctx, layer, monitor, time, popups, lockscreen, workspaceScene);
        if (!active || popups || !layer || !monitor || !Hyprion::gradeProtocol)
            return;
        Hyprion::gradeProtocol->queue(ctx, layer, monitor);
    }
}

namespace Hyprion::Hooks {
    bool install(HANDLE handle) {
        hookHandle = handle;
        auto shadowMethods = HyprlandAPI::findFunctionsByName(handle, "getRenderData");
        std::erase_if(shadowMethods, [](const auto& match) { return !match.demangled.starts_with("CHyprDropShadowDecoration::getRenderData("); });
        if (shadowMethods.size() != 1)
            return false;
        shadowHook = HyprlandAPI::createFunctionHook(handle, shadowMethods.front().address, reinterpret_cast<void*>(&shadowRenderData));
        if (!shadowHook || !shadowHook->hook()) {
            uninstall();
            return false;
        }

        auto shadowDrawMethods = HyprlandAPI::findFunctionsByName(handle, "draw");
        std::erase_if(shadowDrawMethods, [](const auto& match) { return !match.demangled.starts_with("CHyprDropShadowDecoration::draw("); });
        if (shadowDrawMethods.size() != 1)
            return false;
        shadowDrawHook = HyprlandAPI::createFunctionHook(handle, shadowDrawMethods.front().address, reinterpret_cast<void*>(&drawShadow));
        if (!shadowDrawHook || !shadowDrawHook->hook()) {
            uninstall();
            return false;
        }

        auto layerMethods = HyprlandAPI::findFunctionsByName(handle, "renderLayer");
        std::erase_if(layerMethods, [](const auto& match) { return !match.demangled.starts_with("Render::IHyprRenderer::renderLayer("); });
        if (layerMethods.size() != 1)
            return false;
        gradeLayerHook = HyprlandAPI::createFunctionHook(handle, layerMethods.front().address, reinterpret_cast<void*>(&renderLayerHook));
        if (!gradeLayerHook || !gradeLayerHook->hook()) {
            uninstall();
            return false;
        }

        return true;
    }

    void uninstall() {
        active = false;
        if (shadowDrawHook) {
            HyprlandAPI::removeFunctionHook(hookHandle, shadowDrawHook);
            shadowDrawHook = nullptr;
        }
        if (shadowHook) {
            HyprlandAPI::removeFunctionHook(hookHandle, shadowHook);
            shadowHook = nullptr;
        }
        if (gradeLayerHook) {
            HyprlandAPI::removeFunctionHook(hookHandle, gradeLayerHook);
            gradeLayerHook = nullptr;
        }
        shadowWindows.clear();
        hookHandle = nullptr;
    }

    void setActive(bool value) {
        active = value;
    }

    CHyprionDecoration* findDecoration(const PHLWINDOW& window) {
        return ::findDecoration(window);
    }

    void registerShadowWindow(CHyprDropShadowDecoration* shadow, const PHLWINDOW& window) {
        if (shadow)
            shadowWindows.insert_or_assign(shadow, window);
    }

    void unregisterWindow(const PHLWINDOW& window) {
        std::erase_if(shadowWindows, [&](const auto& entry) { return entry.second.expired() || entry.second.lock() == window; });
    }
}
