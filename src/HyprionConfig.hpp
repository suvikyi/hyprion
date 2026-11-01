#pragma once
#include <array>

// The shipped look lives in the HYPRION_*_DEFAULTS seeds below. These members
// are inert fallbacks, used only where a seed omits a field. The tint fallback
// is Hyprland's own default glow blue (`decoration:glow:color`, `33ccffee`).
struct SHyprionTintConfig {
    std::array<float, 3> color = {51.F / 255.F, 204.F / 255.F, 1.F};
    float strength = 0.F;
};

struct SHyprionStateConfig {
    float lightness  = 0.F;
    float saturation = 1.F;
    float chroma     = 0.F;
    float hue        = 0.F;
    // Strength is the transform amount: 1 is the transform as configured, larger
    // values overshoot past it. Opacity is how much of the border covers what is
    // beneath it, so it is the one that fades the frost.
    float strength   = 0.F;
    float opacity    = 1.F;
    SHyprionTintConfig tint;
};

struct SHyprionStyle {
    float lightness  = 0.F;
    float saturation = 1.F;
    float chroma     = 0.F;
    float hue        = 0.F;
    float strength   = 0.F;
    float opacity    = 1.F;
    std::array<float, 3> tintColor = {51.F / 255.F, 204.F / 255.F, 1.F};
    float tintStrength = 0.F;
};

struct SHyprionMotionConfig {
    float amount = 0.F;
    int cycles = 1;
};

struct SHyprionOrbitProfile {
    bool enabled = false;
    bool clockwise = true;
    // Structural default: the animation divides by this, so it must stay positive.
    float timeMs = 12000.F;
    float length = 0.F;
    float lightness = 0.F;
    float saturation = 1.F;
    float chroma = 0.F;
    float hue = 0.F;
    float strength = 0.F;
    float opacity = 1.F;
    SHyprionTintConfig tint;
    SHyprionMotionConfig motion;
    // Flare only: presence ramps in over fadeInMs and out over fadeOutMs, both
    // measured from the ends of the single ignition lap.
    float fadeInMs = 0.F;
    float fadeOutMs = 0.F;
};

struct SHyprionOrbitConfig {
    SHyprionOrbitProfile active;
    SHyprionOrbitProfile urgent;
    SHyprionOrbitProfile inactive;
    // One-shot ignition: on focus gain the orbit fades in over `fade.start`,
    // laps exactly once at this profile's speed, and fades out over `fade.finish`
    // (anchored to the end of that lap), then hands back to the focused state's
    // profile — or stays gone if that profile is disabled.
    SHyprionOrbitProfile flare;
};

struct SHyprionOrbitStyle {
    float timeMs = 12000.F;
    float length = 0.F;
    float lightness = 0.F;
    float saturation = 1.F;
    float chroma = 0.F;
    float hue = 0.F;
    float strength = 0.F;
    float opacity = 1.F;
    float phase = 0.F;
    float amount = 0.F;
    std::array<float, 3> tintColor = {51.F / 255.F, 204.F / 255.F, 1.F};
    float tintStrength = 0.F;
};

struct SHyprionPulseConfig {
    bool enabled = false;
    float timeMs = 333.F;
    float lightness = 0.F;
    float saturation = 1.F;
    float chroma = 0.F;
    float hue = 0.F;
    float strength = 0.F;
    float opacity = 1.F;
    SHyprionTintConfig tint;
};

struct SHyprionBorderConfig {
    bool enabled = false;
    int size = 1;
    SHyprionStateConfig active;
    SHyprionStateConfig inactive;
    SHyprionPulseConfig pulse;
    SHyprionOrbitConfig orbit;
};

// Shipped defaults, one set per placement. These are the source of truth for
// every `plugin.hyprion.border.*` default: the registration reads them directly
// and a partially missing config falls back to them. `inset` is a translucent
// 1px glaze; `outset` is a 2px blurred one that tints toward the accent.
inline const SHyprionBorderConfig HYPRION_INSET_DEFAULTS = {
    .enabled = false,
    .size = 1,
    .active = {
        .lightness = 0.175F,
        .saturation = 1.F,
        .chroma = 0.05F,
        .hue = 0.F,
        .strength = 1.F,
        .tint = {.strength = 0.F},
    },
    .inactive = {
        .lightness = 0.12F,
        .saturation = 1.F,
        .chroma = 0.05F,
        .hue = 0.F,
        .strength = 1.F,
        .tint = {.strength = 0.F},
    },
    .pulse = {
        .enabled = true,
        .timeMs = 333.F,
        .lightness = 0.45F,
        .saturation = 1.F,
        .chroma = 0.1F,
        .hue = 0.F,
        .strength = 1.F,
        .tint = {.strength = 0.F},
    },
    .orbit = {
        .active = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .lightness = 0.5F,
            .saturation = 1.F,
            .chroma = 0.1F,
            .hue = 0.F,
            .strength = 0.618F,
            .tint = {.strength = 0.F},
            .motion = {.amount = 0.75F, .cycles = 2},
        },
        .urgent = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 3000.F,
            .length = 0.154F,
            .lightness = 0.5F,
            .saturation = 1.F,
            .chroma = 0.05F,
            .hue = 0.F,
            .strength = 0.5F,
            .tint = {.strength = 0.F},
            .motion = {.amount = 0.75F, .cycles = 2},
        },
        .inactive = {
            .enabled = false,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .lightness = 0.5F,
            .saturation = 1.F,
            .chroma = 0.1F,
            .hue = 0.F,
            .strength = 0.618F,
            .tint = {.strength = 0.F},
            .motion = {.amount = 0.75F, .cycles = 2},
        },
        .flare = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 667.F,
            .length = 0.309F,
            .lightness = 0.5F,
            .saturation = 1.F,
            .chroma = 0.1F,
            .hue = 0.F,
            .strength = 0.75F,
            .tint = {.strength = 0.F},
            .motion = {.amount = 0.75F, .cycles = 2},
            .fadeInMs = 0.F,
            .fadeOutMs = 0.F,
        },
    },
};

inline const SHyprionBorderConfig HYPRION_OUTSET_DEFAULTS = {
    .enabled = true,
    .size = 2,
    .active = {
        .lightness = 0.4F,
        .saturation = 1.F,
        .chroma = 0.05F,
        .hue = 0.F,
        .strength = 0.85F,
        .tint = {.strength = 1.F},
    },
    .inactive = {
        .lightness = 0.225F,
        .saturation = 1.F,
        .chroma = 0.05F,
        .hue = 0.F,
        .strength = 0.75F,
        .tint = {.strength = 1.F},
    },
    .pulse = {
        .enabled = true,
        .timeMs = 367.F,
        .lightness = 0.375F,
        .saturation = 1.F,
        .chroma = 0.05F,
        .hue = 0.F,
        .strength = 1.F,
        .tint = {.strength = 1.F},
    },
    .orbit = {
        .active = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .lightness = 0.425F,
            .saturation = 1.F,
            .chroma = 0.05F,
            .hue = 0.F,
            .strength = 1.F,
            .tint = {.strength = 1.F},
            .motion = {.amount = 0.5F, .cycles = 2},
        },
        .urgent = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 3000.F,
            .length = 0.154F,
            .lightness = 0.425F,
            .saturation = 1.F,
            .chroma = 0.05F,
            .hue = 0.F,
            .strength = 1.F,
            .tint = {.strength = 1.F},
            .motion = {.amount = 0.5F, .cycles = 2},
        },
        .inactive = {
            .enabled = false,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .lightness = 0.425F,
            .saturation = 1.F,
            .chroma = 0.05F,
            .hue = 0.F,
            .strength = 1.F,
            .tint = {.strength = 1.F},
            .motion = {.amount = 0.5F, .cycles = 2},
        },
        .flare = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 667.F,
            .length = 0.309F,
            .lightness = 0.475F,
            .saturation = 1.F,
            .chroma = 0.1F,
            .hue = 0.F,
            .strength = 1.F,
            .tint = {.strength = 1.F},
            .motion = {.amount = 0.618F, .cycles = 2},
            .fadeInMs = 0.F,
            .fadeOutMs = 0.F,
        },
    },
};

struct SHyprionFrameStyle {
    SHyprionStyle border;
    SHyprionOrbitStyle orbit;
};

struct SHyprionConfig {
    bool                enabled = true;
    SHyprionBorderConfig inset = HYPRION_INSET_DEFAULTS;
    SHyprionBorderConfig outset = HYPRION_OUTSET_DEFAULTS;
    bool                outsetBlur = true;
};

bool         registerHyprionConfig(void* handle);
void         invalidateHyprionConfig();
SHyprionConfig hyprionConfig();
