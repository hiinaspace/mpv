#include "config.h"

#if HAVE_VULKAN

#include "mpv/render_vk.h"
#include "options/m_config.h"
#include "video/out/gpu/context.h"
#include "video/out/gpu/libmpv_gpu.h"
#include "video/out/placebo/ra_pl.h"
#include "video/out/placebo/utils.h"
#include "video/out/vulkan/common.h"

struct priv {
  pl_log pllog;
  struct ra_ctx *ra_ctx;
  pl_vulkan vulkan;
  pl_tex wrapped_pltex;
  struct ra_tex wrapped_tex;
  VkImage wrapped_image;
  pl_vulkan_sem handoff_sem;
  VkImageLayout held_layout;
  uint32_t held_qf;
  bool image_released;
};

static int init(struct libmpv_gpu_context *ctx, mpv_render_param *params) {
  ctx->priv = talloc_zero(NULL, struct priv);
  struct priv *p = ctx->priv;

  mpv_vulkan_init_params *init_params =
      get_mpv_render_param(params, MPV_RENDER_PARAM_VULKAN_INIT_PARAMS, NULL);
  if (!init_params || !init_params->instance || !init_params->phys_device ||
      !init_params->device || !init_params->queue_graphics.count) {
    return MPV_ERROR_INVALID_PARAMETER;
  }

  p->pllog = mppl_log_create(p, ctx->log);
  if (!p->pllog)
    return MPV_ERROR_NOMEM;

  VkPhysicalDeviceHostQueryResetFeatures host_query_reset = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
      .pNext = NULL,
      .hostQueryReset = VK_TRUE,
  };
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
      .pNext = &host_query_reset,
      .timelineSemaphore = VK_TRUE,
  };
  VkPhysicalDeviceFeatures2 fallback_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &timeline_semaphore,
  };
  const VkPhysicalDeviceFeatures2 *features =
      init_params->features ? init_params->features : &fallback_features;

  p->vulkan = pl_vulkan_import(
      p->pllog, pl_vulkan_import_params(
                        .instance = init_params->instance,
                        .get_proc_addr = init_params->get_proc_addr,
                        .phys_device = init_params->phys_device,
                        .device = init_params->device,
                        .extensions = init_params->extensions,
                        .num_extensions = init_params->num_extensions,
                        .queue_graphics =
                            {
                                .index = init_params->queue_graphics.index,
                                .count = init_params->queue_graphics.count,
                            },
                        .queue_compute =
                            {
                                .index = init_params->queue_compute.index,
                                .count = init_params->queue_compute.count,
                            },
                        .queue_transfer =
                            {
                                .index = init_params->queue_transfer.index,
                                .count = init_params->queue_transfer.count,
                            },
                        .features = features,
                        .lock_queue = init_params->lock_queue,
                        .unlock_queue = init_params->unlock_queue,
                        .queue_ctx = init_params->queue_ctx,
                        .no_compute = init_params->no_compute,
                        .max_glsl_version = init_params->max_glsl_version,
                        .max_api_version = init_params->max_api_version, ));
  if (!p->vulkan) {
    MP_FATAL(ctx, "Failed importing external Vulkan device.\n");
    return MPV_ERROR_UNSUPPORTED;
  }

  p->ra_ctx = talloc_zero(p, struct ra_ctx);
  p->ra_ctx->log = ctx->log;
  p->ra_ctx->global = ctx->global;
  p->ra_ctx->opts = (struct ra_ctx_opts){
      .allow_sw = true,
  };
  p->ra_ctx->ra = ra_create_pl(p->vulkan->gpu, ctx->log);
  if (!p->ra_ctx->ra)
    return MPV_ERROR_UNSUPPORTED;

  p->handoff_sem.sem = pl_vulkan_sem_create(
      p->vulkan->gpu,
      pl_vulkan_sem_params(.type = VK_SEMAPHORE_TYPE_TIMELINE,
                           .initial_value = 0));
  if (!p->handoff_sem.sem)
    return MPV_ERROR_UNSUPPORTED;

  ctx->ra_ctx = p->ra_ctx;
  return 0;
}

static int wrap_fbo(struct libmpv_gpu_context *ctx, mpv_render_param *params,
                    struct ra_tex **out) {
  struct priv *p = ctx->priv;

  mpv_vulkan_image *image =
      get_mpv_render_param(params, MPV_RENDER_PARAM_VULKAN_IMAGE, NULL);
  if (!image || !image->image || image->width <= 0 || image->height <= 0 ||
      !image->format || !image->usage) {
    return MPV_ERROR_INVALID_PARAMETER;
  }

  if (p->wrapped_pltex && p->wrapped_image != image->image) {
    pl_tex_destroy(p->vulkan->gpu, &p->wrapped_pltex);
    p->wrapped_image = VK_NULL_HANDLE;
    p->wrapped_tex = (struct ra_tex){0};
    p->image_released = false;
  }

  if (!p->wrapped_pltex) {
        p->wrapped_pltex = pl_vulkan_wrap(
        p->vulkan->gpu,
        pl_vulkan_wrap_params(.image = image->image, .aspect = image->aspect,
                              .width = image->width, .height = image->height,
                              .depth = image->depth,
                              .format = image->format, .usage = image->usage));
    if (!p->wrapped_pltex) {
      MP_FATAL(ctx, "Failed wrapping external VkImage.\n");
      return MPV_ERROR_INVALID_PARAMETER;
    }
    p->wrapped_image = image->image;
  }

  if (!p->image_released) {
    p->held_qf = image->queue_family;
    pl_vulkan_release_ex(
        p->vulkan->gpu,
        pl_vulkan_release_params(.tex = p->wrapped_pltex,
                                 .layout = image->layout,
                                 .qf = image->queue_family));
    p->image_released = true;
  }

  if (!mppl_wrap_tex(p->ra_ctx->ra, p->wrapped_pltex, &p->wrapped_tex)) {
    MP_FATAL(ctx, "Failed mapping wrapped VkImage into ra_tex.\n");
    return MPV_ERROR_GENERIC;
  }

  *out = &p->wrapped_tex;
  return 0;
}

static void done_frame(struct libmpv_gpu_context *ctx, bool ds) {
  struct priv *p = ctx->priv;
  (void)ds;

  if (!p->wrapped_pltex)
    return;

  pl_gpu_finish(p->vulkan->gpu);
  p->handoff_sem.value += 1;
  p->held_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if (!pl_vulkan_hold_ex(
          p->vulkan->gpu,
          pl_vulkan_hold_params(.tex = p->wrapped_pltex,
                                .layout = p->held_layout,
                                .qf = p->held_qf,
                                .semaphore = p->handoff_sem))) {
    MP_ERR(ctx, "Failed returning external VkImage to caller ownership.\n");
  } else {
    p->image_released = false;
  }
}

static void destroy(struct libmpv_gpu_context *ctx) {
  struct priv *p = ctx->priv;

  if (p->wrapped_pltex)
    pl_tex_destroy(p->vulkan->gpu, &p->wrapped_pltex);

  if (p->handoff_sem.sem)
    pl_vulkan_sem_destroy(p->vulkan->gpu, &p->handoff_sem.sem);

  if (p->ra_ctx && p->ra_ctx->ra) {
    p->ra_ctx->ra->fns->destroy(p->ra_ctx->ra);
    p->ra_ctx->ra = NULL;
  }

  if (p->vulkan)
    pl_vulkan_destroy(&p->vulkan);

  if (p->pllog)
    pl_log_destroy(&p->pllog);
}

const struct libmpv_gpu_context_fns libmpv_gpu_context_vk = {
    .api_name = MPV_RENDER_API_TYPE_VULKAN,
    .init = init,
    .wrap_fbo = wrap_fbo,
    .done_frame = done_frame,
    .destroy = destroy,
};

#endif
