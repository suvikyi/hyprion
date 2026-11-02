#include "HyprionHooks.hpp"

#include "HyprionConfig.hpp"
#include "HyprionDecoration.hpp"
#include "HyprionGradeProtocol.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <stdexcept>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/CHyprDropShadowDecoration.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>

using namespace Hyprutils::Memory;

namespace {
    bool                                                          active = false;
    HANDLE                                                        hookHandle = nullptr;
    CFunctionHook*                                                shadowHook = nullptr;
    CFunctionHook*                                                shadowDrawHook = nullptr;
    CFunctionHook*                                                gradePassHook = nullptr;
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
        auto* decoration = owner != shadowWindows.end() ? findDecoration(owner->second.lock()) : nullptr;
        if (decoration)
            decoration->drawOutset(ctx, monitor, alpha, presentation, true);
        using Original = void (*)(CHyprDropShadowDecoration*, Render::CRenderContext&, PHLMONITOR, const float&, const Render::SWindowRenderPresentation&);
        reinterpret_cast<Original>(shadowDrawHook->m_original)(shadow, ctx, monitor, alpha, presentation);
        // Retain the pre-shadow source, but composite the band before the
        // window. The window's own rounded coverage must occlude the outset's
        // inner antialiasing fringe, not be replaced by backdrop pixels.
        if (decoration)
            decoration->drawOutset(ctx, monitor, alpha, presentation, false);
    }

    void addPassHook(Render::CRenderPass* pass, UP<IPassElement>&& element) {
        auto* surface = element && element->type() == EK_SURFACE ? static_cast<CSurfacePassElement*>(element.get()) : nullptr;
        using Original = void (*)(Render::CRenderPass*, UP<IPassElement>&&);
        reinterpret_cast<Original>(gradePassHook->m_original)(pass, std::move(element));
        // Insert only after an actual surface pass, so visibility/capture policy
        // and the scene's sibling/subsurface order remain Hyprland's decision.
        if (active && surface && Hyprion::gradeProtocol)
            Hyprion::gradeProtocol->queue(*pass, *surface);
    }
}

namespace Hyprion::Hooks {
    bool install(HANDLE handle) {
        hookHandle = handle;
        auto shadowMethods = HyprlandAPI::findFunctionsByName(handle, "getRenderData");
        std::erase_if(shadowMethods, [](const auto& match) { return !match.demangled.starts_with("CHyprDropShadowDecoration::getRenderData("); });
        if (shadowMethods.size() != 1)
            throw std::runtime_error("hyprion: ambiguous or missing shadow getRenderData hook");
        shadowHook = HyprlandAPI::createFunctionHook(handle, shadowMethods.front().address, reinterpret_cast<void*>(&shadowRenderData));
        if (!shadowHook || !shadowHook->hook()) {
            uninstall();
            throw std::runtime_error("hyprion: could not hook shadow getRenderData");
        }

        auto shadowDrawMethods = HyprlandAPI::findFunctionsByName(handle, "draw");
        std::erase_if(shadowDrawMethods, [](const auto& match) { return !match.demangled.starts_with("CHyprDropShadowDecoration::draw("); });
        if (shadowDrawMethods.size() != 1) {
            uninstall();
            throw std::runtime_error("hyprion: ambiguous or missing shadow draw hook");
        }
        shadowDrawHook = HyprlandAPI::createFunctionHook(handle, shadowDrawMethods.front().address, reinterpret_cast<void*>(&drawShadow));
        if (!shadowDrawHook || !shadowDrawHook->hook()) {
            uninstall();
            throw std::runtime_error("hyprion: could not hook shadow draw");
        }

        auto passMethods = HyprlandAPI::findFunctionsByName(handle, "CRenderPass3add");
        std::erase_if(passMethods, [](const auto& match) { return !match.demangled.starts_with("Render::CRenderPass::add("); });
        if (passMethods.size() != 1) {
            uninstall();
            throw std::runtime_error("hyprion: ambiguous or missing render pass add hook");
        }
        gradePassHook = HyprlandAPI::createFunctionHook(handle, passMethods.front().address, reinterpret_cast<void*>(&addPassHook));
        if (!gradePassHook || !gradePassHook->hook()) {
            uninstall();
            throw std::runtime_error("hyprion: could not hook render pass add");
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
        if (gradePassHook) {
            HyprlandAPI::removeFunctionHook(hookHandle, gradePassHook);
            gradePassHook = nullptr;
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
