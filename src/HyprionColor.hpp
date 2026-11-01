#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <hyprland/src/helpers/cm/ColorManagement.hpp>
#include "HyprionConfig.hpp"

namespace Hyprion {
    inline std::array<float, 3> srgbToLinear(const std::array<float, 3>& srgb) {
        std::array<float, 3> linear{};
        for (std::size_t i = 0; i < linear.size(); ++i) {
            const double channel = std::clamp(static_cast<double>(srgb[i]), 0.0, 1.0);
            linear[i] = static_cast<float>(channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4));
        }
        return linear;
    }

    inline std::array<float, 3> linearToSrgb(const std::array<float, 3>& linear) {
        std::array<float, 3> srgb{};
        for (std::size_t i = 0; i < srgb.size(); ++i) {
            const double channel = std::clamp(static_cast<double>(linear[i]), 0.0, 1.0);
            srgb[i] = static_cast<float>(channel <= 0.0031308 ? channel * 12.92 : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055);
        }
        return srgb;
    }

    inline float lerpHue(float from, float to, float t) {
        const float delta = std::fmod(to - from + 540.F, 360.F) - 180.F;
        return from + delta * t;
    }

    inline std::array<float, 3> oklabToSrgb(const std::array<float, 3>& lab) {
        const float l_ = lab[0] + 0.3963377774F * lab[1] + 0.2158037573F * lab[2];
        const float m_ = lab[0] - 0.1055613458F * lab[1] - 0.0638541728F * lab[2];
        const float s_ = lab[0] - 0.0894841775F * lab[1] - 1.2914855480F * lab[2];
        const float l = l_ * l_ * l_;
        const float m = m_ * m_ * m_;
        const float s = s_ * s_ * s_;
        return linearToSrgb({
            4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s,
            -1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s,
            -0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s,
        });
    }

    inline std::array<float, 3> srgbToOklab(const std::array<float, 3>& srgb) {
        const auto rgb = srgbToLinear(srgb);
        const auto cubeRoot = [](float value) { return std::cbrt(std::max(value, 0.F)); };
        const float l = cubeRoot(0.4122214708F * rgb[0] + 0.5363325363F * rgb[1] + 0.0514459929F * rgb[2]);
        const float m = cubeRoot(0.2119034982F * rgb[0] + 0.6806995451F * rgb[1] + 0.1073969566F * rgb[2]);
        const float s = cubeRoot(0.0883024619F * rgb[0] + 0.2817188376F * rgb[1] + 0.6299787005F * rgb[2]);
        return {
            0.2104542553F * l + 0.7936177850F * m - 0.0040720468F * s,
            1.9779984951F * l - 2.4285922050F * m + 0.4505937099F * s,
            0.0259040371F * l + 0.7827717662F * m - 0.8086757660F * s,
        };
    }

    struct SColorPipeline {
        int                  sourceTF = 0;
        int                  targetTF = 0;
        std::array<float, 2> sourceRange{};
        std::array<float, 2> targetRange{};
        std::array<float, 9> toWorking{};
        std::array<float, 9> fromWorking{};
        std::array<float, 3> targetLuma{};
        float                targetMaxNits = 80.F;
        float                reference = 80.F;
        bool                 hdr = false;
    };

    std::array<float, 9> matrixFor(const std::array<std::array<double, 3>, 3>& matrix);
    std::array<float, 3> toLinearSrgb(const std::array<float, 3>& srgb);
    bool makeColorPipeline(const NColorManagement::PImageDescription& source, const NColorManagement::PImageDescription& target, SColorPipeline& pipeline);

    SHyprionGradient lerpGradient(const SHyprionGradient& from, const SHyprionGradient& to, float t);
}
