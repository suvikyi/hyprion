#pragma once

#include "HyprionConfig.hpp"

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>

enum class SHyprionGradeArea : uint8_t {
    INSET = 0,
    OUTSET,
    FILL,
};

// One grade area in monitor-local logical coordinates. The renderer scales and
// transforms it like the window-decoration path, but samples a full box
// snapshot so fill and outset areas work as well as inset bands.
struct SHyprionGradeParams {
    CBox               box;
    SHyprionGradeArea  area = SHyprionGradeArea::INSET;
    float              thickness = 0.F;
    float              radius = 0.F;
    float              roundingPower = 2.F;
    SHyprionStyle      style;
    float              alpha = 1.F;
};

bool initializeHyprionGradeRenderer();
void destroyHyprionGradeRenderer();
bool hyprionDrawGrade(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionGradeParams& params);
