#include "HyprionGradeRenderer.hpp"

#include "HyprionColor.hpp"
#include "HyprionGradeShader.hpp"
#include "HyprionSnapshotCache.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

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
        GLint mode = -1;
        GLint windowBox = -1;
        GLint thickness = -1;
        GLint outset = -1;
        GLint snapshotExtent = -1;
        GLint rounding = -1;
        GLint innerRounding = -1;
        GLint roundingPower = -1;
        GLint opacity = -1;
        GLint lightness = -1;
        GLint saturation = -1;
        GLint chroma = -1;
        GLint hue = -1;
        GLint colorizeGradient = -1;
        GLint colorizeGradientLength = -1;
        GLint colorizeGradientAngle = -1;
        GLint colorizeStrength = -1;
        GLint orbitPhase = -1;
        GLint orbitLength = -1;
        GLint orbitLightness = -1;
        GLint orbitSaturation = -1;
        GLint orbitChroma = -1;
        GLint orbitHue = -1;
        GLint orbitOpacity = -1;
        GLint orbitAmount = -1;
        GLint orbitColorizeGradient = -1;
        GLint orbitColorizeGradientLength = -1;
        GLint orbitColorizeGradientAngle = -1;
        GLint orbitColorizeStrength = -1;
        GLint cornerArcTable = -1;
        GLint sourceTF = -1;
        GLint targetTF = -1;
        GLint sourceRange = -1;
        GLint targetRange = -1;
        GLint reference = -1;
        GLint toWorkingPrimaries = -1;
        GLint fromWorkingPrimaries = -1;
        GLint targetLuma = -1;
        GLint targetMaxNits = -1;
        GLint hdr = -1;
    };

    SP<CShader>                shader;
    SUniforms                  uniforms;
    Hyprion::CSnapshotCache    snapshotCache;

    void setMat3(GLint location, const std::array<float, 9>& matrix) {
        if (location >= 0)
            glUniformMatrix3fv(location, 1, GL_FALSE, matrix.data());
    }

    void setFloat(GLint location, float value) {
        if (location >= 0)
            glUniform1f(location, value);
    }

    void setInt(GLint location, GLint value) {
        if (location >= 0)
            glUniform1i(location, value);
    }

    void setVec3(GLint location, const std::array<float, 3>& value) {
        if (location >= 0)
            glUniform3f(location, value[0], value[1], value[2]);
    }

    void setGradient(GLint arrayLocation, GLint lengthLocation, GLint angleLocation, const SHyprionGradient& gradient) {
        const auto count = std::min<std::size_t>(gradient.colors.size(), 10);
        std::array<float, 40> data{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto& stop = gradient.colors[i];
            const auto  lab = Hyprion::srgbToOklab({stop.r, stop.g, stop.b});
            data[i * 4 + 0] = lab[0];
            data[i * 4 + 1] = lab[1];
            data[i * 4 + 2] = lab[2];
            data[i * 4 + 3] = stop.a;
        }
        if (arrayLocation >= 0 && count > 0)
            glUniform4fv(arrayLocation, static_cast<GLsizei>(count), data.data());
        setInt(lengthLocation, static_cast<GLint>(count));
        setFloat(angleLocation, gradient.angle);
    }

    void setColorPipeline(const Hyprion::SColorPipeline& pipeline) {
        setInt(uniforms.sourceTF, pipeline.sourceTF);
        setInt(uniforms.targetTF, pipeline.targetTF);
        if (uniforms.sourceRange >= 0)
            glUniform2f(uniforms.sourceRange, pipeline.sourceRange[0], pipeline.sourceRange[1]);
        if (uniforms.targetRange >= 0)
            glUniform2f(uniforms.targetRange, pipeline.targetRange[0], pipeline.targetRange[1]);
        setMat3(uniforms.toWorkingPrimaries, pipeline.toWorking);
        setMat3(uniforms.fromWorkingPrimaries, pipeline.fromWorking);
        setVec3(uniforms.targetLuma, pipeline.targetLuma);
        setFloat(uniforms.targetMaxNits, pipeline.targetMaxNits);
        setFloat(uniforms.reference, pipeline.reference);
        setInt(uniforms.hdr, pipeline.hdr ? 1 : 0);
    }

    bool safeRoundedInt(double value, int& result) {
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min() / 2) ||
            value > static_cast<double>(std::numeric_limits<int>::max() / 2))
            return false;
        result = static_cast<int>(std::lround(value));
        return true;
    }

    bool initializeShader() {
        if (shader)
            return true;
        if (!g_pHyprRenderer || g_pHyprRenderer->type() != IHyprRenderer::RT_GL || !g_pHyprOpenGL || !g_pHyprOpenGL->m_eglContext)
            return false;

        g_pHyprOpenGL->makeEGLCurrent();
        auto candidate = makeShared<CShader>();
        if (!candidate->createProgram(HyprionGradeShader::VERTEX, HyprionGradeShader::FRAGMENT, true, true)) {
            LOG(Log::ERR, "hyprion: failed to compile the sampling shader");
            return false;
        }

        const auto program = candidate->program();
        uniforms = {
            .horizontal = glGetUniformLocation(program, "uHorizontal"),
            .vertical = glGetUniformLocation(program, "uVertical"),
            .snapshot = glGetUniformLocation(program, "uSnapshot"),
            .fullSnapshot = glGetUniformLocation(program, "uFullSnapshot"),
            .areaMode = glGetUniformLocation(program, "uAreaMode"),
            .mode = glGetUniformLocation(program, "uMode"),
            .windowBox = glGetUniformLocation(program, "uWindowBox"),
            .thickness = glGetUniformLocation(program, "uThickness"),
            .outset = glGetUniformLocation(program, "uOutset"),
            .snapshotExtent = glGetUniformLocation(program, "uSnapshotExtent"),
            .rounding = glGetUniformLocation(program, "uRounding"),
            .innerRounding = glGetUniformLocation(program, "uInnerRounding"),
            .roundingPower = glGetUniformLocation(program, "uRoundingPower"),
            .opacity = glGetUniformLocation(program, "uOpacity"),
            .lightness = glGetUniformLocation(program, "uLightness"),
            .saturation = glGetUniformLocation(program, "uSaturation"),
            .chroma = glGetUniformLocation(program, "uChroma"),
            .hue = glGetUniformLocation(program, "uHue"),
            .colorizeGradient = glGetUniformLocation(program, "uColorizeGradient[0]"),
            .colorizeGradientLength = glGetUniformLocation(program, "uColorizeGradientLength"),
            .colorizeGradientAngle = glGetUniformLocation(program, "uColorizeGradientAngle"),
            .colorizeStrength = glGetUniformLocation(program, "uColorizeStrength"),
            .orbitPhase = glGetUniformLocation(program, "uOrbitPhase"),
            .orbitLength = glGetUniformLocation(program, "uOrbitLength"),
            .orbitLightness = glGetUniformLocation(program, "uOrbitLightness"),
            .orbitSaturation = glGetUniformLocation(program, "uOrbitSaturation"),
            .orbitChroma = glGetUniformLocation(program, "uOrbitChroma"),
            .orbitHue = glGetUniformLocation(program, "uOrbitHue"),
            .orbitOpacity = glGetUniformLocation(program, "uOrbitOpacity"),
            .orbitAmount = glGetUniformLocation(program, "uOrbitAmount"),
            .orbitColorizeGradient = glGetUniformLocation(program, "uOrbitColorizeGradient[0]"),
            .orbitColorizeGradientLength = glGetUniformLocation(program, "uOrbitColorizeGradientLength"),
            .orbitColorizeGradientAngle = glGetUniformLocation(program, "uOrbitColorizeGradientAngle"),
            .orbitColorizeStrength = glGetUniformLocation(program, "uOrbitColorizeStrength"),
            .cornerArcTable = glGetUniformLocation(program, "uCornerArcTable[0]"),
            .sourceTF = glGetUniformLocation(program, "uSourceTF"),
            .targetTF = glGetUniformLocation(program, "uTargetTF"),
            .sourceRange = glGetUniformLocation(program, "uSourceRange"),
            .targetRange = glGetUniformLocation(program, "uTargetRange"),
            .reference = glGetUniformLocation(program, "uReference"),
            .toWorkingPrimaries = glGetUniformLocation(program, "uToWorkingPrimaries"),
            .fromWorkingPrimaries = glGetUniformLocation(program, "uFromWorkingPrimaries"),
            .targetLuma = glGetUniformLocation(program, "uTargetLuma"),
            .targetMaxNits = glGetUniformLocation(program, "uTargetMaxNits"),
            .hdr = glGetUniformLocation(program, "uHdr"),
        };
        shader = std::move(candidate);
        return true;
    }
}

bool initializeHyprionGradeRenderer() {
    return initializeShader();
}

void destroyHyprionGradeRenderer() {
    if (g_pHyprOpenGL && g_pHyprOpenGL->m_eglContext)
        g_pHyprOpenGL->makeEGLCurrent();
    snapshotCache.clear();
    shader.reset();
    uniforms = {};
}

void destroyHyprionGradeMonitor(PHLMONITOR monitor) {
    snapshotCache.destroyMonitor(monitor);
}

bool hyprionCaptureGradeSnapshot(Render::CRenderContext& ctx, PHLMONITOR monitor, SP<Render::IFramebuffer>& snapshot) {
    if (!g_pHyprRenderer || !g_pHyprOpenGL || !monitor)
        return false;
    auto& renderData = ctx.m_data;
    if (!renderData.currentFB)
        return false;
    const auto framebuffer = renderData.currentFB;
    const auto description = framebuffer->imageDescription();
    if (!description.valid() || description->value().icc.present)
        return false;
    auto* const glFramebuffer = dynamic_cast<CGLFramebuffer*>(framebuffer.get());
    if (!glFramebuffer || glFramebuffer->getFBID() == 0)
        return false;
    const double width = framebuffer->m_size.x;
    const double height = framebuffer->m_size.y;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 || width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max())
        return false;

    snapshot = snapshotCache.ensureAtlas(monitor, framebuffer, description);
    if (!snapshot)
        return false;
    const auto texture = snapshot->getTexture();
    if (!texture)
        return false;
    texture->bind();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, glFramebuffer->getFBID());
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    texture->unbind();
    framebuffer->bind();
    return true;
}

bool hyprionDrawGrade(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionGradeRequest& request) {
    if (!g_pHyprRenderer || !g_pHyprOpenGL || !monitor)
        return false;
    if (!shader && !initializeShader())
        return false;

    auto& renderData = ctx.m_data;
    if (renderData.damage.empty() || !renderData.currentFB)
        return false;

    const auto framebuffer = renderData.currentFB;
    const auto imageDescription = framebuffer->imageDescription();
    if (!imageDescription.valid() || imageDescription->value().icc.present)
        return false;

    const CBox& renderBox = request.renderBox;
    if (renderBox.width <= 0.0 || renderBox.height <= 0.0 || !std::isfinite(renderBox.width) || !std::isfinite(renderBox.height))
        return false;

    const double rawWidth = framebuffer->m_size.x;
    const double rawHeight = framebuffer->m_size.y;
    if (!std::isfinite(rawWidth) || !std::isfinite(rawHeight) || rawWidth < 1 || rawHeight < 1 ||
        rawWidth > std::numeric_limits<int>::max() || rawHeight > std::numeric_limits<int>::max())
        return false;
    const int targetWidth = static_cast<int>(rawWidth);
    const int targetHeight = static_cast<int>(rawHeight);

    CBox targetBox = renderBox;
    targetBox.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)), monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    targetBox.round();

    int left = 0, bottom = 0, width = 0, height = 0;
    if (!safeRoundedInt(targetBox.x, left) || !safeRoundedInt(targetBox.y, bottom) || !safeRoundedInt(targetBox.width, width) ||
        !safeRoundedInt(targetBox.height, height) || width <= 0 || height <= 0)
        return false;

    CRegion borderRegion = renderData.damage.copy().intersect(renderBox);
    const double borderExtent = static_cast<double>(request.thickness) + request.rounding;
    if (request.area != SHyprionGradeArea::FILL && renderBox.width > 2.0 * borderExtent && renderBox.height > 2.0 * borderExtent)
        borderRegion.subtract(renderBox.copy().expand(-borderExtent));
    if (request.clipToDamageBox && renderData.clipBox.width != 0 && renderData.clipBox.height != 0)
        borderRegion.intersect(renderData.clipBox);
    if (borderRegion.empty())
        return false;

    auto snapshotFramebuffer = request.snapshot ? request.snapshot : framebuffer;
    SP<IFramebuffer> liveBlurFramebuffer;
    if (!request.snapshot && request.source != SHyprionGradeSource::CURRENT_FRAMEBUFFER) {
        snapshotFramebuffer = monitor->resources()->m_blurFB;
        if (!snapshotFramebuffer || !snapshotFramebuffer->getTexture() || snapshotFramebuffer->m_size != framebuffer->m_size)
            return false;
        if (request.source == SHyprionGradeSource::LIVE_BLUR && request.snapshotOnly) {
            CRegion blurDamage = renderData.damage.copy().intersect(renderBox);
            liveBlurFramebuffer = g_pHyprRenderer->blurMainFramebuffer(ctx, 1.F, blurDamage);
            if (!liveBlurFramebuffer || !liveBlurFramebuffer->getTexture() || liveBlurFramebuffer->m_size != framebuffer->m_size)
                return false;
        }
    }

    const auto sourceDescription = snapshotFramebuffer->imageDescription();
    if (!sourceDescription.valid() || sourceDescription->value().icc.present)
        return false;

    auto* const glFramebuffer = dynamic_cast<CGLFramebuffer*>(snapshotFramebuffer.get());
    if (!glFramebuffer || glFramebuffer->getFBID() == 0)
        return false;

    SP<IFramebuffer> snapshotFramebufferResult;
    if (request.fullSnapshot) {
        if (request.snapshot) {
            snapshotFramebufferResult = request.snapshot;
        } else {
            if (request.source != SHyprionGradeSource::CURRENT_FRAMEBUFFER)
                return false;
            snapshotFramebufferResult = snapshotCache.ensureAtlas(monitor, framebuffer, sourceDescription);
            if (!snapshotFramebufferResult)
                return false;
            const auto snapshotTexture = snapshotFramebufferResult->getTexture();
            if (!snapshotTexture)
                return false;
            snapshotTexture->bind();
            glBindFramebuffer(GL_READ_FRAMEBUFFER, glFramebuffer->getFBID());
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, left, bottom, left, bottom, width, height);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            snapshotTexture->unbind();
            framebuffer->bind();
        }
    } else {
        const bool ready = snapshotCache.ensureStrips(monitor, snapshotFramebuffer, sourceDescription, targetWidth, targetHeight, request.snapshotExtent);
        framebuffer->bind();
        if (!ready)
            return false;
        const auto* snapshots = snapshotCache.strips(monitor);
        if (!snapshots)
            return false;

        if (request.snapshotOnly) {
            GLint previousReadFramebuffer = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);

            GLuint readFramebuffer = glFramebuffer->getFBID();
            GLuint temporaryFramebuffer = 0;
            const auto liveBlurTexture = liveBlurFramebuffer ? liveBlurFramebuffer->getTexture() : nullptr;
            if (liveBlurTexture && liveBlurTexture->m_texID != 0) {
                glGenFramebuffers(1, &temporaryFramebuffer);
                if (temporaryFramebuffer != 0) {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, temporaryFramebuffer);
                    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, liveBlurTexture->m_texID, 0);
                    readFramebuffer = temporaryFramebuffer;
                }
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            snapshotCache.copyStrips(monitor, targetWidth, targetHeight, left, bottom, width, height, request.snapshotExtent);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
            if (temporaryFramebuffer != 0)
                glDeleteFramebuffers(1, &temporaryFramebuffer);
            return true;
        }
    }

    Hyprion::SColorPipeline pipeline;
    if (!Hyprion::makeColorPipeline(sourceDescription, imageDescription, pipeline))
        return false;

    const auto projection = g_pHyprRenderer->projectBoxToTarget(ctx, renderBox);

    auto activeShader = g_pHyprOpenGL->useShader(shader);
    activeShader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, projection.getMatrix());
    if (request.fullSnapshot) {
        glUniform1i(uniforms.snapshot, 0);
        glUniform1i(uniforms.horizontal, 1);
        glUniform1i(uniforms.vertical, 2);
    } else {
        glUniform1i(uniforms.horizontal, 0);
        glUniform1i(uniforms.vertical, 1);
    }
    setInt(uniforms.fullSnapshot, request.fullSnapshot ? 1 : 0);
    setInt(uniforms.areaMode, static_cast<GLint>(request.area));
    setInt(uniforms.mode, static_cast<GLint>(request.mode));
    glUniform4f(uniforms.windowBox, static_cast<float>(left), static_cast<float>(bottom), static_cast<float>(width), static_cast<float>(height));
    setFloat(uniforms.thickness, static_cast<float>(request.thickness));
    setInt(uniforms.outset, request.area == SHyprionGradeArea::OUTSET ? 1 : 0);
    setInt(uniforms.snapshotExtent, request.fullSnapshot ? 0 : request.snapshotExtent);
    setFloat(uniforms.rounding, static_cast<float>(request.rounding));
    setFloat(uniforms.innerRounding, static_cast<float>(request.innerRounding));
    setFloat(uniforms.roundingPower, request.roundingPower);
    setFloat(uniforms.opacity, std::clamp(request.style.opacity * request.alpha, 0.0F, 1.0F));
    setFloat(uniforms.lightness, request.style.grade.lightness);
    setFloat(uniforms.saturation, request.style.grade.saturation);
    setFloat(uniforms.chroma, request.style.grade.chroma);
    setFloat(uniforms.hue, request.style.grade.hue);
    setGradient(uniforms.colorizeGradient, uniforms.colorizeGradientLength, uniforms.colorizeGradientAngle, request.style.grade.colorize.gradient);
    setFloat(uniforms.colorizeStrength, request.style.grade.colorize.strength);
    setFloat(uniforms.orbitPhase, request.orbit.phase);
    setFloat(uniforms.orbitLength, request.orbit.length);
    setFloat(uniforms.orbitLightness, request.orbit.grade.lightness);
    setFloat(uniforms.orbitSaturation, request.orbit.grade.saturation);
    setFloat(uniforms.orbitChroma, request.orbit.grade.chroma);
    setFloat(uniforms.orbitHue, request.orbit.grade.hue);
    setFloat(uniforms.orbitOpacity, std::clamp(request.orbit.opacity * request.alpha, 0.0F, 1.0F));
    setFloat(uniforms.orbitAmount, request.orbit.amount);
    setGradient(uniforms.orbitColorizeGradient, uniforms.orbitColorizeGradientLength, uniforms.orbitColorizeGradientAngle,
                request.orbit.grade.colorize.gradient);
    setFloat(uniforms.orbitColorizeStrength, request.orbit.grade.colorize.strength);
    if (uniforms.cornerArcTable >= 0)
        glUniform1fv(uniforms.cornerArcTable, static_cast<GLsizei>(request.cornerArcTable.size()), request.cornerArcTable.data());
    setColorPipeline(pipeline);

    if (request.fullSnapshot) {
        const auto snapshotTexture = snapshotFramebufferResult->getTexture();
        glActiveTexture(GL_TEXTURE0);
        snapshotTexture->bind();
    } else {
        const auto* snapshots = snapshotCache.strips(monitor);
        glActiveTexture(GL_TEXTURE0);
        snapshots->horizontal->getTexture()->bind();
        glActiveTexture(GL_TEXTURE1);
        snapshots->vertical->getTexture()->bind();
        glActiveTexture(GL_TEXTURE0);
    }
    glBindVertexArray(activeShader->getUniformLocation(SHADER_SHADER_VAO));

    const bool previousBlend = glIsEnabled(GL_BLEND);
    g_pHyprOpenGL->blend(false);
    borderRegion.forEachRect([&](const auto& rect) {
        g_pHyprOpenGL->scissor(ctx, &rect, renderData.transformDamage);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });
    g_pHyprOpenGL->disableScissor();
    g_pHyprOpenGL->blend(previousBlend);

    glBindVertexArray(0);
    if (request.fullSnapshot) {
        snapshotFramebufferResult->getTexture()->unbind();
    } else {
        glActiveTexture(GL_TEXTURE1);
        snapshotCache.strips(monitor)->vertical->getTexture()->unbind();
        glActiveTexture(GL_TEXTURE0);
        snapshotCache.strips(monitor)->horizontal->getTexture()->unbind();
    }
    return true;
}
