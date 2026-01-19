/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include <gbm_backend_abi.h>
#include <gbm.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct gbm_device *membrane_device_create(int fd, uint32_t gbm_backend_version);

static const struct gbm_backend_v0 membrane_backend_v0 = {
	.backend_version = GBM_BACKEND_ABI_VERSION,
	.backend_name = "membrane",
	.create_device = membrane_device_create,
};

static const struct gbm_backend membrane_backend = {
	.v0 = membrane_backend_v0,
};

const struct gbm_backend *gbmint_get_backend(const struct gbm_core *gbm_core)
{
	(void)gbm_core;
	fprintf(stderr, "membrane: %s\n", __func__);
	return &membrane_backend;
}

static void membrane_device_destroy(struct gbm_device *gbm)
{
	fprintf(stderr, "membrane: %s\n", __func__);
	free(gbm);
}

static int membrane_device_is_format_supported(struct gbm_device *gbm,
					       uint32_t format, uint32_t usage)
{
	(void)gbm;
	(void)format;
	(void)usage;
	fprintf(stderr, "membrane: %s\n", __func__);
	return 0;
}

static int membrane_device_get_format_modifier_plane_count(
	struct gbm_device *device, uint32_t format, uint64_t modifier)
{
	(void)device;
	(void)format;
	(void)modifier;
	fprintf(stderr, "membrane: %s\n", __func__);
	return 0;
}

static struct gbm_bo *membrane_bo_create(struct gbm_device *gbm, uint32_t width,
					 uint32_t height, uint32_t format,
					 uint32_t usage,
					 const uint64_t *modifiers,
					 const unsigned int count)
{
	(void)gbm;
	(void)width;
	(void)height;
	(void)format;
	(void)usage;
	(void)modifiers;
	(void)count;
	fprintf(stderr, "membrane: %s\n", __func__);
	return NULL;
}

static struct gbm_bo *membrane_bo_import(struct gbm_device *gbm, uint32_t type,
					 void *buffer, uint32_t usage)
{
	(void)gbm;
	(void)type;
	(void)buffer;
	(void)usage;
	fprintf(stderr, "membrane: %s\n", __func__);
	return NULL;
}

static void *membrane_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y,
			     uint32_t width, uint32_t height, uint32_t flags,
			     uint32_t *stride, void **map_data)
{
	(void)bo;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)flags;
	(void)stride;
	(void)map_data;
	fprintf(stderr, "membrane: %s\n", __func__);
	return NULL;
}

static void membrane_bo_unmap(struct gbm_bo *bo, void *map_data)
{
	(void)bo;
	(void)map_data;
	fprintf(stderr, "membrane: %s\n", __func__);
}

static int membrane_bo_write(struct gbm_bo *bo, const void *buf, size_t data)
{
	(void)bo;
	(void)buf;
	(void)data;
	fprintf(stderr, "membrane: %s\n", __func__);
	return -1;
}

static int membrane_bo_get_fd(struct gbm_bo *bo)
{
	(void)bo;
	fprintf(stderr, "membrane: %s\n", __func__);
	return -1;
}

static int membrane_bo_get_planes(struct gbm_bo *bo)
{
	(void)bo;
	fprintf(stderr, "membrane: %s\n", __func__);
	return 0;
}

static union gbm_bo_handle membrane_bo_get_handle(struct gbm_bo *bo, int plane)
{
	(void)bo;
	(void)plane;
	fprintf(stderr, "membrane: %s\n", __func__);
	union gbm_bo_handle handle = { 0 };
	return handle;
}

static int membrane_bo_get_plane_fd(struct gbm_bo *bo, int plane)
{
	(void)bo;
	(void)plane;
	fprintf(stderr, "membrane: %s\n", __func__);
	return -1;
}

static uint32_t membrane_bo_get_stride(struct gbm_bo *bo, int plane)
{
	(void)bo;
	(void)plane;
	fprintf(stderr, "membrane: %s\n", __func__);
	return 0;
}

static uint32_t membrane_bo_get_offset(struct gbm_bo *bo, int plane)
{
	(void)bo;
	(void)plane;
	fprintf(stderr, "membrane: %s\n", __func__);
	return 0;
}

static uint64_t membrane_bo_get_modifier(struct gbm_bo *bo)
{
	(void)bo;
	fprintf(stderr, "membrane: %s\n", __func__);
	return 0;
}

static void membrane_bo_destroy(struct gbm_bo *bo)
{
	(void)bo;
	fprintf(stderr, "membrane: %s\n", __func__);
}

static struct gbm_surface *
membrane_surface_create(struct gbm_device *gbm, uint32_t width, uint32_t height,
			uint32_t format, uint32_t flags,
			const uint64_t *modifiers, const unsigned count)
{
	(void)gbm;
	(void)width;
	(void)height;
	(void)format;
	(void)flags;
	(void)modifiers;
	(void)count;
	fprintf(stderr, "membrane: %s\n", __func__);
	return NULL;
}

static struct gbm_bo *
membrane_surface_lock_front_buffer(struct gbm_surface *surface)
{
	(void)surface;
	fprintf(stderr, "membrane: %s\n", __func__);
	return NULL;
}

static void membrane_surface_release_buffer(struct gbm_surface *surface,
					    struct gbm_bo *bo)
{
	(void)surface;
	(void)bo;
	fprintf(stderr, "membrane: %s\n", __func__);
}

static int membrane_surface_has_free_buffers(struct gbm_surface *surface)
{
	(void)surface;
	fprintf(stderr, "membrane: %s\n", __func__);
	return 0;
}

static void membrane_surface_destroy(struct gbm_surface *surface)
{
	(void)surface;
	fprintf(stderr, "membrane: %s\n", __func__);
}

struct gbm_device *membrane_device_create(int fd, uint32_t gbm_backend_version)
{
	fprintf(stderr, "membrane: %s(fd=%d, version=%u)\n", __func__, fd,
		gbm_backend_version);
	struct gbm_device *gbm = calloc(1, sizeof(struct gbm_device));
	if (!gbm)
		return NULL;

	gbm->v0.backend_version = gbm_backend_version;
	gbm->v0.fd = fd;
	gbm->v0.name = "membrane";
	gbm->v0.destroy = membrane_device_destroy;
	gbm->v0.is_format_supported = membrane_device_is_format_supported;
	gbm->v0.get_format_modifier_plane_count =
		membrane_device_get_format_modifier_plane_count;
	gbm->v0.bo_create = membrane_bo_create;
	gbm->v0.bo_import = membrane_bo_import;
	gbm->v0.bo_map = membrane_bo_map;
	gbm->v0.bo_unmap = membrane_bo_unmap;
	gbm->v0.bo_write = membrane_bo_write;
	gbm->v0.bo_get_fd = membrane_bo_get_fd;
	gbm->v0.bo_get_planes = membrane_bo_get_planes;
	gbm->v0.bo_get_handle = membrane_bo_get_handle;
	gbm->v0.bo_get_plane_fd = membrane_bo_get_plane_fd;
	gbm->v0.bo_get_stride = membrane_bo_get_stride;
	gbm->v0.bo_get_offset = membrane_bo_get_offset;
	gbm->v0.bo_get_modifier = membrane_bo_get_modifier;
	gbm->v0.bo_destroy = membrane_bo_destroy;
	gbm->v0.surface_create = membrane_surface_create;
	gbm->v0.surface_lock_front_buffer = membrane_surface_lock_front_buffer;
	gbm->v0.surface_release_buffer = membrane_surface_release_buffer;
	gbm->v0.surface_has_free_buffers = membrane_surface_has_free_buffers;
	gbm->v0.surface_destroy = membrane_surface_destroy;

	return gbm;
}
