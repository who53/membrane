/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
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
#include <libdroid/leds.h>

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
static bool g_display_enabled = false;
static DroidLeds *g_droid_leds = NULL;
static bool g_has_backlight = false;
static bool g_backlight_slept = false;
static ANativeWindowBuffer *g_last_buffer = NULL;
static hwc2_compat_layer_t *g_layer = NULL;

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
	static bool needs_validate = true;

	hwc2_compat_layer_set_buffer(g_layer, 0, anw, -1);

	if (needs_validate) {
		hwc2_error_t err = hwc2_compat_display_validate(
			display, &numTypes, &numReqs);

		if (err != HWC2_ERROR_NONE && err != HWC2_ERROR_HAS_CHANGES) {
			fprintf(stderr,
				"hwc2_compat_display_validate failed: err=%d\n",
				err);
		}

		if (numTypes || numReqs) {
			err = hwc2_compat_display_accept_changes(display);
			assert(err == HWC2_ERROR_NONE);
			needs_validate = true;
		} else {
			needs_validate = false;
		}
	}

	int32_t presentFence = -1;
	hwc2_error_t err = hwc2_compat_display_present(display, &presentFence);
	if (err != HWC2_ERROR_NONE) {
		fprintf(stderr,
			"hwc2_compat_display_present failed: err=%d (is compositor dead?)\n",
			err);
		return;
	}

	assert(err == HWC2_ERROR_NONE);
	if (g_last_buffer != NULL) {
		g_last_buffer->common.decRef(&g_last_buffer->common);
	}

	if (presentFence != -1) {
		close(presentFence);
	}

	g_last_buffer = anw;
	g_last_buffer->common.incRef(&g_last_buffer->common);
}

static struct ANativeWindowBuffer *
membrane_handle_present(int mfd, HWC2DisplayConfig *cfg, uint32_t present_id,
			uint32_t expected_num_fds)
{
	struct membrane_get_present_fd arg = {
		.present_id = present_id,
	};

	if (ioctl(mfd, DRM_IOCTL_MEMBRANE_GET_PRESENT_FD, &arg) < 0) {
		perror("MEMBRANE_GET_PRESENT_FD");
		return NULL;
	}

	if (arg.num_fds < 2) {
		fprintf(stderr, "membrane: insufficient fds (%u)\n",
			arg.num_fds);
		return NULL;
	}

	if (arg.num_fds > 4) {
		fprintf(stderr, "membrane: too many fds (%u)\n", arg.num_fds);
		return NULL;
	}

	buffer_handle_t handle = import_buffer_from_fds(
		cfg->width, cfg->height, g_stride, HAL_PIXEL_FORMAT_RGBA_8888,
		GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
			GRALLOC_USAGE_HW_COMPOSER,
		arg.fds, arg.num_fds);

	if (!handle)
		return NULL;

	rwb_t *rwb = rwb_new(handle);
	if (!rwb)
		return NULL;

	return rwb_get_native(rwb);
}

static void handle_dpms_event(hwc2_compat_display_t *display)
{
	g_display_enabled = !g_display_enabled;

	const bool change_backlight = g_has_backlight && g_droid_leds;

	if (!g_display_enabled && change_backlight && !g_backlight_slept) {
		droid_leds_set_backlight(g_droid_leds, 0, FALSE);
		g_backlight_slept = true;
	}

	hwc2_power_mode_t mode = g_display_enabled ? HWC2_POWER_MODE_ON :
						     HWC2_POWER_MODE_OFF;

	if (hwc2_compat_display_set_power_mode(display, mode) !=
	    HWC2_ERROR_NONE)
		return;

	if (g_display_enabled && change_backlight && g_backlight_slept) {
		guint level = droid_leds_get_backlight(g_droid_leds);

		if (level == 0)
			level = 5;

		droid_leds_set_backlight(g_droid_leds, level, FALSE);
		g_backlight_slept = false;
	}

	printf("membrane: DPMS %s\n", g_display_enabled ? "ON" : "OFF");
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
					handle_dpms_event(display);
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

	if (getenv("MEMBRANE_BACKLIGHT")) {
		GError *err = NULL;

		g_droid_leds = droid_leds_new(&err);
		if (err) {
			fprintf(stderr, "libdroid: init failed: %s\n",
				err->message);
			g_error_free(err);
			g_droid_leds = NULL;
		} else {
			g_has_backlight = true;

			(void)droid_leds_get_backlight(g_droid_leds);

			printf("libdroid: backlight control enabled\n");
		}
	}

	hwc2_compat_display_set_power_mode(display, HWC2_POWER_MODE_ON);
	hwc2_compat_display_set_vsync_enabled(display, HWC2_VSYNC_ENABLE);

	HWC2DisplayConfig *cfg = hwc2_compat_display_get_active_config(display);
	assert(cfg);

	g_layer = hwc2_compat_display_create_layer(display);
	assert(g_layer);

	hwc2_compat_layer_set_blend_mode(g_layer, HWC2_BLEND_MODE_NONE);
	hwc2_compat_layer_set_composition_type(g_layer,
					       HWC2_COMPOSITION_DEVICE);
	hwc2_compat_layer_set_source_crop(g_layer, 0.0f, 0.0f, cfg->width,
					  cfg->height);
	hwc2_compat_layer_set_display_frame(g_layer, 0, 0, cfg->width,
					    cfg->height);
	hwc2_compat_layer_set_visible_region(g_layer, 0, 0, cfg->width,
					     cfg->height);

	printf("Display %dx%d\n", cfg->width, cfg->height);

	g_stride =
		get_stride(cfg->width, cfg->height, HAL_PIXEL_FORMAT_RGBA_8888,
			   GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
				   GRALLOC_USAGE_HW_COMPOSER);

	printf("Using cached gralloc stride = %u (width = %u)\n", g_stride,
	       cfg->width);

	membrane_send_cfg(mfd, cfg);

	rwb_set_properties(cfg->width, cfg->height, g_stride,
			   HAL_PIXEL_FORMAT_RGBA_8888,
			   GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
				   GRALLOC_USAGE_HW_COMPOSER);

	membrane_event_loop(mfd, display, cfg);

	return 0;
}
