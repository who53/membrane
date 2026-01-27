/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#ifndef _MEMBRANE_DRV_H_
#define _MEMBRANE_DRV_H_

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/hrtimer.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_gem.h>

#include "uapi/membrane.h"

#define MAX_PRESENTS 64

struct membrane_present {
	u32 id;
	u32 num_files;
	struct file *files[MEMBRANE_MAX_FDS];
	int fds[MEMBRANE_MAX_FDS];
	bool fds_valid;
};

struct membrane_framebuffer {
	struct drm_framebuffer base;
	struct file *files[MEMBRANE_MAX_FDS];
	uint32_t handles[MEMBRANE_MAX_FDS];
};

struct membrane_device {
	struct drm_device dev;
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct drm_file *event_consumer;
	atomic_t next_present_id;
	struct membrane_present presents[MAX_PRESENTS];

	int w, h, r;
	int buf_id;

	atomic64_t pending_present;
	struct hrtimer vblank_timer;
};

int membrane_config(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);
void membrane_send_event(struct membrane_device *mdev, u32 flags,
			 u32 present_id, u32 num_fds);

static inline struct membrane_framebuffer *
to_membrane_framebuffer(struct drm_framebuffer *fb)
{
	return container_of(fb, struct membrane_framebuffer, base);
}

struct drm_framebuffer *
membrane_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		   const struct drm_mode_fb_cmd2 *mode_cmd);
void membrane_crtc_enable(struct drm_crtc *crtc);
void membrane_crtc_disable(struct drm_crtc *crtc);
int membrane_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
			 uint32_t handle, uint32_t width, uint32_t height,
			 int32_t hot_x, int32_t hot_y);
int membrane_cursor_move(struct drm_crtc *crtc, int x, int y);
int membrane_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		       uint32_t size);
int membrane_set_config(struct drm_mode_set *set);
int membrane_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct drm_pending_vblank_event *event, uint32_t flags);
int membrane_prime_fd_to_handle(struct drm_device *dev,
				struct drm_file *file_priv, int prime_fd,
				uint32_t *handle);
int membrane_prime_handle_to_fd(struct drm_device *dev,
				struct drm_file *file_priv, uint32_t handle,
				uint32_t flags, int *prime_fd);
int membrane_get_present_fd(struct drm_device *dev, void *data,
			    struct drm_file *file);
enum hrtimer_restart membrane_vblank_timer_fn(struct hrtimer *timer);

void membrane_gem_free_object(struct drm_gem_object *obj);
struct file *membrane_gem_handle_to_file(struct drm_file *file_priv,
					 uint32_t handle);

static const struct drm_ioctl_desc membrane_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MEMBRANE_GET_PRESENT_FD, membrane_get_present_fd,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MEMBRANE_CONFIG, membrane_config, DRM_UNLOCKED),
};

#define membrane_debug(fmt, ...) \
	pr_debug("membrane: %s: " fmt "\n", __func__, ##__VA_ARGS__)

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
#define drm_dev_put(dev) drm_dev_unref(dev)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
#define drm_gem_object_put(obj) drm_gem_object_unreference_unlocked(obj)
#endif

#endif
