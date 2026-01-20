/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include "membrane_drv.h"

struct drm_framebuffer *
membrane_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	membrane_debug("%s", __func__);
	fb = kzalloc(sizeof(*fb), GFP_ATOMIC);
	return fb;
}

void membrane_crtc_enable(struct drm_crtc *crtc)
{
	membrane_debug("%s", __func__);
}

void membrane_crtc_disable(struct drm_crtc *crtc)
{
	membrane_debug("%s", __func__);
}

int membrane_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
			 uint32_t handle, uint32_t width, uint32_t height,
			 int32_t hot_x, int32_t hot_y)
{
	membrane_debug("%s", __func__);
	return 0;
}

int membrane_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	membrane_debug("%s", __func__);
	return 0;
}

int membrane_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		       uint32_t size)
{
	membrane_debug("%s", __func__);
	return 0;
}

int membrane_set_config(struct drm_mode_set *set)
{
	membrane_debug("%s", __func__);
	if (!set->fb || set->num_connectors == 0) {
		membrane_crtc_disable(set->crtc);
		return 0;
	}

	membrane_crtc_enable(set->crtc);

	return 0;
}

int membrane_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct drm_pending_vblank_event *event, uint32_t flags)
{
	membrane_debug("%s", __func__);
	return 0;
}

int membrane_prime_fd_to_handle(struct drm_device *dev,
				struct drm_file *file_priv, int prime_fd,
				uint32_t *handle)
{
	membrane_debug("%s", __func__);
	return 0;
}
