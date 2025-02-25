/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "hwc-platform-meson"

#include "platformmeson.h"
#include "drmdevice.h"
#include "platform.h"

#include <drm/drm_fourcc.h>
#include <stdatomic.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cinttypes>

#include <hardware/gralloc.h>
#include <log/log.h>
#include "gralloc_priv.h"

namespace android {

Importer *Importer::CreateInstance(DrmDevice *drm) {
  MesonImporter *importer = new MesonImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the meson importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}

#if defined(MALI_GRALLOC_INTFMT_AFBC_BASIC) && \
    defined(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)
uint64_t MesonImporter::ConvertGrallocFormatToDrmModifiers(uint64_t flags) {
  uint64_t features = 0UL;

  if (flags & MALI_GRALLOC_INTFMT_AFBC_BASIC)
    features |= AFBC_FORMAT_MOD_BLOCK_SIZE_16x16;

  if (flags & MALI_GRALLOC_INTFMT_AFBC_SPLITBLK)
    features |= (AFBC_FORMAT_MOD_SPLIT | AFBC_FORMAT_MOD_SPARSE);

  if (flags & MALI_GRALLOC_INTFMT_AFBC_WIDEBLK)
    features |= AFBC_FORMAT_MOD_BLOCK_SIZE_32x8;

  if (flags & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS)
    features |= AFBC_FORMAT_MOD_TILED;

  if (features)
    return DRM_FORMAT_MOD_ARM_AFBC(features | AFBC_FORMAT_MOD_YTR);

  return 0;
}
#else
uint64_t MesonImporter::ConvertGrallocFormatToDrmModifiers(
    uint64_t /* flags */) {
  return 0;
}
#endif

// 导入 buffer
int MesonImporter::ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  uint64_t modifiers[4] = {0};

  memset(bo, 0, sizeof(hwc_drm_bo_t));

  private_handle_t const *hnd = reinterpret_cast<private_handle_t const *>(
      handle);
  if (!hnd)
    return -EINVAL;

  // We can't import these types of buffers.
  // These buffers should have been filtered out with CanImportBuffer()
  if (!(hnd->usage & GRALLOC_USAGE_HW_FB))
    return -EINVAL;

  uint32_t gem_handle;
  int ret = drmPrimeFDToHandle(drm_->fd(), hnd->share_fd, &gem_handle);
  if (ret) {
    ALOGE("failed to import prime fd %d ret=%d", hnd->share_fd, ret);
    return ret;
  }

  int32_t fmt = ConvertHalFormatToDrm(hnd->req_format);
  if (fmt < 0)
    return fmt;

  modifiers[0] = ConvertGrallocFormatToDrmModifiers(hnd->internal_format);

  bo->width = hnd->width;
  bo->height = hnd->height;
  bo->hal_format = hnd->req_format;
  bo->format = fmt;
  bo->usage = hnd->usage;
  bo->pixel_stride = hnd->stride;
  bo->pitches[0] = hnd->byte_stride;
  bo->gem_handles[0] = gem_handle;
  bo->offsets[0] = 0;

  ret = drmModeAddFB2WithModifiers(drm_->fd(), bo->width, bo->height,
                                   bo->format, bo->gem_handles, bo->pitches,
                                   bo->offsets, modifiers, &bo->fb_id,
                                   modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);

  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    return ret;
  }

  return ret;
}

bool MesonImporter::CanImportBuffer(buffer_handle_t handle) {
  private_handle_t const *hnd = reinterpret_cast<private_handle_t const *>(
      handle);
  return hnd && (hnd->usage & GRALLOC_USAGE_HW_FB);
}

std::unique_ptr<Planner> Planner::CreateInstance(DrmDevice *) {
  std::unique_ptr<Planner> planner(new Planner);
  planner->AddStage<PlanStageGreedy>();
  return planner;
}
}  // namespace android
