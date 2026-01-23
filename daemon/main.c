/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include <xf86drm.h>
#include <hybris/hwc2/hwc2_compatibility_layer.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "rwb.h"
#include "membrane.h"

int hybris_gralloc_allocate(int width, int height, int format, int usage,
			    buffer_handle_t *handle, uint32_t *stride);
int hybris_gralloc_release(buffer_handle_t handle, int was_allocated);
int hybris_gralloc_import_buffer(buffer_handle_t raw_handle,
				 buffer_handle_t *out_handle);

static uint32_t g_stride = 0;

static uint32_t get_stride(int width, int height, int format, int usage)
{
	buffer_handle_t handle = NULL;
	uint32_t stride = 0;

	int ret = hybris_gralloc_allocate(width, height, format, usage, &handle,
					  &stride);

	assert(ret == 0);
	assert(handle);
	assert(stride > 0);

	hybris_gralloc_release(handle, 1);

	return stride;
}

static void membrane_send_cfg(int fd, HWC2DisplayConfig *cfg)
{
	struct membrane_u2k_cfg u = {
		.w = cfg->width,
		.h = cfg->height,
		.r = (cfg->vsyncPeriod > 0) ?
			     (int)lround(1e9 / cfg->vsyncPeriod) :
			     60,
		.__reserved = 0,
	};

	if (u.r <= 0)
		u.r = 60;

	int ret = ioctl(fd, DRM_IOCTL_MEMBRANE_CONFIG, &u);
	assert(ret == 0);

	printf("membrane: sent cfg %dx%d@%d\n", u.w, u.h, u.r);
}

static buffer_handle_t import_buffer_from_fds(int width, int height, int stride,
					      int format, int usage, int *fds,
					      int num_fds)
{
	if (num_fds < 2)
		return NULL;

	int meta_fd = fds[num_fds - 1];
	int plane_fds = num_fds - 1;

	struct stat sb;
	if (fstat(meta_fd, &sb) < 0 || sb.st_size <= 0)
		return NULL;

	int num_ints = sb.st_size / sizeof(int);
	int *ints = calloc(num_ints, sizeof(int));
	assert(ints);

	lseek(meta_fd, 0, SEEK_SET);
	read(meta_fd, ints, sb.st_size);

	native_handle_t *nh = native_handle_create(plane_fds, num_ints);
	assert(nh);

	for (int i = 0; i < plane_fds; i++)
		nh->data[i] = fds[i];

	for (int i = 0; i < num_ints; i++)
		nh->data[plane_fds + i] = ints[i];

	buffer_handle_t handle = NULL;
	hybris_gralloc_import_buffer(nh, &handle);

	native_handle_delete(nh);
	free(ints);

	return handle;
}

static void do_present_block(hwc2_compat_display_t *display,
			     struct ANativeWindowBuffer *anw)
{
	uint32_t numTypes = 0;
	uint32_t numReqs = 0;

	hwc2_error_t err =
		hwc2_compat_display_validate(display, &numTypes, &numReqs);
	assert(err == HWC2_ERROR_NONE || err == HWC2_ERROR_HAS_CHANGES);

	if (numTypes || numReqs) {
		err = hwc2_compat_display_accept_changes(display);
		assert(err == HWC2_ERROR_NONE);
	}

	err = hwc2_compat_display_set_client_target(display, 0, anw, -1,
						    HAL_DATASPACE_UNKNOWN);
	assert(err == HWC2_ERROR_NONE);

	int32_t presentFence = -1;
	err = hwc2_compat_display_present(display, &presentFence);
	assert(err == HWC2_ERROR_NONE);

	if (presentFence >= 0)
		close(presentFence);
}

static struct ANativeWindowBuffer *
membrane_handle_present(int mfd, HWC2DisplayConfig *cfg, uint32_t present_id,
			uint32_t num_fds)
{
	int fds[8];
	int nfds = 0;
	unsigned int i;

	if (num_fds > 8) {
		fprintf(stderr, "membrane: too many fds %u\n", num_fds);
		return NULL;
	}

	for (i = 0; i < num_fds; i++) {
		struct membrane_get_present_fd arg = {
			.present_id = present_id,
			.index = i,
		};

		if (ioctl(mfd, DRM_IOCTL_MEMBRANE_GET_PRESENT_FD, &arg) < 0) {
			perror("MEMBRANE_GET_PRESENT_FD");
			goto err;
		}

		fds[nfds++] = arg.fd;
	}

	if (nfds < 2)
		goto err;

	buffer_handle_t handle = import_buffer_from_fds(
		cfg->width, cfg->height, g_stride, HAL_PIXEL_FORMAT_RGBA_8888,
		GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
			GRALLOC_USAGE_HW_COMPOSER,
		fds, nfds);

	for (i = 0; i < (unsigned int)nfds; i++)
		close(fds[i]);

	if (!handle)
		return NULL;

	rwb_set_properties(cfg->width, cfg->height, g_stride,
			   HAL_PIXEL_FORMAT_RGBA_8888,
			   GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
				   GRALLOC_USAGE_HW_COMPOSER);

	rwb_t *rwb = rwb_new(handle);
	if (!rwb)
		return NULL;

	return rwb_get_native(rwb);

err:
	for (i = 0; i < (unsigned int)nfds; i++)
		close(fds[i]);
	return NULL;
}

static void membrane_event_loop(int mfd, hwc2_compat_display_t *display,
				HWC2DisplayConfig *cfg)
{
	char buf[256];

	for (;;) {
		ssize_t len = read(mfd, buf, sizeof(buf));
		if (len < 0) {
			if (errno == EINTR)
				continue;
			perror("drm read");
			continue;
		}

		ssize_t off = 0;
		while (off < len) {
			struct drm_event *e = (struct drm_event *)(buf + off);

			if (e->type == DRM_MEMBRANE_EVENT) {
				struct drm_membrane_event *me =
					(struct drm_membrane_event *)e;

				if (me->flags & MEMBRANE_DPMS_UPDATED) {
					printf("membrane: DPMS %u\n",
					       me->present_id);
				}

				if (me->flags & MEMBRANE_PRESENT_UPDATED) {
					struct ANativeWindowBuffer *anw =
						membrane_handle_present(
							mfd, cfg,
							me->present_id,
							me->num_fds);

					if (anw) {
						do_present_block(display, anw);
						anw->common.decRef(
							&anw->common);
					}
				}
			}

			off += e->length;
		}
	}
}

static void on_vsync(HWC2EventListener *l, int32_t id, hwc2_display_t d,
		     int64_t ts)
{
}

static void on_hotplug(HWC2EventListener *l, int32_t id, hwc2_display_t d,
		       bool c, bool p)
{
	printf("hotplug display=%lu connected=%d primary=%d\n", d, c, p);
}

static void on_refresh(HWC2EventListener *l, int32_t id, hwc2_display_t d)
{
	printf("refresh display=%lu\n", d);
}

int main(void)
{
	int mfd = open("/dev/dri/by-path/platform-membrane-card",
		       O_RDWR | O_CLOEXEC);
	assert(mfd >= 0);

	drmDropMaster(mfd);

	hwc2_compat_device_t *device = hwc2_compat_device_new(false);
	assert(device);

	HWC2EventListener listener = {};
	listener.on_vsync_received = on_vsync;
	listener.on_hotplug_received = on_hotplug;
	listener.on_refresh_received = on_refresh;

	hwc2_compat_device_register_callback(device, &listener, 0);
	hwc2_compat_device_on_hotplug(device, 0, true);

	hwc2_compat_display_t *display =
		hwc2_compat_device_get_display_by_id(device, 0);
	assert(display);

	hwc2_compat_display_set_power_mode(display, HWC2_POWER_MODE_ON);
	hwc2_compat_display_set_vsync_enabled(display, HWC2_VSYNC_ENABLE);

	HWC2DisplayConfig *cfg = hwc2_compat_display_get_active_config(display);
	assert(cfg);

	hwc2_compat_layer_t *layer = hwc2_compat_display_create_layer(display);
	assert(layer);

	hwc2_compat_layer_set_composition_type(layer, HWC2_COMPOSITION_CLIENT);
	hwc2_compat_layer_set_blend_mode(layer, HWC2_BLEND_MODE_NONE);
	hwc2_compat_layer_set_source_crop(layer, 0.0f, 0.0f, cfg->width,
					  cfg->height);
	hwc2_compat_layer_set_display_frame(layer, 0, 0, cfg->width,
					    cfg->height);
	hwc2_compat_layer_set_visible_region(layer, 0, 0, cfg->width,
					     cfg->height);

	printf("Display %dx%d\n", cfg->width, cfg->height);

	g_stride =
		get_stride(cfg->width, cfg->height, HAL_PIXEL_FORMAT_RGBA_8888,
			   GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
				   GRALLOC_USAGE_HW_COMPOSER);

	printf("Using cached gralloc stride = %u (width = %u)\n", g_stride,
	       cfg->width);

	membrane_send_cfg(mfd, cfg);
	membrane_event_loop(mfd, display, cfg);

	return 0;
}
