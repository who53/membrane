/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#ifndef _UAPI_MEMBRANE_H_
#define _UAPI_MEMBRANE_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define MEMBRANE_PRESENT_UPDATED (1 << 0)
#define MEMBRANE_DPMS_UPDATED (1 << 1)

#define DRM_MEMBRANE_EVENT 0x80000001

struct membrane_k2u_msg {
	uint32_t flags;
	int32_t present;
	int32_t dpms;
};

struct drm_membrane_event {
	struct drm_event base;
	struct membrane_k2u_msg msg;
};

struct membrane_u2k_cfg {
	int32_t w;
	int32_t h;
	int32_t r;
	int32_t __reserved;
};

struct membrane_pop_fd {
	int32_t fd;
};

#define DRM_MEMBRANE_POP_FD 0x23
#define DRM_MEMBRANE_CONFIG 0x24

#define DRM_IOCTL_MEMBRANE_POP_FD \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_MEMBRANE_POP_FD, struct membrane_pop_fd)

#define DRM_IOCTL_MEMBRANE_CONFIG \
	DRM_IOW(DRM_COMMAND_BASE + DRM_MEMBRANE_CONFIG, struct membrane_u2k_cfg)

#endif
