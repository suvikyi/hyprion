#include "HyprionSnapshotCache.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace {
    bool configureTexture(const SP<Render::ITexture>& texture) {
        if (!texture)
            return false;
        texture->bind();
        texture->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        texture->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        texture->setTexParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        texture->setTexParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        texture->unbind();
        return true;
    }

    bool allocateFramebuffer(SP<Render::IFramebuffer>& framebuffer, const std::string& name, int width, int height, DRMFormat format,
                             const NColorManagement::PImageDescription& imageDescription) {
        framebuffer = g_pHyprRenderer->createFB(name);
        if (!framebuffer || !framebuffer->alloc(width, height, format))
            return false;
        framebuffer->setImageDescription(imageDescription);
        return configureTexture(framebuffer->getTexture());
    }

    void copyClippedStrip(int targetWidth, int targetHeight, int textureWidth, int textureHeight, int64_t destinationX, int64_t destinationY, int64_t sourceX,
                          int64_t sourceY, int64_t width, int64_t height) {
        if (width <= 0 || height <= 0)
            return;

        const int64_t destinationLeft = std::max<int64_t>(destinationX, 0);
        const int64_t destinationBottom = std::max<int64_t>(destinationY, 0);
        const int64_t destinationRight = std::min<int64_t>(destinationX + width, textureWidth);
        const int64_t destinationTop = std::min<int64_t>(destinationY + height, textureHeight);
        if (destinationRight <= destinationLeft || destinationTop <= destinationBottom)
            return;

        const int64_t requestedWidth = destinationRight - destinationLeft;
        const int64_t requestedHeight = destinationTop - destinationBottom;
        const int64_t requestedSourceX = sourceX + destinationLeft - destinationX;
        const int64_t requestedSourceY = sourceY + destinationBottom - destinationY;
        const int64_t clippedLeft = std::max<int64_t>(requestedSourceX, 0);
        const int64_t clippedBottom = std::max<int64_t>(requestedSourceY, 0);
        const int64_t clippedRight = std::min<int64_t>(requestedSourceX + requestedWidth, targetWidth);
        const int64_t clippedTop = std::min<int64_t>(requestedSourceY + requestedHeight, targetHeight);
        if (clippedRight <= clippedLeft || clippedTop <= clippedBottom)
            return;

        const int64_t copyWidth = clippedRight - clippedLeft;
        const int64_t copyHeight = clippedTop - clippedBottom;
        const int64_t destX = destinationLeft + clippedLeft - requestedSourceX;
        const int64_t destY = destinationBottom + clippedBottom - requestedSourceY;
        if (destX < 0 || destY < 0 || destX + copyWidth > textureWidth || destY + copyHeight > textureHeight ||
            clippedLeft > std::numeric_limits<GLint>::max() || clippedBottom > std::numeric_limits<GLint>::max() ||
            copyWidth > std::numeric_limits<GLsizei>::max() || copyHeight > std::numeric_limits<GLsizei>::max())
            return;

        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(destX), static_cast<GLint>(destY), static_cast<GLint>(clippedLeft),
                            static_cast<GLint>(clippedBottom), static_cast<GLsizei>(copyWidth), static_cast<GLsizei>(copyHeight));
    }
}

namespace Hyprion {
    bool CSnapshotCache::ensureStrips(PHLMONITOR monitor, const SP<Render::IFramebuffer>& sourceFramebuffer,
                                      const NColorManagement::PImageDescription& imageDescription, int width, int height, int strip) {
        auto& cache = m_strips[static_cast<uint64_t>(monitor->m_id)];
        const auto descriptionId = imageDescription->id();
        const bool sameTarget = cache.width == width && cache.height == height && cache.format == sourceFramebuffer->m_drmFormat &&
            cache.imageDescriptionId == descriptionId;
        const bool ready = sameTarget && cache.horizontal && cache.vertical && cache.maxStrip >= strip;
        if (ready)
            return true;

        const bool previouslyFailed = cache.failedWidth == width && cache.failedHeight == height && cache.failedStrip == strip &&
            cache.failedFormat == sourceFramebuffer->m_drmFormat && cache.failedImageDescriptionId == descriptionId;
        if (previouslyFailed)
            return false;

        const int allocationStrip = sameTarget ? std::max(cache.maxStrip, strip) : strip;
        if (allocationStrip < 1 || allocationStrip > std::numeric_limits<int>::max() / 2) {
            LOG(Log::ERR, "hyprion: invalid snapshot strip size for monitor {}", monitor->m_id);
            return false;
        }

        SP<Render::IFramebuffer> horizontal;
        SP<Render::IFramebuffer> vertical;
        const auto monitorName = std::to_string(monitor->m_id);
        const bool allocated = allocateFramebuffer(horizontal, "hyprion-horizontal-" + monitorName, width, allocationStrip * 2, sourceFramebuffer->m_drmFormat,
                                                   imageDescription) &&
            allocateFramebuffer(vertical, "hyprion-vertical-" + monitorName, allocationStrip * 2, height, sourceFramebuffer->m_drmFormat, imageDescription);
        if (!allocated) {
            cache.failedWidth = width;
            cache.failedHeight = height;
            cache.failedStrip = strip;
            cache.failedFormat = sourceFramebuffer->m_drmFormat;
            cache.failedImageDescriptionId = descriptionId;
            LOG(Log::ERR, "hyprion: failed to allocate pixel snapshots for monitor {}", monitor->m_id);
            return false;
        }

        cache.horizontal = std::move(horizontal);
        cache.vertical = std::move(vertical);
        cache.width = width;
        cache.height = height;
        cache.maxStrip = allocationStrip;
        cache.format = sourceFramebuffer->m_drmFormat;
        cache.imageDescriptionId = descriptionId;
        cache.failedWidth = 0;
        cache.failedHeight = 0;
        cache.failedStrip = 0;
        cache.failedFormat = DRM_FORMAT_INVALID;
        cache.failedImageDescriptionId = 0;
        return true;
    }

    const SSnapshotSet* CSnapshotCache::strips(PHLMONITOR monitor) const {
        const auto it = m_strips.find(static_cast<uint64_t>(monitor->m_id));
        return it == m_strips.end() ? nullptr : &it->second;
    }

    void CSnapshotCache::copyStrips(PHLMONITOR monitor, int targetWidth, int targetHeight, int left, int bottom, int width, int height, int strip) {
        const auto* cache = strips(monitor);
        if (!cache || !cache->horizontal || !cache->vertical)
            return;

        const auto horizontal = cache->horizontal->getTexture();
        const auto vertical = cache->vertical->getTexture();
        if (!horizontal || !vertical)
            return;

        horizontal->bind();
        // Keep the long texture axis in framebuffer coordinates. Window-local
        // offsets can exceed these monitor-sized textures for offscreen windows.
        copyClippedStrip(targetWidth, targetHeight, targetWidth, cache->maxStrip * 2, left, 0, left, bottom, width, strip);
        copyClippedStrip(targetWidth, targetHeight, targetWidth, cache->maxStrip * 2, left, strip, left, static_cast<int64_t>(bottom) + height - strip, width,
                         strip);
        horizontal->unbind();

        const int64_t middleHeight = std::max<int64_t>(0, static_cast<int64_t>(height) - 2LL * strip);
        if (middleHeight == 0)
            return;
        vertical->bind();
        const int64_t middleStart = static_cast<int64_t>(bottom) + strip;
        copyClippedStrip(targetWidth, targetHeight, cache->maxStrip * 2, targetHeight, 0, middleStart, left, middleStart, strip, middleHeight);
        copyClippedStrip(targetWidth, targetHeight, cache->maxStrip * 2, targetHeight, strip, middleStart, static_cast<int64_t>(left) + width - strip,
                         middleStart, strip, middleHeight);
        vertical->unbind();
    }

    SP<Render::IFramebuffer> CSnapshotCache::ensureAtlas(PHLMONITOR monitor, const SP<Render::IFramebuffer>& source,
                                                         const NColorManagement::PImageDescription& sourceDescription) {
        auto& atlas = m_atlases[static_cast<uint64_t>(monitor->m_id)];
        const auto descriptionId = sourceDescription->id();
        if (atlas.fb && atlas.size == source->m_size && atlas.format == source->m_drmFormat && atlas.descriptionId == descriptionId)
            return atlas.fb;

        atlas.fb = g_pHyprRenderer->createFB(std::format("hyprion-grade-atlas-{}", monitor->m_id));
        if (!atlas.fb || !atlas.fb->alloc(static_cast<int>(source->m_size.x), static_cast<int>(source->m_size.y), source->m_drmFormat)) {
            atlas.fb.reset();
            return nullptr;
        }
        atlas.fb->setImageDescription(sourceDescription);
        atlas.size = source->m_size;
        atlas.format = source->m_drmFormat;
        atlas.descriptionId = descriptionId;
        return atlas.fb;
    }

    void CSnapshotCache::clear() {
        m_strips.clear();
        m_atlases.clear();
    }

    void CSnapshotCache::destroyMonitor(PHLMONITOR monitor) {
        if (!monitor)
            return;
        m_strips.erase(static_cast<uint64_t>(monitor->m_id));
        m_atlases.erase(static_cast<uint64_t>(monitor->m_id));
    }
}
