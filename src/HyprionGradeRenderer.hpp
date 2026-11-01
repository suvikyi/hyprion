#pragma once

#include <array>
#include "HyprionConfig.hpp"

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>

enum class SHyprionGradeArea : uint8_t {
    INSET = 0,
    OUTSET,
    FILL,
};

enum class SHyprionComposite : uint8_t {
    REPLACE = 1,
    OVER = 2,
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
    SHyprionComposite  composite = SHyprionComposite::REPLACE;
};

struct SHyprionStripParams {
    CBox               box;
    bool               outset = false;
    bool               snapshotOnly = false;
    bool               sharedBlur = false;
    bool               blurRequested = false;
    int                thickness = 0;
    float              rounding = 0.F;
    float              roundingPower = 2.F;
    SHyprionStyle      style;
    SHyprionOrbitStyle orbit;
    std::array<float, 65> orbitArcTable{};
    float              alpha = 1.F;
    SHyprionComposite  composite = SHyprionComposite::REPLACE;
};

bool initializeHyprionGradeRenderer();
void destroyHyprionGradeRenderer();
void destroyHyprionGradeMonitor(PHLMONITOR monitor);
bool hyprionDrawGrade(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionGradeParams& params);
bool hyprionDrawStrips(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionStripParams& params);
