#pragma once
#include <array>
#include <vector>

// The shipped look lives in the HYPRION_*_DEFAULTS seeds below. These members
// are inert fallbacks, used only where a seed omits a field. The colorize fallback
// is Hyprland's own default glow blue (`decoration:glow:color`, `33ccffee`).
struct SHyprionColorStop {
    float r = 0.F;
    float g = 0.F;
    float b = 0.F;
    float a = 1.F;

    bool operator==(const SHyprionColorStop& other) const = default;
};

struct SHyprionGradient {
    // Stops are sRGB, matching Hyprland's gradient syntax. angle is radians.
    std::vector<SHyprionColorStop> colors;
    float                          angle = 0.F;

    bool operator==(const SHyprionGradient& other) const = default;
};

struct SHyprionColorizeConfig {
    SHyprionGradient gradient = {.colors = {{51.F / 255.F, 204.F / 255.F, 1.F, 1.F}}};
    float            strength = 0.F;
};

struct SHyprionGradeStyle {
    float lightness = 0.F;
    float saturation = 1.F;
    float chroma = 0.F;
    float hue = 0.F;
    SHyprionColorizeConfig colorize;
};

struct SHyprionStateConfig {
    SHyprionGradeStyle grade;
    float               opacity = 1.F;
};

struct SHyprionStyle {
    SHyprionGradeStyle grade;
    float               opacity = 1.F;
};

struct SHyprionMotionConfig {
    float amount = 0.F;
    int cycles = 1;
};

struct SHyprionOrbitProfile {
    bool                enabled = false;
    bool                clockwise = true;
    // Structural default: the animation divides by this, so it must stay positive.
    float               timeMs = 12000.F;
    float               length = 0.F;
    SHyprionGradeStyle  grade;
    float               opacity = 1.F;
    SHyprionMotionConfig motion;
    // Flare only: presence ramps in over fadeInMs and out over fadeOutMs, both
    // measured from the ends of the single ignition lap.
    float               fadeInMs = 0.F;
    float               fadeOutMs = 0.F;
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
    float              timeMs = 12000.F;
    float              length = 0.F;
    SHyprionGradeStyle grade;
    float              opacity = 1.F;
    float              phase = 0.F;
    float              amount = 0.F;
};

struct SHyprionPulseProfile {
    bool               enabled = false;
    float              timeMs = 333.F;
    float              intervalMs = 0.F;
    SHyprionGradeStyle grade;
    float              opacity = 1.F;
};

struct SHyprionPulseConfig {
    SHyprionPulseProfile active;
    SHyprionPulseProfile inactive;
    SHyprionPulseProfile urgent;
};

struct SHyprionBorderConfig {
    bool                 enabled = false;
    int                  size = 1;
    SHyprionStateConfig  active;
    SHyprionStateConfig  inactive;
    SHyprionPulseConfig  pulse;
    SHyprionOrbitConfig  orbit;
};

// Shipped defaults, one set per placement. These are the source of truth for
// every `plugin.hyprion.border.*` default: the registration reads them directly
// and a partially missing config falls back to them. `inset` is a translucent
// 1px glaze; `outset` is a 2px blurred one that colorizes toward the accent.
inline const SHyprionBorderConfig HYPRION_INSET_DEFAULTS = {
    .enabled = false,
    .size = 1,
    .active = {
        .grade = {
            .lightness = 0.175F,
            .saturation = 1.F,
            .chroma = 0.05F,
            .hue = 0.F,
            .colorize = {.strength = 0.F},
        },
    },
    .inactive = {
        .grade = {
            .lightness = 0.12F,
            .saturation = 1.F,
            .chroma = 0.05F,
            .hue = 0.F,
            .colorize = {.strength = 0.F},
        },
    },
    .pulse = {
        .active = {
            .enabled = true,
            .timeMs = 333.F,
            .grade = {
                .lightness = 0.45F,
                .saturation = 1.F,
                .chroma = 0.1F,
                .hue = 0.F,
                .colorize = {.strength = 0.F},
            },
        },
    },
    .orbit = {
        .active = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .grade = {
                .lightness = 0.309F,
                .saturation = 1.F,
                .chroma = 0.0618F,
                .hue = 0.F,
                .colorize = {.strength = 0.F},
            },
            .motion = {.amount = 0.75F, .cycles = 2},
        },
        .urgent = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 3000.F,
            .length = 0.154F,
            .grade = {
                .lightness = 0.25F,
                .saturation = 1.F,
                .chroma = 0.025F,
                .hue = 0.F,
                .colorize = {.strength = 0.F},
            },
            .motion = {.amount = 0.75F, .cycles = 2},
        },
        .inactive = {
            .enabled = false,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .grade = {
                .lightness = 0.309F,
                .saturation = 1.F,
                .chroma = 0.0618F,
                .hue = 0.F,
                .colorize = {.strength = 0.F},
            },
            .motion = {.amount = 0.75F, .cycles = 2},
        },
        .flare = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 667.F,
            .length = 0.309F,
            .grade = {
                .lightness = 0.375F,
                .saturation = 1.F,
                .chroma = 0.075F,
                .hue = 0.F,
                .colorize = {.strength = 0.F},
            },
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
        .grade = {
            .lightness = 0.34F,
            .saturation = 1.F,
            .chroma = 0.0425F,
            .hue = 0.F,
            .colorize = {.strength = 0.85F},
        },
    },
    .inactive = {
        .grade = {
            .lightness = 0.16875F,
            .saturation = 1.F,
            .chroma = 0.0375F,
            .hue = 0.F,
            .colorize = {.strength = 0.75F},
        },
    },
    .pulse = {
        .active = {
            .enabled = true,
            .timeMs = 367.F,
            .grade = {
                .lightness = 0.375F,
                .saturation = 1.F,
                .chroma = 0.05F,
                .hue = 0.F,
                .colorize = {.strength = 1.F},
            },
        },
    },
    .orbit = {
        .active = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .grade = {
                .lightness = 0.425F,
                .saturation = 1.F,
                .chroma = 0.05F,
                .hue = 0.F,
                .colorize = {.strength = 1.F},
            },
            .motion = {.amount = 0.5F, .cycles = 2},
        },
        .urgent = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 3000.F,
            .length = 0.154F,
            .grade = {
                .lightness = 0.425F,
                .saturation = 1.F,
                .chroma = 0.05F,
                .hue = 0.F,
                .colorize = {.strength = 1.F},
            },
            .motion = {.amount = 0.5F, .cycles = 2},
        },
        .inactive = {
            .enabled = false,
            .clockwise = true,
            .timeMs = 30000.F,
            .length = 0.154F,
            .grade = {
                .lightness = 0.425F,
                .saturation = 1.F,
                .chroma = 0.05F,
                .hue = 0.F,
                .colorize = {.strength = 1.F},
            },
            .motion = {.amount = 0.5F, .cycles = 2},
        },
        .flare = {
            .enabled = true,
            .clockwise = true,
            .timeMs = 667.F,
            .length = 0.309F,
            .grade = {
                .lightness = 0.475F,
                .saturation = 1.F,
                .chroma = 0.1F,
                .hue = 0.F,
                .colorize = {.strength = 1.F},
            },
            .motion = {.amount = 0.618F, .cycles = 2},
            .fadeInMs = 0.F,
            .fadeOutMs = 0.F,
        },
    },
};

struct SHyprionFrameStyle {
    SHyprionStyle      border;
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
