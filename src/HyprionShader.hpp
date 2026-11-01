#pragma once

#include "HyprionConfig.hpp"

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>

// A single inset glaze ring, in monitor-local logical coordinates. The
// renderer scales and transforms it like the window-decoration path.
struct SHyprionGlazeParams {
    CBox          box;
    float         thickness     = 0.F;
    float         radius        = 0.F;
    float         roundingPower = 2.F;
    SHyprionStyle style;
    float         alpha = 1.F;
};

bool hyprionDrawInsetGlaze(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionGlazeParams& params);
