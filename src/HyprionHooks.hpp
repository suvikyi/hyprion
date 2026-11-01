#pragma once

#include <hyprland/src/defines.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

class CHyprDropShadowDecoration;
class CHyprionDecoration;

namespace Hyprion::Hooks {
    bool                  install(HANDLE handle);
    void                  uninstall();
    void                  setActive(bool active);
    CHyprionDecoration*   findDecoration(const PHLWINDOW& window);
    void                  registerShadowWindow(CHyprDropShadowDecoration* shadow, const PHLWINDOW& window);
    void                  unregisterWindow(const PHLWINDOW& window);
}
