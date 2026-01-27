/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#ifndef RWB_H
#define RWB_H

#include <stdint.h>
#include <system/window.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rwb rwb_t;

void rwb_set_properties(unsigned int width, unsigned int height,
			unsigned int stride, unsigned int format,
			uint64_t usage);

rwb_t *rwb_new(buffer_handle_t handle);

void rwb_destroy(rwb_t *buffer);

void rwb_acquire(rwb_t *buffer);

struct ANativeWindowBuffer *rwb_get_native(rwb_t *buffer);

void rwb_set_allocated(rwb_t *buffer, int allocated);

int rwb_is_allocated(rwb_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* RWB_H */
