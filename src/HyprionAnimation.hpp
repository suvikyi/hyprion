#pragma once

#include "HyprionConfig.hpp"
#include "HyprionColor.hpp"

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

// Hyprland times both progress values we animate, so we never invent a
// duration: each is a 0 -> 1 value bound to the `border` animation leaf, the
// very one the native border colour fade uses. Focus transitions and orbit
// crossfades therefore take their curve and speed from `animation = border,
// ...`, land instantly when that animation is off or `animations:enabled =
// false`, and get repainted through the animation manager's damage.
class CHyprionProgress {
  public:
    explicit CHyprionProgress(std::string leaf = "border") : m_leaf(std::move(leaf)) {}

    void bind(PHLWINDOW window) {
        if (window && m_progress && m_window.lock() == window) {
            // The animation tree is rebuilt on every config reload, so pick up
            // the fresh animation node even when the window has not changed.
            m_progress->setConfig(animationConfig());
            return;
        }

        m_window = window;
        m_progress.reset();
        if (!window)
            return;

        Animation::mgr()->createAnimation(1.F, m_progress, animationConfig(), window, AVARDAMAGE_ENTIRE);
    }

    // Start a fresh 0 -> 1 leg. The value we come from stays on screen because
    // the caller captures the displayed style before restarting.
    void restart() {
        if (!m_progress)
            return;
        m_progress->setValueAndWarp(0.F);
        *m_progress = 1.F;
    }

    // Stop driving the value and land on it: nothing to cross-fade from.
    void settle() {
        if (m_progress)
            m_progress->setValueAndWarp(1.F);
    }

    // 1 whenever no leg is running, so a settled leg reads as its target style.
    float value() const { return m_progress ? m_progress->value() : 1.F; }

    bool running() const { return m_progress && m_progress->isBeingAnimated(); }

  private:
    SP<Hyprutils::Animation::SAnimationPropertyConfig> animationConfig() const {
        return Config::animationTree()->getAnimationPropertyConfig(m_leaf);
    }

    std::string       m_leaf;
    PHLANIMVAR<float> m_progress;
    PHLWINDOWREF      m_window;
};

// Each decoration owns its animation: losing focus must not cancel another window's fade.
class CHyprionAnimation {
  public:
    enum class EPulseKind : uint8_t {
        NONE = 0,
        ACTIVE,
        INACTIVE,
        URGENT,
    };

    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    explicit CHyprionAnimation(bool active = false, bool urgent = false) : m_active(active), m_urgent(urgent), m_orbitPhaseUpdated(Clock::now()) {}

    SHyprionFrameStyle style(const SHyprionBorderConfig& config, Time now) const {
        const auto& state = m_active ? config.active : config.inactive;
        const SHyprionStyle target = {
            .grade = state.grade,
            .opacity = state.opacity,
        };
        // The leg's progress is 1 whenever it is settled, so an idle border is
        // exactly the state's style.
        const float transition = m_borderProgress.value();
        SHyprionStyle border = {
            .grade = {
                .lightness = std::lerp(m_from.grade.lightness, target.grade.lightness, transition),
                .saturation = std::lerp(m_from.grade.saturation, target.grade.saturation, transition),
                .chroma = std::lerp(m_from.grade.chroma, target.grade.chroma, transition),
                .hue = Hyprion::lerpHue(m_from.grade.hue, target.grade.hue, transition),
                .colorize = {
                    .gradient = Hyprion::lerpGradient(m_from.grade.colorize.gradient, target.grade.colorize.gradient, transition),
                    .strength = std::lerp(m_from.grade.colorize.strength, target.grade.colorize.strength, transition),
                },
            },
            .opacity = std::lerp(m_from.opacity, target.opacity, transition),
        };

        // Pulse profiles are state-entry overlays: active/inactive/urgent.
        if (const auto* pulseProfile = currentPulseProfile(config); pulseProfile && pulseProfile->enabled) {
            const float duration = std::max(pulseProfile->timeMs, 0.F);
            float       elapsed = pulseElapsedMs(now);
            if (pulseProfile->intervalMs > 0.F && pulseProfile->intervalMs > duration)
                elapsed = std::fmod(elapsed, pulseProfile->intervalMs);
            const float pulse = duration > 0.F ? 1.F - std::abs(2.F * progress(elapsed, duration) - 1.F) : 0.F;
            border.grade.lightness = std::lerp(border.grade.lightness, pulseProfile->grade.lightness, pulse);
            border.grade.saturation = std::lerp(border.grade.saturation, pulseProfile->grade.saturation, pulse);
            border.grade.chroma = std::lerp(border.grade.chroma, pulseProfile->grade.chroma, pulse);
            border.grade.hue = Hyprion::lerpHue(border.grade.hue, pulseProfile->grade.hue, pulse);
            border.opacity = std::lerp(border.opacity, pulseProfile->opacity, pulse);
            border.grade.colorize.strength = std::lerp(border.grade.colorize.strength, pulseProfile->grade.colorize.strength, pulse);
            border.grade.colorize.gradient = Hyprion::lerpGradient(border.grade.colorize.gradient, pulseProfile->grade.colorize.gradient, pulse);
        }
        const float angleOffset = m_angleProgress.value() * 6.2831853071795864769F;
        border.grade.colorize.gradient.angle = std::fmod(border.grade.colorize.gradient.angle + angleOffset, 6.2831853071795864769F);
        auto orbit = orbitStyle(now);
        orbit.grade.colorize.gradient.angle = std::fmod(orbit.grade.colorize.gradient.angle + angleOffset, 6.2831853071795864769F);
        return {.border = border, .orbit = orbit};
    }

    void bind(PHLWINDOW window) {
        m_borderProgress.bind(window);
        m_orbitProgress.bind(window);
        m_angleProgress.bind(window);
    }

    void stateChanged(bool active, bool urgent, const SHyprionBorderConfig& config, Time now) {
        const bool activeChanged = active != m_active;
        const bool urgentChanged = urgent != m_urgent;
        if (activeChanged) {
            m_from = style(config, now).border;
            m_started = now;
            m_borderProgress.restart();
            m_angleProgress.restart();
            m_orbitFlareMs = active && config.orbit.flare.enabled ? std::max(config.orbit.flare.timeMs, 1.F) : 0.F;
            m_orbitFlareFadeInMs = m_orbitFlareMs > 0.F ? std::max(config.orbit.flare.fadeInMs, 0.F) : 0.F;
            m_orbitFlareFadeOutMs = m_orbitFlareMs > 0.F ? std::max(config.orbit.flare.fadeOutMs, 0.F) : 0.F;
        }

        m_active = active;
        m_urgent = urgent;

        if (urgentChanged && urgent && config.pulse.urgent.enabled)
            startPulse(EPulseKind::URGENT, config.pulse.urgent, now);
        else if (activeChanged) {
            const auto& profile = active ? config.pulse.active : config.pulse.inactive;
            if (profile.enabled)
                startPulse(active ? EPulseKind::ACTIVE : EPulseKind::INACTIVE, profile, now);
            else if (m_pulseKind == EPulseKind::ACTIVE || m_pulseKind == EPulseKind::INACTIVE)
                clearPulse();
        } else if (urgentChanged && !urgent && m_pulseKind == EPulseKind::URGENT)
            clearPulse();

        retargetOrbit(config, now);
        if (m_orbitFlareMs > 0.F) {
            // Ignition: the flare orbit is fully present the moment it appears.
            m_orbitProgress.settle();
        }
    }

    void refresh(bool active, bool urgent, const SHyprionBorderConfig& config, Time now) {
        advanceOrbitPhase(now);
        m_active = active;
        m_urgent = urgent;
        clearPulse();
        m_orbitFlareMs = 0.F;
        m_orbitFlareFadeInMs = 0.F;
        m_orbitFlareFadeOutMs = 0.F;
        m_borderProgress.settle();
        m_angleProgress.settle();
        retargetOrbit(config, now);
    }

    void reset(bool active, bool urgent, Time now) {
        advanceOrbitPhase(now);
        m_active = active;
        m_urgent = urgent;
        clearPulse();
        m_orbitFlareMs = 0.F;
        m_orbitFlareFadeInMs = 0.F;
        m_orbitFlareFadeOutMs = 0.F;
        m_borderProgress.settle();
        m_angleProgress.settle();
        m_orbitFrom = m_orbitTo;
        m_orbitTo.amount = 0.F;
        m_orbitTargetEnabled = false;
        m_orbitProgress.settle();
        m_orbitPhaseUpdated = now;
    }

    // Returns whether anything we show still needs our ticker: the pulse, the
    // flare, the traveling orbit and the focus transition. Hyprland animates the
    // transition value and damages the window for it, so our tick only has to
    // cover the ring's own box.
    bool advance(const SHyprionBorderConfig& config, Time now) {
        const bool wasRunning = m_borderProgress.running() || m_pulseKind != EPulseKind::NONE || m_orbitFlareMs > 0.F || m_orbitTo.amount > 0.F;
        advanceOrbitPhase(now);
        updatePulse(config, now);
        if (m_orbitFlareMs > 0.F && elapsedMs(now) >= m_orbitFlareMs) {
            // The flare's fade-out is already complete (amount 0), so hand back
            // from exactly that faded style.
            const SHyprionOrbitStyle flareEnd = orbitStyle(now);
            m_orbitFlareMs = 0.F;
            m_orbitFlareFadeInMs = 0.F;
            m_orbitFlareFadeOutMs = 0.F;
            retargetOrbit(config, now, &flareEnd);
        }
        return wasRunning;
    }

  private:
    static float progress(float elapsed, float duration) {
        return duration > 0.F ? std::clamp(elapsed / duration, 0.F, 1.F) : 1.F;
    }

    float elapsedMs(Time now) const {
        return std::chrono::duration<float, std::milli>(now - m_started).count();
    }

    SHyprionOrbitStyle orbitStyle(Time now) const {
        SHyprionOrbitStyle result = m_orbitTo;
        const float transition = m_orbitProgress.value();
        result.timeMs = std::lerp(m_orbitFrom.timeMs, m_orbitTo.timeMs, transition);
        result.length = std::lerp(m_orbitFrom.length, m_orbitTo.length, transition);
        result.grade.lightness = std::lerp(m_orbitFrom.grade.lightness, m_orbitTo.grade.lightness, transition);
        result.grade.saturation = std::lerp(m_orbitFrom.grade.saturation, m_orbitTo.grade.saturation, transition);
        result.grade.chroma = std::lerp(m_orbitFrom.grade.chroma, m_orbitTo.grade.chroma, transition);
        result.grade.hue = Hyprion::lerpHue(m_orbitFrom.grade.hue, m_orbitTo.grade.hue, transition);
        result.opacity = std::lerp(m_orbitFrom.opacity, m_orbitTo.opacity, transition);
        result.amount = std::lerp(m_orbitFrom.amount, m_orbitTo.amount, transition);
        result.grade.colorize.strength = std::lerp(m_orbitFrom.grade.colorize.strength, m_orbitTo.grade.colorize.strength, transition);
        result.grade.colorize.gradient = Hyprion::lerpGradient(m_orbitFrom.grade.colorize.gradient, m_orbitTo.grade.colorize.gradient, transition);
        // The flare fades in and out inside its single ignition lap: the orbit is
        // fully transparent at the start and again exactly as the lap completes,
        // instead of popping in or lingering afterwards.
        if (m_orbitFlareMs > 0.F)
            result.amount *= flareAmount(now);
        result.phase = static_cast<float>(motionPhase(m_orbitPhase));
        return result;
    }

    double motionPhase(double phase) const {
        constexpr double TAU = 6.2831853071795864769;
        const double frequency = TAU * m_motionCycles;
        return phase - m_motionAmount * std::sin(frequency * phase) / frequency;
    }

    // Presence envelope of the ignition lap: ramp in over the first fadeInMs and
    // out over the last fadeOutMs, reaching zero exactly at the end of the lap.
    // If the two fades would overrun the lap they are scaled down together so the
    // envelope still peaks at full presence.
    float flareAmount(Time now) const {
        const float elapsed = elapsedMs(now);
        const float lap = m_orbitFlareMs;
        float fadeIn = m_orbitFlareFadeInMs;
        float fadeOut = m_orbitFlareFadeOutMs;
        if (fadeIn + fadeOut > lap && fadeIn + fadeOut > 0.F) {
            const float scale = lap / (fadeIn + fadeOut);
            fadeIn *= scale;
            fadeOut *= scale;
        }
        float amount = 1.F;
        if (fadeIn > 0.F && elapsed < fadeIn)
            amount = elapsed / fadeIn;
        if (fadeOut > 0.F && elapsed > lap - fadeOut)
            amount = std::min(amount, (lap - elapsed) / fadeOut);
        return std::clamp(amount, 0.F, 1.F);
    }

    void setMotion(const SHyprionMotionConfig& motion) {
        if (m_motionAmount == motion.amount && m_motionCycles == motion.cycles)
            return;
        const double position = motionPhase(m_orbitPhase);
        m_motionAmount = motion.amount;
        m_motionCycles = motion.cycles;
        // The warp is strictly monotonic: preserve position when a profile changes.
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 32; ++i) {
            const double mid = (lo + hi) * 0.5;
            if (motionPhase(mid) < position)
                lo = mid;
            else
                hi = mid;
        }
        m_orbitPhase = (lo + hi) * 0.5;
    }

    void advanceOrbitPhase(Time now) {
        const double elapsed = std::chrono::duration<double, std::milli>(now - m_orbitPhaseUpdated).count();
        if (elapsed > 0.0 && m_orbitLapTimeMs > 0.F)
            m_orbitPhase = m_orbitPhase + elapsed * m_orbitDirection / static_cast<double>(m_orbitLapTimeMs);
        m_orbitPhase -= std::floor(m_orbitPhase);
        m_orbitPhaseUpdated = now;
    }

    static bool sameOrbit(const SHyprionOrbitStyle& lhs, const SHyprionOrbitStyle& rhs) {
        return lhs.timeMs == rhs.timeMs && lhs.length == rhs.length && lhs.grade.lightness == rhs.grade.lightness && lhs.grade.saturation == rhs.grade.saturation &&
            lhs.grade.chroma == rhs.grade.chroma && lhs.grade.hue == rhs.grade.hue &&
            lhs.opacity == rhs.opacity && lhs.amount == rhs.amount && lhs.grade.colorize.gradient == rhs.grade.colorize.gradient && lhs.grade.colorize.strength == rhs.grade.colorize.strength;
    }

    const SHyprionPulseProfile* pulseProfile(const SHyprionBorderConfig& config, EPulseKind kind) const {
        switch (kind) {
            case EPulseKind::ACTIVE: return &config.pulse.active;
            case EPulseKind::INACTIVE: return &config.pulse.inactive;
            case EPulseKind::URGENT: return &config.pulse.urgent;
            default: return nullptr;
        }
    }

    const SHyprionPulseProfile* currentPulseProfile(const SHyprionBorderConfig& config) const {
        return pulseProfile(config, m_pulseKind);
    }

    void startPulse(EPulseKind kind, const SHyprionPulseProfile& profile, Time now) {
        m_pulseKind = kind;
        m_pulseStarted = now;
        m_pulseDurationMs = std::max(profile.timeMs, 0.F);
        m_pulseIntervalMs = std::max(profile.intervalMs, 0.F);
        if (m_pulseIntervalMs > 0.F && m_pulseIntervalMs < m_pulseDurationMs)
            m_pulseIntervalMs = m_pulseDurationMs;
    }

    void clearPulse() {
        m_pulseKind = EPulseKind::NONE;
        m_pulseDurationMs = 0.F;
        m_pulseIntervalMs = 0.F;
    }

    float pulseElapsedMs(Time now) const {
        return std::chrono::duration<float, std::milli>(now - m_pulseStarted).count();
    }

    void updatePulse(const SHyprionBorderConfig& config, Time now) {
        if (m_pulseKind == EPulseKind::NONE)
            return;
        const auto* profile = currentPulseProfile(config);
        if (!profile || !profile->enabled) {
            clearPulse();
            return;
        }
        if ((m_pulseKind == EPulseKind::ACTIVE && !m_active) || (m_pulseKind == EPulseKind::INACTIVE && m_active) ||
            (m_pulseKind == EPulseKind::URGENT && !m_urgent)) {
            clearPulse();
            return;
        }

        const float elapsed = pulseElapsedMs(now);
        if (m_pulseIntervalMs > 0.F) {
            if (elapsed >= m_pulseIntervalMs) {
                const auto periods = std::floor(elapsed / m_pulseIntervalMs);
                m_pulseStarted += std::chrono::duration_cast<Time::duration>(std::chrono::duration<float, std::milli>(periods * m_pulseIntervalMs));
            }
        } else if (elapsed >= m_pulseDurationMs)
            clearPulse();
    }

    void retargetOrbit(const SHyprionBorderConfig& config, Time now, const SHyprionOrbitStyle* fromOverride = nullptr) {
        advanceOrbitPhase(now);
        const SHyprionOrbitProfile* profile = m_orbitFlareMs > 0.F ? &config.orbit.flare
            : m_urgent                                            ? &config.orbit.urgent
            : m_active                                           ? &config.orbit.active
                                                                  : &config.orbit.inactive;
        const bool targetEnabled = profile && profile->enabled;
        if (targetEnabled) {
            m_orbitDirection = profile->clockwise ? -1.0 : 1.0;
            setMotion(profile->motion);
        }
        SHyprionOrbitStyle target = orbitStyle(now);
        if (targetEnabled) {
            target = {
                .timeMs = profile->timeMs,
                .length = profile->length,
                .grade = profile->grade,
                .opacity = profile->opacity,
                .phase = static_cast<float>(m_orbitPhase),
                .amount = 1.F,
            };
        } else {
            target.amount = 0.F;
        }
        if (targetEnabled == m_orbitTargetEnabled && sameOrbit(target, m_orbitTo))
            return;

        m_orbitFrom = fromOverride ? *fromOverride : orbitStyle(now);
        if (!m_orbitTargetEnabled && targetEnabled && m_orbitFrom.amount == 0.F) {
            m_orbitFrom = target;
            m_orbitFrom.amount = 0.F;
        }
        m_orbitTo = target;
        m_orbitTargetEnabled = targetEnabled;
        // Track the selected profile's lap time even when it is disabled: a flare
        // handing back to a disabled profile would otherwise keep travelling at
        // the flare's fast lap speed while it fades, adding a whole extra lap.
        m_orbitLapTimeMs = profile->timeMs;
        if (sameOrbit(m_orbitFrom, m_orbitTo))
            m_orbitProgress.settle();
        else
            m_orbitProgress.restart();
    }

    bool             m_active = false;
    bool             m_urgent = false;
    SHyprionStyle    m_from;
    Time             m_started;
    EPulseKind       m_pulseKind = EPulseKind::NONE;
    Time             m_pulseStarted;
    float            m_pulseDurationMs = 0.F;
    float            m_pulseIntervalMs = 0.F;
    // Non-zero while a focus flare is running; the value is the flare's lap time,
    // which is also how long the ignition lasts (exactly one lap).
    float m_orbitFlareMs = 0.F;
    // How long the flare fades in from the start and out to the end of the lap.
    float m_orbitFlareFadeInMs = 0.F;
    float m_orbitFlareFadeOutMs = 0.F;

    SHyprionOrbitStyle m_orbitFrom = {};
    SHyprionOrbitStyle m_orbitTo = {};
    Time m_orbitPhaseUpdated;
    float m_orbitLapTimeMs = 12000.F;
    double m_orbitPhase = 0.0;
    double m_orbitDirection = -1.0;
    double m_motionAmount = 0.35;
    int m_motionCycles = 1;
    bool m_orbitTargetEnabled = false;

    // Focus transition and orbit crossfade, both timed by Hyprland.
    CHyprionProgress m_borderProgress;
    CHyprionProgress m_orbitProgress;
    CHyprionProgress m_angleProgress{"borderangle"};
};
