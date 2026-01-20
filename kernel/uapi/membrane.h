/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#ifndef _UAPI_MEMBRANE_H_
#define _UAPI_MEMBRANE_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define MEMBRANE_BUF_ID_UPDATED (1 << 0)
#define MEMBRANE_DPMS_UPDATED (1 << 1)

struct membrane_k2u_msg {
	uint32_t flags;
	int32_t buf_id;
	int32_t dpms;
};

struct membrane_u2k_cfg {
	int32_t w;
	int32_t h;
	int32_t r;
	int32_t __reserved;
};

#endif
