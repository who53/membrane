/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#ifndef _MEMBRANE_DRV_H_
#define _MEMBRANE_DRV_H_

#include <drm/drm_plane.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>

#include <linux/version.h>

struct membrane_device {
	struct drm_device dev;
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
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
