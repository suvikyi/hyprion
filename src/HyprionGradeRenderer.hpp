#pragma once

#include <cstdint>
#include "HyprionConfig.hpp"

#include <array>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Context.hpp>

enum class SHyprionGradeArea : uint8_t {
    INSET = 0,
    OUTSET,
    FILL,
};

enum class SHyprionGradeMode : uint8_t {
    IN_PLACE = 1,
    OVER     = 2,
};

enum class SHyprionGradeSource : uint8_t {
    CURRENT_FRAMEBUFFER = 0,
    BLUR_CACHE,
    LIVE_BLUR,
};

struct SHyprionSnapshotTile {
    SP<Render::IFramebuffer> framebuffer;
    CBox sourceBox;
};

struct SHyprionGradeSnapshot {
    std::vector<SHyprionSnapshotTile> tiles;
};

// One grade draw in monitor-local coordinates. Geometry that depends on the
// window or protocol surface is resolved by the caller; the renderer owns
// snapshots, colour management, uniforms, and the actual draw.
struct SHyprionGradeRequest {
    CBox                  renderBox;
    SHyprionGradeArea     area = SHyprionGradeArea::INSET;
    SHyprionGradeMode     mode = SHyprionGradeMode::IN_PLACE;
    SHyprionGradeSource   source = SHyprionGradeSource::CURRENT_FRAMEBUFFER;
    int                   thickness = 0;
    int                   rounding = 0;
    int                   innerRounding = 0;
    int                   snapshotExtent = 0;
    float                 roundingPower = 2.F;
    SHyprionStyle         style;
    SHyprionOrbitStyle    orbit;
    std::array<float, 65> cornerArcTable{};
    float                 alpha = 1.F;
    bool                  fullSnapshot = false;
    bool                  snapshotOnly = false;
    const SHyprionGradeSnapshot* snapshot = nullptr;
    bool                  clipToDamageBox = false;
    std::optional<CBox> clipBox;
};

// Regions are in the same pixel coordinate space as renderBox and render damage.
CRegion hyprionGradeRegion(const SHyprionGradeRequest& request);
CRegion hyprionGradeFramebufferRegion(Render::CRenderContext& ctx, const CRegion& region);
bool hyprionCaptureGradeSnapshot(Render::CRenderContext& ctx, PHLMONITOR monitor, SHyprionGradeSnapshot& snapshot, const CRegion& region);
bool initializeHyprionGradeRenderer();
void destroyHyprionGradeRenderer();
void destroyHyprionGradeMonitor(PHLMONITOR monitor);
bool hyprionDrawGrade(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionGradeRequest& request);
