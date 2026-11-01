#pragma once

#include <array>
#include <drm_fourcc.h>
#include <cstdint>
#include <unordered_map>

#include <hyprland/src/defines.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/helpers/cm/ColorManagement.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

namespace Hyprion {
    struct SSnapshotSet {
        SP<Render::IFramebuffer> horizontal;
        SP<Render::IFramebuffer> vertical;
        int              width = 0;
        int              height = 0;
        int              maxStrip = 0;
        DRMFormat        format = DRM_FORMAT_INVALID;
        uint64_t         imageDescriptionId = 0;

        int              failedWidth = 0;
        int              failedHeight = 0;
        int              failedStrip = 0;
        DRMFormat        failedFormat = DRM_FORMAT_INVALID;
        uint64_t         failedImageDescriptionId = 0;
    };

    struct SAtlas {
        SP<Render::IFramebuffer> fb;
        Vector2D         size;
        DRMFormat        format = DRM_FORMAT_INVALID;
        uint64_t         descriptionId = 0;
    };

    class CSnapshotCache {
      public:
        bool                  ensureStrips(PHLMONITOR monitor, const SP<Render::IFramebuffer>& source, const NColorManagement::PImageDescription& description, int width,
                                            int height, int strip);
        const SSnapshotSet*   strips(PHLMONITOR monitor) const;
        void                  copyStrips(PHLMONITOR monitor, int targetWidth, int targetHeight, int left, int bottom, int width, int height, int strip);
        SP<Render::IFramebuffer>      ensureAtlas(PHLMONITOR monitor, const SP<Render::IFramebuffer>& source, const NColorManagement::PImageDescription& description);
        void                  clear();
        void                  destroyMonitor(PHLMONITOR monitor);

      private:
        std::unordered_map<uint64_t, SSnapshotSet> m_strips;
        std::unordered_map<uint64_t, SAtlas>       m_atlases;
    };
}
