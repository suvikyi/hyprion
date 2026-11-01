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
#include <hyprland/src/config/values/types/GradientValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>

namespace {
    using Config::Values::CBoolValue;
    using Config::Values::CFloatValue;
    using Config::Values::CIntValue;

    struct SColorizeValues {
        SP<Config::Values::CGradientValue> gradient;
        SP<CFloatValue>                    strength;
    };

    struct SGradeValues {
        SP<CFloatValue>  lightness;
        SP<CFloatValue>  saturation;
        SP<CFloatValue>  chroma;
        SP<CFloatValue>  hue;
        SColorizeValues  colorize;
    };

    struct SStateValues {
        SGradeValues    grade;
        SP<CFloatValue> opacity;
    };

    struct SPulseValues {
        SP<CBoolValue>  enabled;
        SP<CFloatValue> time;
        SP<CFloatValue> interval;
        SStateValues    state;
    };

    struct SOrbitValues {
        SP<CBoolValue>  enabled;
        SP<CBoolValue>  clockwise;
        SP<CFloatValue> time;
        SP<CFloatValue> length;
        SGradeValues    grade;
        SP<CFloatValue> opacity;
        SP<CFloatValue> motionAmount;
        SP<CIntValue>   motionCycles;
    };

    struct SBorderValues {
        SP<CBoolValue>  enabled;
        SP<CIntValue>   size;
        SStateValues    active;
        SStateValues    inactive;
        SPulseValues    pulseActive;
        SPulseValues    pulseInactive;
        SPulseValues    pulseUrgent;
        SOrbitValues    activeOrbit;
        SOrbitValues    urgentOrbit;
        SOrbitValues    inactiveOrbit;
        SOrbitValues    orbitFlare;
        SP<CFloatValue> flareFadeIn;
        SP<CFloatValue> flareFadeOut;
    };

    // Hyprland stores borrowed name pointers; deque preserves them until plugin unload.
    std::deque<std::string> configNames;
    const char* configName(std::string name) {
        configNames.push_back(std::move(name));
        return configNames.back().c_str();
    }

    SP<CBoolValue> enabled;
    SP<CBoolValue> outsetBlur;
    SBorderValues  insetValues, outsetValues;

    SP<CIntValue> intValue(HANDLE handle, const std::string& name, const char* description, int value, int minimum, int maximum) {
        auto option = Config::Values::SIntValueOptions{.min = minimum, .max = maximum};
        auto result = makeShared<CIntValue>(configName(name), description, value, std::move(option));
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

    bool registerColorize(HANDLE handle, const std::string& prefix, SColorizeValues& values, const SHyprionColorizeConfig& defaults) {
        const auto& stop = defaults.gradient.colors.empty() ? SHyprionColorStop{51.F / 255.F, 204.F / 255.F, 1.F, 1.F} : defaults.gradient.colors.front();
        auto        result = makeShared<Config::Values::CGradientValue>(configName(prefix + ".gradient"), "Colorize target gradient", CHyprColor{stop.r, stop.g, stop.b, stop.a});
        if (!HyprlandAPI::addConfigValueV2(handle, result))
            return false;
        values.gradient = result;
        values.strength = floatValue(handle, prefix + ".strength", "How much to blend toward the target gradient (0 disables colorizing)", defaults.strength, 0.F, 1.F);
        return static_cast<bool>(values.strength);
    }

    bool registerGrade(HANDLE handle, const std::string& prefix, SGradeValues& values, const SHyprionGradeStyle& defaults, const std::string& label) {
        values.lightness = floatValue(handle, prefix + ".lightness", (label + " grade-space lightness offset").c_str(), defaults.lightness, -1.F, 1.F);
        if (!values.lightness)
            return false;
        values.saturation = floatValue(handle, prefix + ".saturation", (label + " chroma multiplier (1 is unchanged)").c_str(), defaults.saturation, 0.F, 2.F);
        if (!values.saturation)
            return false;
        values.chroma = floatValue(handle, prefix + ".chroma", (label + " grade-space chroma added (neutrals unaffected)").c_str(), defaults.chroma, -1.F, 1.F);
        if (!values.chroma)
            return false;
        values.hue = floatValue(handle, prefix + ".hue", (label + " grade-space hue rotation in degrees").c_str(), defaults.hue, -180.F, 180.F);
        if (!values.hue)
            return false;
        return registerColorize(handle, prefix + ".colorize", values.colorize, defaults.colorize);
    }

    bool registerState(HANDLE handle, const std::string& prefix, SStateValues& values, const SHyprionStateConfig& defaults, const std::string& label) {
        if (!registerGrade(handle, prefix, values.grade, defaults.grade, label))
            return false;
        values.opacity = floatValue(handle, prefix + ".opacity", ("How much " + label + " covers the backdrop").c_str(), defaults.opacity, 0.F, 1.F);
        return static_cast<bool>(values.opacity);
    }

    bool registerPulse(HANDLE handle, const std::string& prefix, SPulseValues& values, const SHyprionPulseProfile& defaults) {
        values.enabled = makeShared<CBoolValue>(configName(prefix + ".enabled"), "Enable this pulse", defaults.enabled);
        if (!HyprlandAPI::addConfigValueV2(handle, values.enabled))
            return false;
        values.time = floatValue(handle, prefix + ".time", "Pulse duration in milliseconds", defaults.timeMs, 0.F, 2000.F);
        if (!values.time)
            return false;
        values.interval = floatValue(handle, prefix + ".interval", "Pulse repeat interval in milliseconds (0 is one-shot)", defaults.intervalMs, 0.F, 120000.F);
        if (!values.interval)
            return false;
        return registerState(handle, prefix, values.state, {.grade = defaults.grade, .opacity = defaults.opacity}, "Pulse border");
    }

    bool registerOrbit(HANDLE handle, const std::string& prefix, SOrbitValues& values, const SHyprionOrbitProfile& defaults,
                       const char* enabledDescription = "Enable the traveling orbit highlight for this state") {
        values.enabled = makeShared<CBoolValue>(configName(prefix + ".enabled"), enabledDescription, defaults.enabled);
        if (!HyprlandAPI::addConfigValueV2(handle, values.enabled))
            return false;
        values.clockwise = makeShared<CBoolValue>(configName(prefix + ".clockwise"), "Travel clockwise instead of counter-clockwise", defaults.clockwise);
        if (!HyprlandAPI::addConfigValueV2(handle, values.clockwise))
            return false;
        values.time = floatValue(handle, prefix + ".time", "Orbit lap duration in milliseconds", defaults.timeMs, 1.F, 120000.F);
        if (!values.time)
            return false;
        values.length = floatValue(handle, prefix + ".length", "Orbit highlight length as a fraction of the perimeter", defaults.length, 0.F, 1.F);
        if (!values.length)
            return false;
        if (!registerGrade(handle, prefix, values.grade, defaults.grade, "Orbit"))
            return false;
        values.opacity = floatValue(handle, prefix + ".opacity", "Orbit border coverage over the backdrop", defaults.opacity, 0.F, 1.F);
        if (!values.opacity)
            return false;
        values.motionAmount = floatValue(handle, prefix + ".motion.amount", "Orbit speed variation amplitude", defaults.motion.amount, 0.F, 0.95F);
        values.motionCycles = intValue(handle, prefix + ".motion.cycles", "Speed variation cycles per lap", defaults.motion.cycles, 1, 4);
        return values.motionAmount && values.motionCycles;
    }

    bool colorizeReady(const SColorizeValues& values) {
        return values.gradient && values.strength;
    }

    bool gradeReady(const SGradeValues& values) {
        return values.lightness && values.saturation && values.chroma && values.hue && colorizeReady(values.colorize);
    }

    bool stateReady(const SStateValues& values) {
        return gradeReady(values.grade) && values.opacity;
    }

    bool pulseReady(const SPulseValues& values) {
        return values.enabled && values.time && values.interval && stateReady(values.state);
    }

    bool orbitReady(const SOrbitValues& values) {
        return values.enabled && values.clockwise && values.time && values.length && gradeReady(values.grade) && values.opacity && values.motionAmount && values.motionCycles;
    }

    SHyprionColorizeConfig readColorize(const SColorizeValues& values, const SHyprionColorizeConfig& fallback) {
        const auto& source = values.gradient->value();
        SHyprionGradient gradient;
        gradient.angle = source.m_angle;
        const auto count = std::min<std::size_t>(source.m_colors.size(), 10);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& color = source.m_colors[i];
            gradient.colors.push_back({static_cast<float>(color.r), static_cast<float>(color.g), static_cast<float>(color.b), static_cast<float>(color.a)});
        }
        if (gradient.colors.empty())
            gradient = fallback.gradient;
        return {
            .gradient = std::move(gradient),
            .strength = boundedFloat(values.strength->value(), fallback.strength, 0.F, 1.F),
        };
    }

    SHyprionGradeStyle readGrade(const SGradeValues& values, const SHyprionGradeStyle& fallback) {
        return {
            .lightness = boundedFloat(values.lightness->value(), fallback.lightness, -1.F, 1.F),
            .saturation = boundedFloat(values.saturation->value(), fallback.saturation, 0.F, 2.F),
            .chroma = boundedFloat(values.chroma->value(), fallback.chroma, -1.F, 1.F),
            .hue = boundedFloat(values.hue->value(), fallback.hue, -180.F, 180.F),
            .colorize = readColorize(values.colorize, fallback.colorize),
        };
    }

    SHyprionStateConfig readState(const SStateValues& values, const SHyprionStateConfig& fallback) {
        return {
            .grade = readGrade(values.grade, fallback.grade),
            .opacity = boundedFloat(values.opacity->value(), fallback.opacity, 0.F, 1.F),
        };
    }

    SHyprionPulseProfile readPulse(const SPulseValues& values, const SHyprionPulseProfile& fallback) {
        return {
            .enabled = values.enabled->value(),
            .timeMs = boundedFloat(values.time->value(), fallback.timeMs, 0.F, 2000.F),
            .intervalMs = boundedFloat(values.interval->value(), fallback.intervalMs, 0.F, 120000.F),
            .grade = readGrade(values.state.grade, fallback.grade),
            .opacity = boundedFloat(values.state.opacity->value(), fallback.opacity, 0.F, 1.F),
        };
    }

    SHyprionOrbitProfile readOrbit(const SOrbitValues& values, const SHyprionOrbitProfile& fallback) {
        return {
            .enabled = values.enabled->value(),
            .clockwise = values.clockwise->value(),
            .timeMs = boundedFloat(values.time->value(), fallback.timeMs, 1.F, 120000.F),
            .length = boundedFloat(values.length->value(), fallback.length, 0.F, 1.F),
            .grade = readGrade(values.grade, fallback.grade),
            .opacity = boundedFloat(values.opacity->value(), fallback.opacity, 0.F, 1.F),
            .motion = {
                .amount = boundedFloat(values.motionAmount->value(), fallback.motion.amount, 0.F, 0.95F),
                .cycles = static_cast<int>(std::clamp<Config::INTEGER>(values.motionCycles->value(), 1, 4)),
            },
            .fadeInMs = fallback.fadeInMs,
            .fadeOutMs = fallback.fadeOutMs,
        };
    }

    bool registerBorder(HANDLE handle, SBorderValues& values, const std::string& prefix, const SHyprionBorderConfig& defaults) {
        values.enabled = makeShared<CBoolValue>(configName(prefix + ".enabled"), "Enable this sampled window border", defaults.enabled);
        if (!HyprlandAPI::addConfigValueV2(handle, values.enabled))
            return false;
        values.size = intValue(handle, prefix + ".size", "Border thickness in logical pixels (0 disables)", defaults.size, 0, 20);
        if (!values.size)
            return false;

        if (!registerState(handle, prefix + ".active", values.active, defaults.active, "Active border"))
            return false;
        if (!registerState(handle, prefix + ".inactive", values.inactive, defaults.inactive, "Inactive border"))
            return false;

        if (!registerPulse(handle, prefix + ".pulse.active", values.pulseActive, defaults.pulse.active))
            return false;
        if (!registerPulse(handle, prefix + ".pulse.inactive", values.pulseInactive, defaults.pulse.inactive))
            return false;
        if (!registerPulse(handle, prefix + ".pulse.urgent", values.pulseUrgent, defaults.pulse.urgent))
            return false;

        if (!registerOrbit(handle, prefix + ".orbit.active", values.activeOrbit, defaults.orbit.active))
            return false;
        if (!registerOrbit(handle, prefix + ".orbit.urgent", values.urgentOrbit, defaults.orbit.urgent))
            return false;
        if (!registerOrbit(handle, prefix + ".orbit.inactive", values.inactiveOrbit, defaults.orbit.inactive))
            return false;
        if (!registerOrbit(handle, prefix + ".orbit.flare", values.orbitFlare, defaults.orbit.flare, "Ignite the orbit on focus gain with a single lap"))
            return false;

        values.flareFadeIn = floatValue(handle, prefix + ".orbit.flare.fade.start", "Flare fade-in duration in milliseconds", defaults.orbit.flare.fadeInMs, 0.F, 120000.F);
        values.flareFadeOut = floatValue(handle, prefix + ".orbit.flare.fade.finish", "Flare fade-out duration anchored to the lap end, in milliseconds", defaults.orbit.flare.fadeOutMs, 0.F, 120000.F);
        return values.flareFadeIn && values.flareFadeOut;
    }

    SHyprionConfig cachedConfig;
    bool            cachedConfigValid = false;

    SHyprionBorderConfig readBorder(const SBorderValues& values, const SHyprionBorderConfig& defaults) {
        SHyprionBorderConfig config = defaults;
        if (!values.enabled || !values.size || !stateReady(values.active) || !stateReady(values.inactive) || !pulseReady(values.pulseActive) ||
            !pulseReady(values.pulseInactive) || !pulseReady(values.pulseUrgent) || !orbitReady(values.activeOrbit) ||
            !orbitReady(values.urgentOrbit) || !orbitReady(values.inactiveOrbit) || !orbitReady(values.orbitFlare) || !values.flareFadeIn ||
            !values.flareFadeOut)
            return config;

        config.enabled = values.enabled->value();
        config.size = static_cast<int>(std::clamp<Config::INTEGER>(values.size->value(), 0, 20));
        if (config.size <= 0)
            config.enabled = false;
        config.active = readState(values.active, defaults.active);
        config.inactive = readState(values.inactive, defaults.inactive);
        config.pulse.active = readPulse(values.pulseActive, defaults.pulse.active);
        config.pulse.inactive = readPulse(values.pulseInactive, defaults.pulse.inactive);
        config.pulse.urgent = readPulse(values.pulseUrgent, defaults.pulse.urgent);
        config.orbit.active = readOrbit(values.activeOrbit, defaults.orbit.active);
        config.orbit.urgent = readOrbit(values.urgentOrbit, defaults.orbit.urgent);
        config.orbit.inactive = readOrbit(values.inactiveOrbit, defaults.orbit.inactive);
        config.orbit.flare = readOrbit(values.orbitFlare, defaults.orbit.flare);
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
        cachedConfig.outsetBlur = cachedConfig.enabled && cachedConfig.outset.enabled && outsetBlur && outsetBlur->value() && *NATIVE_BLUR;
        cachedConfigValid = true;
    }
    return cachedConfig;
}
