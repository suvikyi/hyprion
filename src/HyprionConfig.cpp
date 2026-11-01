#include "HyprionConfig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>

namespace {
    using Config::Values::CBoolValue;
    using Config::Values::CColorValue;
    using Config::Values::CFloatValue;
    using Config::Values::CIntValue;

    struct STintConfigValues {
        SP<CColorValue> color;
        SP<CFloatValue> strength;
    };

    struct STintConfigNames {
        std::string color;
        std::string strength;
    };
    struct SOrbitConfigNames {
        std::string enabled;
        std::string clockwise;
        std::string time;
        std::string length;
        std::string lightness;
        std::string saturation;
        std::string chroma;
        std::string hue;
        std::string strength;
        std::string opacity;
        STintConfigNames tint;
        std::string motionAmount;
        std::string motionCycles;
    };

    struct SOrbitConfigValues {
        SP<CBoolValue> enabled;
        SP<CBoolValue> clockwise;
        SP<CFloatValue> time;
        SP<CFloatValue> length;
        SP<CFloatValue> lightness;
        SP<CFloatValue> saturation;
        SP<CFloatValue> chroma;
        SP<CFloatValue> hue;
        SP<CFloatValue> strength;
        SP<CFloatValue> opacity;
        STintConfigValues tint;
        SP<CFloatValue> motionAmount;
        SP<CIntValue> motionCycles;
    };

    // Hyprland stores borrowed name pointers; deque preserves them until plugin unload.
    std::deque<std::string> configNames;
    const char* configName(std::string name) {
        configNames.push_back(std::move(name));
        return configNames.back().c_str();
    }
    SP<CBoolValue> enabled;
    SP<CBoolValue> outsetBlur;
    struct SBorderValues {
        SP<CBoolValue>  enabled;
        SP<CIntValue>   size;
        SP<CFloatValue> activeLightness;
        SP<CFloatValue> activeSaturation;
        SP<CFloatValue> activeChroma;
        SP<CFloatValue> activeHue;
        SP<CFloatValue> activeStrength;
        SP<CFloatValue> activeOpacity;
        SP<CFloatValue> inactiveLightness;
        SP<CFloatValue> inactiveSaturation;
        SP<CFloatValue> inactiveChroma;
        SP<CFloatValue> inactiveHue;
        SP<CFloatValue> inactiveStrength;
        SP<CFloatValue> inactiveOpacity;
        SP<CBoolValue>  pulseEnabled;
        SP<CFloatValue> pulseTime;
        SP<CFloatValue> pulseLightness;
        SP<CFloatValue> pulseSaturation;
        SP<CFloatValue> pulseChroma;
        SP<CFloatValue> pulseHue;
        SP<CFloatValue> pulseStrength;
        SP<CFloatValue> pulseOpacity;
        STintConfigValues activeTint;
        STintConfigValues inactiveTint;
        STintConfigValues pulseTint;
        SOrbitConfigValues activeOrbit;
        SOrbitConfigValues urgentOrbit;
        SOrbitConfigValues inactiveOrbit;
        SOrbitConfigValues orbitFlare;
        SP<CFloatValue> flareFadeIn;
        SP<CFloatValue> flareFadeOut;

    };
    SBorderValues insetValues, outsetValues;

    SP<CIntValue> intValue(HANDLE handle, const std::string& name, const char* description, int value, int minimum, int maximum) {
        auto option = Config::Values::SIntValueOptions{.min = minimum, .max = maximum};
        auto result = makeShared<CIntValue>(configName(name), description, value, std::move(option));
        if (!HyprlandAPI::addConfigValueV2(handle, result))
            return nullptr;
        return result;
    }

    SP<CColorValue> colorValue(HANDLE handle, const std::string& name, const char* description, Config::INTEGER value) {
        auto result = makeShared<CColorValue>(configName(name), description, value);
        if (!HyprlandAPI::addConfigValueV2(handle, result))
            return nullptr;
        return result;
    }

    SP<CFloatValue> floatValue(HANDLE handle, const std::string& name, const char* description, float value, float minimum, float maximum) {
        auto option = Config::Values::SFloatValueOptions{.min = minimum, .max = maximum};
        auto result = makeShared<CFloatValue>(configName(name), description, value, std::move(option));
        if (!HyprlandAPI::addConfigValueV2(handle, result))
            return nullptr;
        return result;
    }

    float boundedFloat(Config::FLOAT value, float fallback, float minimum, float maximum) {
        if (!std::isfinite(value))
            return fallback;

        return static_cast<float>(std::clamp(static_cast<double>(value), static_cast<double>(minimum), static_cast<double>(maximum)));
    }

    Config::INTEGER tintArgb(const std::array<float, 3>& color) {
        const auto channel = [](float value) { return static_cast<std::uint32_t>(std::clamp(value, 0.F, 1.F) * 255.F + 0.5F); };
        return static_cast<Config::INTEGER>(0xFF000000U | (channel(color[0]) << 16) | (channel(color[1]) << 8) | channel(color[2]));
    }

    bool registerTintValues(HANDLE handle, const STintConfigNames& names, STintConfigValues& tint, const SHyprionTintConfig& defaults) {
        tint.color = colorValue(handle, names.color, "Tint color for this state", tintArgb(defaults.color));
        if (!tint.color)
            return false;
        tint.strength = floatValue(handle, names.strength, "How much to blend toward the tint color (0 disables tinting)", defaults.strength, 0.F, 1.F);
        return static_cast<bool>(tint.strength);
    }
    bool registerOrbitValues(HANDLE handle, const SOrbitConfigNames& names, SOrbitConfigValues& values, const SHyprionOrbitProfile& defaults,
                             const char* enabledDescription = "Enable the traveling orbit highlight for this state") {
        values.enabled = makeShared<CBoolValue>(configName(names.enabled), enabledDescription, defaults.enabled);
        if (!HyprlandAPI::addConfigValueV2(handle, values.enabled))
            return false;
        values.clockwise = makeShared<CBoolValue>(configName(names.clockwise), "Travel clockwise instead of counter-clockwise", defaults.clockwise);
        if (!HyprlandAPI::addConfigValueV2(handle, values.clockwise))
            return false;
        values.time = floatValue(handle, names.time, "Orbit lap duration in milliseconds", defaults.timeMs, 1.F, 120000.F);
        if (!values.time)
            return false;
        values.length = floatValue(handle, names.length, "Orbit highlight length as a fraction of the perimeter", defaults.length, 0.F, 1.F);
        if (!values.length)
            return false;
        values.lightness = floatValue(handle, names.lightness, "Orbit absolute OKLab lightness offset", defaults.lightness, -1.F, 1.F);
        if (!values.lightness)
            return false;
        values.saturation = floatValue(handle, names.saturation, "Orbit chroma multiplier (1 is unchanged)", defaults.saturation, 0.F, 2.F);
        if (!values.saturation)
            return false;
        values.chroma = floatValue(handle, names.chroma, "Orbit OKLab chroma added (neutrals unaffected)", defaults.chroma, -1.F, 1.F);
        if (!values.chroma)
            return false;
        values.hue = floatValue(handle, names.hue, "Orbit OKLab hue rotation in degrees", defaults.hue, -180.F, 180.F);
        if (!values.hue)
            return false;
        values.motionAmount = floatValue(handle, names.motionAmount, "Orbit speed variation amplitude", defaults.motion.amount, 0.F, 0.95F);
        values.motionCycles = intValue(handle, names.motionCycles, "Speed variation cycles per lap", defaults.motion.cycles, 1, 4);
        if (!values.motionAmount || !values.motionCycles)
            return false;
        values.strength = floatValue(handle, names.strength, "Orbit transform amount (1 is as configured)", defaults.strength, 0.F, 2.F);
        if (!values.strength)
            return false;
        values.opacity = floatValue(handle, names.opacity, "Orbit border coverage over the backdrop", defaults.opacity, 0.F, 1.F);
        return values.opacity && registerTintValues(handle, names.tint, values.tint, defaults.tint);
    }

    bool tintValuesReady(const STintConfigValues& tint) {
        return tint.color && tint.strength;
    }

    SHyprionTintConfig tintConfig(const STintConfigValues& tint, float fallbackStrength) {
        const auto argb = static_cast<std::uint32_t>(tint.color->value());
        return {
            .color = {
                static_cast<float>((argb >> 16) & 0xFFU) / 255.F,
                static_cast<float>((argb >> 8) & 0xFFU) / 255.F,
                static_cast<float>(argb & 0xFFU) / 255.F,
            },
            .strength = boundedFloat(tint.strength->value(), fallbackStrength, 0.F, 1.F),
        };
    }

    bool orbitValuesReady(const SOrbitConfigValues& values) {
        return values.enabled && values.clockwise && values.time && values.length && values.lightness && values.saturation && values.chroma && values.hue && values.strength &&
            values.opacity && values.motionAmount && values.motionCycles && tintValuesReady(values.tint);
    }

    SHyprionOrbitProfile orbitProfileConfig(const SOrbitConfigValues& values, const SHyprionOrbitProfile& fallback) {
        return {
            .enabled = values.enabled->value(),
            .clockwise = values.clockwise->value(),
            .timeMs = boundedFloat(values.time->value(), fallback.timeMs, 1.F, 120000.F),
            .length = boundedFloat(values.length->value(), fallback.length, 0.F, 1.F),
            .lightness = boundedFloat(values.lightness->value(), fallback.lightness, -1.F, 1.F),
            .saturation = boundedFloat(values.saturation->value(), fallback.saturation, 0.F, 2.F),
            .chroma = boundedFloat(values.chroma->value(), fallback.chroma, -1.F, 1.F),
            .hue = boundedFloat(values.hue->value(), fallback.hue, -180.F, 180.F),
            .strength = boundedFloat(values.strength->value(), fallback.strength, 0.F, 2.F),
            .opacity = boundedFloat(values.opacity->value(), fallback.opacity, 0.F, 1.F),
            .tint = tintConfig(values.tint, fallback.tint.strength),
            .motion = {
                .amount = boundedFloat(values.motionAmount->value(), fallback.motion.amount, 0.F, 0.95F),
                .cycles = static_cast<int>(std::clamp<Config::INTEGER>(values.motionCycles->value(), 1, 4)),
            },
        };
    }

    bool registerBorder(HANDLE handle, SBorderValues& values, const std::string& prefix, const SHyprionBorderConfig& defaults) {
        const SOrbitConfigNames ACTIVE_ORBIT_NAMES = {
            .enabled = prefix + ".orbit.active.enabled",
            .clockwise = prefix + ".orbit.active.clockwise",
            .time = prefix + ".orbit.active.time",
            .length = prefix + ".orbit.active.length",
            .lightness = prefix + ".orbit.active.lightness",
            .saturation = prefix + ".orbit.active.saturation",
            .chroma = prefix + ".orbit.active.chroma",
            .hue = prefix + ".orbit.active.hue",
            .strength = prefix + ".orbit.active.strength",
            .opacity = prefix + ".orbit.active.opacity",
            .tint = {
                .color = prefix + ".orbit.active.tint.color",
                .strength = prefix + ".orbit.active.tint.strength",
            },
            .motionAmount = prefix + ".orbit.active.motion.amount",
            .motionCycles = prefix + ".orbit.active.motion.cycles",
        };
        const SOrbitConfigNames URGENT_ORBIT_NAMES = {
            .enabled = prefix + ".orbit.urgent.enabled",
            .clockwise = prefix + ".orbit.urgent.clockwise",
            .time = prefix + ".orbit.urgent.time",
            .length = prefix + ".orbit.urgent.length",
            .lightness = prefix + ".orbit.urgent.lightness",
            .saturation = prefix + ".orbit.urgent.saturation",
            .chroma = prefix + ".orbit.urgent.chroma",
            .hue = prefix + ".orbit.urgent.hue",
            .strength = prefix + ".orbit.urgent.strength",
            .opacity = prefix + ".orbit.urgent.opacity",
            .tint = {
                .color = prefix + ".orbit.urgent.tint.color",
                .strength = prefix + ".orbit.urgent.tint.strength",
            },
            .motionAmount = prefix + ".orbit.urgent.motion.amount",
            .motionCycles = prefix + ".orbit.urgent.motion.cycles",
        };
        const SOrbitConfigNames INACTIVE_ORBIT_NAMES = {
            .enabled = prefix + ".orbit.inactive.enabled",
            .clockwise = prefix + ".orbit.inactive.clockwise",
            .time = prefix + ".orbit.inactive.time",
            .length = prefix + ".orbit.inactive.length",
            .lightness = prefix + ".orbit.inactive.lightness",
            .saturation = prefix + ".orbit.inactive.saturation",
            .chroma = prefix + ".orbit.inactive.chroma",
            .hue = prefix + ".orbit.inactive.hue",
            .strength = prefix + ".orbit.inactive.strength",
            .opacity = prefix + ".orbit.inactive.opacity",
            .tint = {
                .color = prefix + ".orbit.inactive.tint.color",
                .strength = prefix + ".orbit.inactive.tint.strength",
            },
            .motionAmount = prefix + ".orbit.inactive.motion.amount",
            .motionCycles = prefix + ".orbit.inactive.motion.cycles",
        };
        const SOrbitConfigNames ORBIT_FLARE_NAMES = {
            .enabled = prefix + ".orbit.flare.enabled",
            .clockwise = prefix + ".orbit.flare.clockwise",
            .time = prefix + ".orbit.flare.time",
            .length = prefix + ".orbit.flare.length",
            .lightness = prefix + ".orbit.flare.lightness",
            .saturation = prefix + ".orbit.flare.saturation",
            .chroma = prefix + ".orbit.flare.chroma",
            .hue = prefix + ".orbit.flare.hue",
            .strength = prefix + ".orbit.flare.strength",
            .opacity = prefix + ".orbit.flare.opacity",
            .tint = {
                .color = prefix + ".orbit.flare.tint.color",
                .strength = prefix + ".orbit.flare.tint.strength",
            },
            .motionAmount = prefix + ".orbit.flare.motion.amount",
            .motionCycles = prefix + ".orbit.flare.motion.cycles",
        };

        const STintConfigNames ACTIVE_TINT_NAMES = {
            .color = prefix + ".active.tint.color",
            .strength = prefix + ".active.tint.strength",
        };
        const STintConfigNames INACTIVE_TINT_NAMES = {
            .color = prefix + ".inactive.tint.color",
            .strength = prefix + ".inactive.tint.strength",
        };
        const STintConfigNames PULSE_TINT_NAMES = {
            .color = prefix + ".pulse.tint.color",
            .strength = prefix + ".pulse.tint.strength",
        };

        values.enabled = makeShared<CBoolValue>(configName(prefix + ".enabled"), "Enable this sampled window border", defaults.enabled);
        if (!HyprlandAPI::addConfigValueV2(handle, values.enabled))
            return false;

        values.size = intValue(handle, prefix + ".size", "Border thickness in logical pixels (0 disables)", defaults.size, 0, 20);
        if (!values.size)
            return false;

        values.activeLightness = floatValue(handle, prefix + ".active.lightness", "Active border OKLab lightness offset", defaults.active.lightness, -1.F, 1.F);
        if (!values.activeLightness)
            return false;
        values.activeSaturation = floatValue(handle, prefix + ".active.saturation", "Active border chroma multiplier (1 is unchanged)", defaults.active.saturation, 0.F, 2.F);
        if (!values.activeSaturation)
            return false;
        values.activeChroma = floatValue(handle, prefix + ".active.chroma", "Active border OKLab chroma added (neutrals unaffected)", defaults.active.chroma, -1.F, 1.F);
        if (!values.activeChroma)
            return false;
        values.activeHue = floatValue(handle, prefix + ".active.hue", "Active border OKLab hue rotation in degrees", defaults.active.hue, -180.F, 180.F);
        if (!values.activeHue)
            return false;
        values.activeStrength = floatValue(handle, prefix + ".active.strength", "Active border transform amount (1 is as configured)", defaults.active.strength, 0.F, 2.F);
        if (!values.activeStrength)
            return false;
        values.activeOpacity = floatValue(handle, prefix + ".active.opacity", "How much the active border covers the backdrop", defaults.active.opacity, 0.F, 1.F);
        if (!values.activeOpacity || !registerTintValues(handle, ACTIVE_TINT_NAMES, values.activeTint, defaults.active.tint))
            return false;

        values.inactiveLightness = floatValue(handle, prefix + ".inactive.lightness", "Inactive border OKLab lightness offset", defaults.inactive.lightness, -1.F, 1.F);
        if (!values.inactiveLightness)
            return false;
        values.inactiveSaturation = floatValue(handle, prefix + ".inactive.saturation", "Inactive border chroma multiplier (1 is unchanged)", defaults.inactive.saturation, 0.F, 2.F);
        if (!values.inactiveSaturation)
            return false;
        values.inactiveChroma = floatValue(handle, prefix + ".inactive.chroma", "Inactive border OKLab chroma added (neutrals unaffected)", defaults.inactive.chroma, -1.F, 1.F);
        if (!values.inactiveChroma)
            return false;
        values.inactiveHue = floatValue(handle, prefix + ".inactive.hue", "Inactive border OKLab hue rotation in degrees", defaults.inactive.hue, -180.F, 180.F);
        if (!values.inactiveHue)
            return false;
        values.inactiveStrength = floatValue(handle, prefix + ".inactive.strength", "Inactive border transform amount (1 is as configured)", defaults.inactive.strength, 0.F, 2.F);
        if (!values.inactiveStrength)
            return false;
        values.inactiveOpacity = floatValue(handle, prefix + ".inactive.opacity", "How much the inactive border covers the backdrop", defaults.inactive.opacity, 0.F, 1.F);
        if (!values.inactiveOpacity || !registerTintValues(handle, INACTIVE_TINT_NAMES, values.inactiveTint, defaults.inactive.tint))
            return false;

        values.pulseEnabled = makeShared<CBoolValue>(configName(prefix + ".pulse.enabled"), "Enable the focus-gain border pulse", defaults.pulse.enabled);
        if (!HyprlandAPI::addConfigValueV2(handle, values.pulseEnabled))
            return false;
        values.pulseTime = floatValue(handle, prefix + ".pulse.time", "Pulse duration in milliseconds", defaults.pulse.timeMs, 0.F, 2000.F);
        if (!values.pulseTime)
            return false;
        values.pulseLightness = floatValue(handle, prefix + ".pulse.lightness", "Peak pulse OKLab lightness offset", defaults.pulse.lightness, -1.F, 1.F);
        if (!values.pulseLightness)
            return false;
        values.pulseSaturation = floatValue(handle, prefix + ".pulse.saturation", "Peak pulse chroma multiplier (1 is unchanged)", defaults.pulse.saturation, 0.F, 2.F);
        if (!values.pulseSaturation)
            return false;
        values.pulseChroma = floatValue(handle, prefix + ".pulse.chroma", "Peak pulse OKLab chroma added (neutrals unaffected)", defaults.pulse.chroma, -1.F, 1.F);
        if (!values.pulseChroma)
            return false;
        values.pulseHue = floatValue(handle, prefix + ".pulse.hue", "Peak pulse OKLab hue rotation in degrees", defaults.pulse.hue, -180.F, 180.F);
        if (!values.pulseHue)
            return false;
        values.pulseStrength = floatValue(handle, prefix + ".pulse.strength", "Peak pulse transform amount (1 is as configured)", defaults.pulse.strength, 0.F, 2.F);
        if (!values.pulseStrength)
            return false;
        values.pulseOpacity = floatValue(handle, prefix + ".pulse.opacity", "How much the pulse border covers the backdrop", defaults.pulse.opacity, 0.F, 1.F);
        if (!values.pulseOpacity || !registerTintValues(handle, PULSE_TINT_NAMES, values.pulseTint, defaults.pulse.tint))
            return false;
        return registerOrbitValues(handle, ACTIVE_ORBIT_NAMES, values.activeOrbit, defaults.orbit.active) &&
            registerOrbitValues(handle, URGENT_ORBIT_NAMES, values.urgentOrbit, defaults.orbit.urgent) &&
            registerOrbitValues(handle, INACTIVE_ORBIT_NAMES, values.inactiveOrbit, defaults.orbit.inactive) &&
            registerOrbitValues(handle, ORBIT_FLARE_NAMES, values.orbitFlare, defaults.orbit.flare,
                "Ignite the orbit on focus gain with a single lap") &&
            (values.flareFadeIn = floatValue(handle, prefix + ".orbit.flare.fade.start", "Flare fade-in duration in milliseconds",
                                             defaults.orbit.flare.fadeInMs, 0.F, 120000.F)) &&
            (values.flareFadeOut = floatValue(handle, prefix + ".orbit.flare.fade.finish", "Flare fade-out duration anchored to the lap end, in milliseconds",
                                              defaults.orbit.flare.fadeOutMs, 0.F, 120000.F));
    }

    SHyprionConfig cachedConfig;
    bool            cachedConfigValid = false;

    SHyprionBorderConfig readBorder(const SBorderValues& values, const SHyprionBorderConfig& defaults) {
        SHyprionBorderConfig config = defaults;
        if (!values.enabled || !values.size || !values.activeLightness || !values.activeSaturation || !values.activeChroma || !values.activeHue || !values.activeStrength ||
            !values.activeOpacity || !values.inactiveLightness ||
            !values.inactiveSaturation || !values.inactiveChroma || !values.inactiveHue || !values.inactiveStrength || !values.inactiveOpacity ||
            !values.pulseEnabled || !values.pulseTime ||
            !values.pulseLightness || !values.pulseSaturation || !values.pulseChroma || !values.pulseHue || !values.pulseStrength || !values.pulseOpacity ||
            !tintValuesReady(values.activeTint) ||
            !tintValuesReady(values.inactiveTint) ||
            !tintValuesReady(values.pulseTint) || !orbitValuesReady(values.activeOrbit) || !orbitValuesReady(values.urgentOrbit) || !orbitValuesReady(values.inactiveOrbit) ||
            !orbitValuesReady(values.orbitFlare) || !values.flareFadeIn || !values.flareFadeOut)
            return config;

        config.enabled = values.enabled->value();
        config.size = static_cast<int>(std::clamp<Config::INTEGER>(values.size->value(), 0, 20));
        // A zero thickness draws nothing, so treat it like `enabled = false`.
        if (config.size <= 0)
            config.enabled = false;
        config.active = {
            .lightness = boundedFloat(values.activeLightness->value(), defaults.active.lightness, -1.F, 1.F),
            .saturation = boundedFloat(values.activeSaturation->value(), defaults.active.saturation, 0.F, 2.F),
            .chroma = boundedFloat(values.activeChroma->value(), defaults.active.chroma, -1.F, 1.F),
            .hue = boundedFloat(values.activeHue->value(), defaults.active.hue, -180.F, 180.F),
            .strength = boundedFloat(values.activeStrength->value(), defaults.active.strength, 0.F, 2.F),
            .opacity = boundedFloat(values.activeOpacity->value(), defaults.active.opacity, 0.F, 1.F),
            .tint = tintConfig(values.activeTint, defaults.active.tint.strength),
        };
        config.inactive = {
            .lightness = boundedFloat(values.inactiveLightness->value(), defaults.inactive.lightness, -1.F, 1.F),
            .saturation = boundedFloat(values.inactiveSaturation->value(), defaults.inactive.saturation, 0.F, 2.F),
            .chroma = boundedFloat(values.inactiveChroma->value(), defaults.inactive.chroma, -1.F, 1.F),
            .hue = boundedFloat(values.inactiveHue->value(), defaults.inactive.hue, -180.F, 180.F),
            .strength = boundedFloat(values.inactiveStrength->value(), defaults.inactive.strength, 0.F, 2.F),
            .opacity = boundedFloat(values.inactiveOpacity->value(), defaults.inactive.opacity, 0.F, 1.F),
            .tint = tintConfig(values.inactiveTint, defaults.inactive.tint.strength),
        };
        config.pulse = {
            .enabled = values.pulseEnabled->value(),
            .timeMs = boundedFloat(values.pulseTime->value(), defaults.pulse.timeMs, 0.F, 2000.F),
            .lightness = boundedFloat(values.pulseLightness->value(), defaults.pulse.lightness, -1.F, 1.F),
            .saturation = boundedFloat(values.pulseSaturation->value(), defaults.pulse.saturation, 0.F, 2.F),
            .chroma = boundedFloat(values.pulseChroma->value(), defaults.pulse.chroma, -1.F, 1.F),
            .hue = boundedFloat(values.pulseHue->value(), defaults.pulse.hue, -180.F, 180.F),
            .strength = boundedFloat(values.pulseStrength->value(), defaults.pulse.strength, 0.F, 2.F),
            .opacity = boundedFloat(values.pulseOpacity->value(), defaults.pulse.opacity, 0.F, 1.F),
            .tint = tintConfig(values.pulseTint, defaults.pulse.tint.strength),
        };
        config.orbit.active = orbitProfileConfig(values.activeOrbit, config.orbit.active);
        config.orbit.urgent = orbitProfileConfig(values.urgentOrbit, config.orbit.urgent);
        config.orbit.inactive = orbitProfileConfig(values.inactiveOrbit, config.orbit.inactive);
        config.orbit.flare = orbitProfileConfig(values.orbitFlare, config.orbit.flare);
        config.orbit.flare.fadeInMs = boundedFloat(values.flareFadeIn->value(), defaults.orbit.flare.fadeInMs, 0.F, 120000.F);
        config.orbit.flare.fadeOutMs = boundedFloat(values.flareFadeOut->value(), defaults.orbit.flare.fadeOutMs, 0.F, 120000.F);
        return config;
    }
}

bool registerHyprionConfig(HANDLE handle) {
    cachedConfigValid = false;
    enabled = makeShared<CBoolValue>("plugin:hyprion:enabled", "Enable Hyprion visual effects", true);
    outsetBlur = makeShared<CBoolValue>("plugin:hyprion:border.outset.blur", "Sample Hyprland's blur for this window (requires blur)", true);
    return HyprlandAPI::addConfigValueV2(handle, enabled) && HyprlandAPI::addConfigValueV2(handle, outsetBlur) &&
        registerBorder(handle, insetValues, "plugin:hyprion:border.inset", HYPRION_INSET_DEFAULTS) &&
        registerBorder(handle, outsetValues, "plugin:hyprion:border.outset", HYPRION_OUTSET_DEFAULTS);
}

void invalidateHyprionConfig() {
    cachedConfigValid = false;
}

SHyprionConfig hyprionConfig() {
    if (!cachedConfigValid) {
        cachedConfig = {};
        if (enabled)
            cachedConfig.enabled = enabled->value();
        cachedConfig.inset = readBorder(insetValues, HYPRION_INSET_DEFAULTS);
        cachedConfig.outset = readBorder(outsetValues, HYPRION_OUTSET_DEFAULTS);
        static const auto NATIVE_BLUR = CConfigValue<Config::INTEGER>("decoration:blur:enabled");
        // Which blur source the outset samples is Hyprland's decision, not ours
        // (see CHyprionDecoration::drawPass), so only the global blur toggle matters here.
        cachedConfig.outsetBlur = cachedConfig.enabled && cachedConfig.outset.enabled && outsetBlur && outsetBlur->value() && *NATIVE_BLUR;
        cachedConfigValid = true;
    }
    return cachedConfig;
}
