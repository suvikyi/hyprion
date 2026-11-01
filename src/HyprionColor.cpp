#include "HyprionColor.hpp"

namespace Hyprion {
    std::array<float, 9> matrixFor(const std::array<std::array<double, 3>, 3>& matrix) {
        return {
            static_cast<float>(matrix[0][0]), static_cast<float>(matrix[1][0]), static_cast<float>(matrix[2][0]),
            static_cast<float>(matrix[0][1]), static_cast<float>(matrix[1][1]), static_cast<float>(matrix[2][1]),
            static_cast<float>(matrix[0][2]), static_cast<float>(matrix[1][2]), static_cast<float>(matrix[2][2]),
        };
    }

    std::array<float, 3> toLinearSrgb(const std::array<float, 3>& srgb) {
        return srgbToLinear(srgb);
    }

    bool isHdrTransferFunction(NColorManagement::eTransferFunction tf) {
        return tf == NColorManagement::CM_TRANSFER_FUNCTION_ST2084_PQ || tf == NColorManagement::CM_TRANSFER_FUNCTION_HLG ||
            tf == NColorManagement::CM_TRANSFER_FUNCTION_EXT_LINEAR;
    }

    float targetGamutMaxNits(const NColorManagement::SImageDescription& description) {
        switch (description.transferFunction) {
            case NColorManagement::CM_TRANSFER_FUNCTION_ST2084_PQ:
            case NColorManagement::CM_TRANSFER_FUNCTION_EXT_LINEAR:
            case NColorManagement::CM_TRANSFER_FUNCTION_LINEAR: return 10000.F;
            case NColorManagement::CM_TRANSFER_FUNCTION_HLG: return 1000.F;
            default: return description.getTFMaxLuminance();
        }
    }

    bool makeColorPipeline(const NColorManagement::PImageDescription& source, const NColorManagement::PImageDescription& target, SColorPipeline& pipeline) {
        if (!source || !target || source->value().icc.present || target->value().icc.present)
            return false;

        const auto sourcePrimaries = source->getPrimaries();
        const auto targetPrimaries = target->getPrimaries();
        const auto workingPrimaries = NColorManagement::CPrimaries::from(NColorManagement::CM_PRIMARIES_SRGB);
        if (!sourcePrimaries || !targetPrimaries || !workingPrimaries)
            return false;

        const auto& sourceDescription = source->value();
        const auto& targetDescription = target->value();
        // This pass samples and writes the same framebuffer. If a source ever
        // has a different description (for example a differently managed blur
        // buffer), refuse it instead of clipping without tone mapping.
        if (sourceDescription != targetDescription)
            return false;

        pipeline.sourceTF = static_cast<int>(sourceDescription.transferFunction);
        pipeline.targetTF = static_cast<int>(targetDescription.transferFunction);
        pipeline.sourceRange = {sourceDescription.getTFMinLuminance(), sourceDescription.getTFMaxLuminance()};
        pipeline.targetRange = {targetDescription.getTFMinLuminance(), targetDescription.getTFMaxLuminance()};

        auto toWorking = sourcePrimaries->convertMatrix(workingPrimaries);
        auto fromWorking = workingPrimaries->convertMatrix(targetPrimaries);
        pipeline.toWorking = matrixFor(toWorking.mat());
        pipeline.fromWorking = matrixFor(fromWorking.mat());

        auto       targetXYZ = targetPrimaries->value().toXYZ();
        const auto targetMatrix = targetXYZ.mat();
        pipeline.targetLuma = {
            static_cast<float>(targetMatrix[0][1]),
            static_cast<float>(targetMatrix[1][1]),
            static_cast<float>(targetMatrix[2][1]),
        };
        pipeline.targetMaxNits = targetGamutMaxNits(targetDescription);
        pipeline.reference = 80.F;
        pipeline.hdr = isHdrTransferFunction(sourceDescription.transferFunction) || isHdrTransferFunction(targetDescription.transferFunction);
        return true;
    }
}

namespace Hyprion {
    namespace {
        SHyprionColorStop sampleGradient(const SHyprionGradient& gradient, float position) {
            if (gradient.colors.empty())
                return {};
            if (gradient.colors.size() == 1)
                return gradient.colors.front();

            const float clamped = std::clamp(position, 0.F, 1.F);
            const float scaled = clamped * static_cast<float>(gradient.colors.size() - 1);
            const auto  lower = static_cast<std::size_t>(std::floor(scaled));
            const auto  upper = std::min(lower + 1, gradient.colors.size() - 1);
            const float fraction = scaled - static_cast<float>(lower);
            const auto& a = gradient.colors[lower];
            const auto& b = gradient.colors[upper];
            const auto  mixedLab = [] (const SHyprionColorStop& x, const SHyprionColorStop& y, float t) {
                const auto aLab = srgbToOklab({x.r, x.g, x.b});
                const auto bLab = srgbToOklab({y.r, y.g, y.b});
                return oklabToSrgb({
                    aLab[0] + (bLab[0] - aLab[0]) * t,
                    aLab[1] + (bLab[1] - aLab[1]) * t,
                    aLab[2] + (bLab[2] - aLab[2]) * t,
                });
            };
            const auto  mixed = mixedLab(a, b, fraction);
            return {.r = mixed[0], .g = mixed[1], .b = mixed[2], .a = a.a + (b.a - a.a) * fraction};
        }
    }

    SHyprionGradient lerpGradient(const SHyprionGradient& from, const SHyprionGradient& to, float t) {
        if (from.colors.empty())
            return to;
        if (to.colors.empty())
            return from;

        const auto count = std::clamp<std::size_t>(std::max(from.colors.size(), to.colors.size()), 1, 10);
        SHyprionGradient result;
        result.angle = to.angle;
        result.colors.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const float position = count == 1 ? 0.F : static_cast<float>(i) / static_cast<float>(count - 1);
            const auto  a = sampleGradient(from, position);
            const auto  b = sampleGradient(to, position);
            const auto  aLab = srgbToOklab({a.r, a.g, a.b});
            const auto  bLab = srgbToOklab({b.r, b.g, b.b});
            const auto  mixed = oklabToSrgb({
                aLab[0] + (bLab[0] - aLab[0]) * t,
                aLab[1] + (bLab[1] - aLab[1]) * t,
                aLab[2] + (bLab[2] - aLab[2]) * t,
            });
            result.colors.push_back({.r = mixed[0], .g = mixed[1], .b = mixed[2], .a = a.a + (b.a - a.a) * t});
        }
        return result;
    }
}
