/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#ifndef _MEMBRANE_DRV_H_
#define _MEMBRANE_DRV_H_

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>

#include <linux/version.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/kfifo.h>
#include "uapi/membrane.h"

#define MEMBRANE_FIFO_SIZE 64

struct membrane_device {
	struct drm_device dev;
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;

	DECLARE_KFIFO(kfifo, struct membrane_k2u_msg, MEMBRANE_FIFO_SIZE);
	DECLARE_KFIFO(fd_fifo, struct file *, MEMBRANE_FIFO_SIZE);
	wait_queue_head_t rw_wq;
	spinlock_t rw_lock;

	int w, h, r;
	int buf_id;
};

ssize_t membrane_read(struct file *f, char __user *buf, size_t len,
		      loff_t *off);
ssize_t membrane_write(struct file *f, const char __user *buf, size_t len,
		       loff_t *off);

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
int membrane_pop_fd(struct drm_device *dev, void *data, struct drm_file *file);

static const struct drm_ioctl_desc membrane_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MEMBRANE_POP_FD, membrane_pop_fd,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
};

#define membrane_debug(fmt, ...) \
	pr_err("membrane: %s: " fmt "\n", __func__, ##__VA_ARGS__)

#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
static inline void drm_dev_put(struct drm_device *dev)
{
	drm_dev_unref(dev);
}
#endif

#endif
