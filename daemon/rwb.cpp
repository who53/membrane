/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include "rwb.h"
#include <windowbuffer.h>
#include <string.h>
#include <log.h>

static struct {
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int format;
	uint64_t usage;
	int initialized;
} g_config = { 0, 0, 0, 0, 0, 0 };

void rwb_set_properties(unsigned int width, unsigned int height,
			unsigned int stride, unsigned int format,
			uint64_t usage)
{
	g_config.width = width;
	g_config.height = height;
	g_config.stride = stride;
	g_config.format = format;
	g_config.usage = usage;
	g_config.initialized = 1;
}

rwb_t *rwb_new(buffer_handle_t handle)
{
	membrane_assert(g_config.initialized);

	RemoteWindowBuffer *wb = new RemoteWindowBuffer(
		g_config.width, g_config.height, g_config.stride,
		g_config.format, g_config.usage, handle);

	if (!wb) {
		return NULL;
	}

	wb->common.incRef(&wb->common);

	return reinterpret_cast<rwb_t *>(wb);
}

void rwb_destroy(rwb_t *buffer)
{
	if (buffer) {
		RemoteWindowBuffer *wb =
			reinterpret_cast<RemoteWindowBuffer *>(buffer);
		wb->common.decRef(&wb->common);
	}
}

void rwb_acquire(rwb_t *buffer)
{
	if (buffer) {
		RemoteWindowBuffer *wb =
			reinterpret_cast<RemoteWindowBuffer *>(buffer);
		wb->common.incRef(&wb->common);
	}
}

struct ANativeWindowBuffer *rwb_get_native(rwb_t *buffer)
{
	if (buffer) {
		RemoteWindowBuffer *wb =
			reinterpret_cast<RemoteWindowBuffer *>(buffer);
		return wb->getNativeBuffer();
	}
	return NULL;
}

void rwb_set_allocated(rwb_t *buffer, int allocated)
{
	if (buffer) {
		RemoteWindowBuffer *wb =
			reinterpret_cast<RemoteWindowBuffer *>(buffer);
		wb->setAllocated(allocated != 0);
	}
}

int rwb_is_allocated(rwb_t *buffer)
{
	if (buffer) {
		RemoteWindowBuffer *wb =
			reinterpret_cast<RemoteWindowBuffer *>(buffer);
		return wb->isAllocated() ? 1 : 0;
	}
	return 0;
}
