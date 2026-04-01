/* Copyright (C) 2026 the mpv developers
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef MPV_CLIENT_API_RENDER_VK_H_
#define MPV_CLIENT_API_RENDER_VK_H_

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mpv_vulkan_queue {
  uint32_t index;
  uint32_t count;
} mpv_vulkan_queue;

typedef struct mpv_vulkan_init_params {
  VkInstance instance;
  PFN_vkGetInstanceProcAddr get_proc_addr;
  VkPhysicalDevice phys_device;
  VkDevice device;
  const char *const *extensions;
  int num_extensions;
  mpv_vulkan_queue queue_graphics;
  mpv_vulkan_queue queue_compute;
  mpv_vulkan_queue queue_transfer;
  const VkPhysicalDeviceFeatures2 *features;
  void (*lock_queue)(void *ctx, uint32_t qf, uint32_t qidx);
  void (*unlock_queue)(void *ctx, uint32_t qf, uint32_t qidx);
  void *queue_ctx;
  bool no_compute;
  int max_glsl_version;
  uint32_t max_api_version;
} mpv_vulkan_init_params;

typedef struct mpv_vulkan_image {
  VkImage image;
  VkImageAspectFlags aspect;
  int width;
  int height;
  int depth;
  VkFormat format;
  VkImageUsageFlags usage;
  VkImageLayout layout;
  uint32_t queue_family;
} mpv_vulkan_image;

#ifdef __cplusplus
}
#endif

#endif
