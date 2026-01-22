/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include <gbm_backend_abi.h>
#include <gbm.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <hardware/gralloc.h>

struct membrane_bo {
	struct gbm_bo base;
	buffer_handle_t handle;
	int meta_fd;
};

int hybris_gralloc_allocate(int width, int height, int format, int usage,
			    buffer_handle_t *handle, uint32_t *stride);
int hybris_gralloc_release(buffer_handle_t handle, int was_allocated);

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
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	return 0;
}

static struct gbm_bo *membrane_bo_create(struct gbm_device *gbm, uint32_t width,
					 uint32_t height, uint32_t format,
					 uint32_t usage,
					 const uint64_t *modifiers,
					 const unsigned int count)
{
	(void)modifiers;
	(void)count;
	(void)usage;
	struct membrane_bo *bo = calloc(1, sizeof(struct membrane_bo));
	if (!bo)
		return NULL;

	bo->base.gbm = gbm;
	bo->base.v0.width = width;
	bo->base.v0.height = height;
	bo->base.v0.format = format;

	int gralloc_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
			    GRALLOC_USAGE_HW_COMPOSER;
	//if (usage & GBM_BO_USE_SCANOUT)
	//	gralloc_usage |= GRALLOC_USAGE_HW_COMPOSER;
	//if (usage & GBM_BO_USE_RENDERING)
	//	gralloc_usage |= GRALLOC_USAGE_HW_RENDER;

	buffer_handle_t handle = NULL;
	uint32_t stride = 0;

	int ret = hybris_gralloc_allocate(width, height,
					  HAL_PIXEL_FORMAT_RGBA_8888,
					  gralloc_usage, &handle, &stride);
	if (ret != 0) {
		fprintf(stderr, "membrane: %s: gralloc_allocate failed: %d\n",
			__func__, ret);
		free(bo);
		return NULL;
	}

	bo->handle = handle;
	bo->base.v0.stride = stride * 4;

	bo->meta_fd = -1;

	native_handle_t *nh = (native_handle_t *)handle;
	int meta_size = nh->numInts * sizeof(int);

	if (meta_size > 0) {
		bo->meta_fd = memfd_create("membrane_meta", MFD_CLOEXEC);
		if (bo->meta_fd >= 0) {
			if (ftruncate(bo->meta_fd, meta_size) == -1)
				fprintf(stderr,
					"membrane: %s: ftruncate failed\n",
					__func__);
			if (write(bo->meta_fd, &nh->data[nh->numFds],
				  meta_size) == -1)
				fprintf(stderr, "membrane: %s: write failed\n",
					__func__);
			lseek(bo->meta_fd, 0, SEEK_SET);
		}
	}

	return &bo->base;
}

static struct gbm_bo *membrane_bo_import(struct gbm_device *gbm, uint32_t type,
					 void *buffer, uint32_t usage)
{
	(void)gbm;
	(void)type;
	(void)buffer;
	(void)usage;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
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
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	return NULL;
}

static void membrane_bo_unmap(struct gbm_bo *bo, void *map_data)
{
	(void)bo;
	(void)map_data;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
}

static int membrane_bo_write(struct gbm_bo *bo, const void *buf, size_t data)
{
	(void)bo;
	(void)buf;
	(void)data;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	return -1;
}

static int membrane_bo_get_plane_fd(struct gbm_bo *bo, int plane)
{
	struct membrane_bo *mbo = (struct membrane_bo *)bo;
	native_handle_t *nh = (native_handle_t *)mbo->handle;
	if (!nh)
		return -1;

	if (plane >= 0 && plane < nh->numFds) {
		return dup(nh->data[plane]);
	} else if (plane == nh->numFds && mbo->meta_fd >= 0) {
		return dup(mbo->meta_fd);
	}
	return -1;
}

static int membrane_bo_get_fd(struct gbm_bo *bo)
{
	(void)bo;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	return -1;
}

static union gbm_bo_handle membrane_bo_get_handle(struct gbm_bo *bo, int plane)
{
	(void)bo;
	(void)plane;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	union gbm_bo_handle handle = { 0 };
	return handle;
}

static uint32_t membrane_bo_get_stride(struct gbm_bo *bo, int plane)
{
	(void)plane;
	return bo->v0.stride;
}

static uint32_t membrane_bo_get_offset(struct gbm_bo *bo, int plane)
{
	(void)bo;
	(void)plane;
	return 0;
}

static uint64_t membrane_bo_get_modifier(struct gbm_bo *bo)
{
	(void)bo;
	return 0;
}

static void membrane_bo_destroy(struct gbm_bo *bo)
{
	struct membrane_bo *mbo = (struct membrane_bo *)bo;
	if (mbo->meta_fd >= 0)
		close(mbo->meta_fd);
	if (mbo->handle)
		hybris_gralloc_release(mbo->handle, 1);
	free(mbo);
}

static int membrane_bo_get_planes(struct gbm_bo *bo)
{
	struct membrane_bo *mbo = (struct membrane_bo *)bo;
	native_handle_t *nh = (native_handle_t *)mbo->handle;
	if (!nh)
		return 0;
	return nh->numFds + (mbo->meta_fd >= 0 ? 1 : 0);
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
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	return NULL;
}

static struct gbm_bo *
membrane_surface_lock_front_buffer(struct gbm_surface *surface)
{
	(void)surface;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	return NULL;
}

static void membrane_surface_release_buffer(struct gbm_surface *surface,
					    struct gbm_bo *bo)
{
	(void)surface;
	(void)bo;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
}

static int membrane_surface_has_free_buffers(struct gbm_surface *surface)
{
	(void)surface;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
	return 0;
}

static void membrane_surface_destroy(struct gbm_surface *surface)
{
	(void)surface;
	fprintf(stderr, "membrane: %s shouldnt get called\n", __func__);
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
