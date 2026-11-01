#include "HyprionGradeRenderer.hpp"

#include "HyprionGradeShader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

using namespace Hyprutils::Memory;
using namespace Render;
using namespace Render::GL;

namespace {
    struct SUniforms {
        GLint horizontal = -1;
        GLint vertical = -1;
        GLint snapshot = -1;
        GLint fullSnapshot = -1;
        GLint areaMode = -1;
        GLint windowBox = -1;
        GLint thickness = -1;
        GLint outset = -1;
        GLint snapshotExtent = -1;
        GLint rounding = -1;
        GLint innerRounding = -1;
        GLint roundingPower = -1;
        GLint strength = -1;
        GLint opacity = -1;
        GLint lightness = -1;
        GLint saturation = -1;
        GLint chroma = -1;
        GLint hue = -1;
        GLint tintChroma = -1;
        GLint tintStrength = -1;
        GLint orbitAmount = -1;
        GLint sourceTF = -1;
        GLint targetTF = -1;
        GLint sourceRange = -1;
        GLint targetRange = -1;
        GLint defaultReference = -1;
        GLint toDefaultPrimaries = -1;
        GLint fromDefaultPrimaries = -1;
    };

    SP<CShader> gradeShader;
    SUniforms   uniforms;

    std::array<GLfloat, 9> matrixFor(const std::array<std::array<double, 3>, 3>& matrix) {
        return {
            static_cast<GLfloat>(matrix[0][0]), static_cast<GLfloat>(matrix[1][0]), static_cast<GLfloat>(matrix[2][0]),
            static_cast<GLfloat>(matrix[0][1]), static_cast<GLfloat>(matrix[1][1]), static_cast<GLfloat>(matrix[2][1]),
            static_cast<GLfloat>(matrix[0][2]), static_cast<GLfloat>(matrix[1][2]), static_cast<GLfloat>(matrix[2][2]),
        };
    }

    std::array<GLfloat, 2> toOklabChroma(const std::array<float, 3>& srgbColor, const std::array<std::array<double, 3>, 3>& toDefault) {
        std::array<double, 3> linear{};
        for (size_t i = 0; i < linear.size(); ++i) {
            const double channel = std::clamp(static_cast<double>(srgbColor[i]), 0.0, 1.0);
            linear[i] = channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
        }

        std::array<double, 3> rgb{};
        for (size_t row = 0; row < rgb.size(); ++row) {
            for (size_t column = 0; column < linear.size(); ++column)
                rgb[row] += toDefault[row][column] * linear[column];
        }

        const auto cubeRoot = [](double value) { return std::cbrt(std::max(value, 0.0)); };
        const double l = cubeRoot(0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2]);
        const double m = cubeRoot(0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2]);
        const double s = cubeRoot(0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2]);
        return {
            static_cast<GLfloat>(1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s),
            static_cast<GLfloat>(0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s),
        };
    }

    bool safeRoundedInt(double value, int& result) {
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min() / 2) || value > static_cast<double>(std::numeric_limits<int>::max() / 2))
            return false;
        result = static_cast<int>(std::lround(value));
        return true;
    }

    void setMat3(GLint location, const std::array<GLfloat, 9>& matrix) {
        if (location >= 0)
            glUniformMatrix3fv(location, 1, GL_FALSE, matrix.data());
    }

    void setFloat(GLint location, GLfloat value) {
        if (location >= 0)
            glUniform1f(location, value);
    }

    void setInt(GLint location, GLint value) {
        if (location >= 0)
            glUniform1i(location, value);
    }
}

bool initializeHyprionGradeRenderer() {
    if (gradeShader)
        return true;
    if (!g_pHyprOpenGL || !g_pHyprOpenGL->m_eglContext)
        return false;

    g_pHyprOpenGL->makeEGLCurrent();
    auto candidate = makeShared<CShader>();
    if (!candidate->createProgram(HyprionGradeShader::VERTEX, HyprionGradeShader::FRAGMENT, true, true))
        return false;

    const auto program = candidate->program();
    uniforms = {
        .horizontal = glGetUniformLocation(program, "uHorizontal"),
        .vertical = glGetUniformLocation(program, "uVertical"),
        .snapshot = glGetUniformLocation(program, "uSnapshot"),
        .fullSnapshot = glGetUniformLocation(program, "uFullSnapshot"),
        .areaMode = glGetUniformLocation(program, "uAreaMode"),
        .windowBox = glGetUniformLocation(program, "uWindowBox"),
        .thickness = glGetUniformLocation(program, "uThickness"),
        .outset = glGetUniformLocation(program, "uOutset"),
        .snapshotExtent = glGetUniformLocation(program, "uSnapshotExtent"),
        .rounding = glGetUniformLocation(program, "uRounding"),
        .innerRounding = glGetUniformLocation(program, "uInnerRounding"),
        .roundingPower = glGetUniformLocation(program, "uRoundingPower"),
        .strength = glGetUniformLocation(program, "uStrength"),
        .opacity = glGetUniformLocation(program, "uOpacity"),
        .lightness = glGetUniformLocation(program, "uLightness"),
        .saturation = glGetUniformLocation(program, "uSaturation"),
        .chroma = glGetUniformLocation(program, "uChroma"),
        .hue = glGetUniformLocation(program, "uHue"),
        .tintChroma = glGetUniformLocation(program, "uTintChroma"),
        .tintStrength = glGetUniformLocation(program, "uTintStrength"),
        .orbitAmount = glGetUniformLocation(program, "uOrbitAmount"),
        .sourceTF = glGetUniformLocation(program, "uSourceTF"),
        .targetTF = glGetUniformLocation(program, "uTargetTF"),
        .sourceRange = glGetUniformLocation(program, "uSourceRange"),
        .targetRange = glGetUniformLocation(program, "uTargetRange"),
        .defaultReference = glGetUniformLocation(program, "uDefaultReference"),
        .toDefaultPrimaries = glGetUniformLocation(program, "uToDefaultPrimaries"),
        .fromDefaultPrimaries = glGetUniformLocation(program, "uFromDefaultPrimaries"),
    };
    gradeShader = std::move(candidate);
    return true;
}

void destroyHyprionGradeRenderer() {
    gradeShader.reset();
    uniforms = {};
}

bool hyprionDrawGrade(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionGradeParams& params) {
    if (!g_pHyprRenderer || !g_pHyprOpenGL || !monitor)
        return false;
    if (!gradeShader && !initializeHyprionGradeRenderer())
        return false;

    auto& renderData = ctx.m_data;
    if (renderData.damage.empty() || !renderData.currentFB)
        return false;

    const auto framebuffer = renderData.currentFB;
    const auto imageDescription = framebuffer->imageDescription();
    if (!imageDescription.valid() || imageDescription->value().icc.present)
        return false;

    const double modificationScale = renderData.renderModif.combinedScale();
    const double requestedThickness = std::round(std::round(static_cast<double>(params.thickness) * monitor->m_scale) * modificationScale);
    const double boundedThickness = std::clamp(requestedThickness, 0.0, 4096.0);
    const int thickness = static_cast<int>(boundedThickness);
    const double baseRounding = std::max(0.0, static_cast<double>(params.radius) * monitor->m_scale - 1.0) * modificationScale;
    const float roundingPower = std::clamp(std::isfinite(params.roundingPower) ? params.roundingPower : 2.0F, 1.0F, 10.0F);
    if (!std::isfinite(baseRounding) || baseRounding < 0.0)
        return false;

    CBox renderBox = params.box;
    renderBox.scale(monitor->m_scale).round();
    renderData.renderModif.applyToBox(renderBox);
    if (renderBox.width <= 0.0 || renderBox.height <= 0.0 || !std::isfinite(renderBox.width) || !std::isfinite(renderBox.height))
        return false;

    double rounding = baseRounding;
    double innerRounding = 0.0;
    if (params.area == SHyprionGradeArea::OUTSET) {
        const double correction = static_cast<double>(thickness) * (std::sqrt(2.0) - 1.0) * std::max(2.0 - roundingPower, 0.0);
        rounding = baseRounding > 0.0 ? baseRounding + thickness - correction : 0.0;
        innerRounding = baseRounding;
    } else if (params.area == SHyprionGradeArea::INSET) {
        innerRounding = std::max(baseRounding - thickness, 0.0);
    }

    const int roundingInt = static_cast<int>(std::clamp(rounding, 0.0, std::min(renderBox.width, renderBox.height) * 0.5));
    const int innerRoundingInt = static_cast<int>(std::clamp(innerRounding, 0.0, std::min(renderBox.width, renderBox.height) * 0.5));

    CBox targetBox = renderBox;
    targetBox.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)), monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    targetBox.round();

    int left = 0, bottom = 0, width = 0, height = 0;
    if (!safeRoundedInt(targetBox.x, left) || !safeRoundedInt(targetBox.y, bottom) || !safeRoundedInt(targetBox.width, width) ||
        !safeRoundedInt(targetBox.height, height) || width <= 0 || height <= 0)
        return false;

    CRegion borderRegion = renderData.damage.copy().intersect(renderBox);
    if (params.area != SHyprionGradeArea::FILL) {
        const double borderExtent = static_cast<double>(thickness) + rounding;
        if (renderBox.width > 2.0 * borderExtent && renderBox.height > 2.0 * borderExtent)
            borderRegion.subtract(renderBox.copy().expand(-borderExtent));
    }
    if (borderRegion.empty())
        return false;

    const auto defaultDescription = NColorManagement::getDefaultImageDescription();
    if (!defaultDescription.valid() || defaultDescription->value().icc.present)
        return false;
    const auto sourceDescription = framebuffer->imageDescription();
    if (!sourceDescription.valid() || sourceDescription->value().icc.present)
        return false;

    auto* const glFramebuffer = dynamic_cast<CGLFramebuffer*>(framebuffer.get());
    if (!glFramebuffer || glFramebuffer->getFBID() == 0)
        return false;

    auto snapshot = g_pHyprRenderer->createFB("hyprion-grade-snapshot");
    if (!snapshot || !snapshot->alloc(width, height, framebuffer->m_drmFormat))
        return false;
    snapshot->setImageDescription(sourceDescription);
    const auto snapshotTexture = snapshot->getTexture();
    if (!snapshotTexture)
        return false;

    snapshotTexture->bind();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, glFramebuffer->getFBID());
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, left, bottom, width, height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    snapshotTexture->unbind();
    framebuffer->bind();

    const auto inputSettings = g_pHyprRenderer->getCMSettings(ctx, sourceDescription, defaultDescription);
    const auto outputSettings = g_pHyprRenderer->getCMSettings(ctx, defaultDescription, imageDescription);
    const float reference = inputSettings.dstRefLuminance;
    if (!std::isfinite(reference) || reference <= 0.0F) {
        snapshot->release();
        return false;
    }

    const auto toDefault = matrixFor(inputSettings.convertMatrix);
    const auto fromDefault = matrixFor(outputSettings.convertMatrix);
    const auto projection = g_pHyprRenderer->projectBoxToTarget(ctx, renderBox);

    auto activeShader = g_pHyprOpenGL->useShader(gradeShader);
    activeShader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, projection.getMatrix());
    glUniform1i(uniforms.snapshot, 0);
    glUniform1i(uniforms.horizontal, 1);
    glUniform1i(uniforms.vertical, 2);
    setInt(uniforms.fullSnapshot, 1);
    setInt(uniforms.areaMode, static_cast<GLint>(params.area));
    glUniform4f(uniforms.windowBox, static_cast<float>(left), static_cast<float>(bottom), static_cast<float>(width), static_cast<float>(height));
    setFloat(uniforms.thickness, static_cast<float>(thickness));
    setInt(uniforms.outset, params.area == SHyprionGradeArea::OUTSET ? 1 : 0);
    setInt(uniforms.snapshotExtent, 0);
    setFloat(uniforms.rounding, static_cast<float>(roundingInt));
    setFloat(uniforms.innerRounding, static_cast<float>(innerRoundingInt));
    setFloat(uniforms.roundingPower, roundingPower);
    setFloat(uniforms.strength, params.style.strength);
    setFloat(uniforms.opacity, std::clamp(params.style.opacity * params.alpha, 0.F, 1.F));
    setFloat(uniforms.lightness, params.style.lightness);
    setFloat(uniforms.saturation, params.style.saturation);
    setFloat(uniforms.chroma, params.style.chroma);
    setFloat(uniforms.hue, params.style.hue);
    if (params.style.tintStrength > 0.F) {
        const auto tintChroma = toOklabChroma(params.style.tintColor, inputSettings.convertMatrix);
        glUniform2f(uniforms.tintChroma, tintChroma[0], tintChroma[1]);
    } else
        glUniform2f(uniforms.tintChroma, 0.F, 0.F);
    setFloat(uniforms.tintStrength, params.style.tintStrength);
    setFloat(uniforms.orbitAmount, 0.F);
    setInt(uniforms.sourceTF, static_cast<GLint>(inputSettings.sourceTF));
    setInt(uniforms.targetTF, static_cast<GLint>(outputSettings.targetTF));
    if (uniforms.sourceRange >= 0)
        glUniform2f(uniforms.sourceRange, inputSettings.srcTFRange.min, inputSettings.srcTFRange.max);
    if (uniforms.targetRange >= 0)
        glUniform2f(uniforms.targetRange, outputSettings.dstTFRange.min, outputSettings.dstTFRange.max);
    setFloat(uniforms.defaultReference, reference);
    setMat3(uniforms.toDefaultPrimaries, toDefault);
    setMat3(uniforms.fromDefaultPrimaries, fromDefault);

    glActiveTexture(GL_TEXTURE0);
    snapshotTexture->bind();
    glBindVertexArray(activeShader->getUniformLocation(SHADER_SHADER_VAO));

    const bool previousBlend = glIsEnabled(GL_BLEND);
    g_pHyprOpenGL->blend(params.area == SHyprionGradeArea::OUTSET);
    borderRegion.forEachRect([&](const auto& rect) {
        g_pHyprOpenGL->scissor(ctx, &rect, renderData.transformDamage);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });
    g_pHyprOpenGL->disableScissor();
    g_pHyprOpenGL->blend(previousBlend);

    glBindVertexArray(0);
    snapshotTexture->unbind();
    snapshot->release();
    return true;
}
