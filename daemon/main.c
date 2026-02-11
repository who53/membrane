/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hybris/hwc2/hwc2_compatibility_layer.h>
#include <libdroid/leds.h>
#include <xf86drm.h>

#include "rwb.h"

#include <log.h>
#include <membrane.h>

int hybris_gralloc_allocate(
    int width, int height, int format, int usage, buffer_handle_t* handle, uint32_t* stride);
int hybris_gralloc_release(buffer_handle_t handle, int was_allocated);
int hybris_gralloc_import_buffer(buffer_handle_t raw_handle, buffer_handle_t* out_handle);

static uint32_t g_stride = 0;
static bool g_display_enabled = false;
static DroidLeds* g_droid_leds = NULL;
static bool g_has_backlight = false;
static bool g_backlight_slept = false;
static ANativeWindowBuffer* g_last_buffer = NULL;
static hwc2_compat_layer_t* g_layer = NULL;
static bool g_needs_revalidate = true;

#define BUFFER_CACHE_SIZE 64
static struct {
    uint32_t id;
    struct ANativeWindowBuffer* anw;
} g_buffer_cache[BUFFER_CACHE_SIZE];

static void clear_buffer_cache(void) {
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
        if (g_buffer_cache[i].anw) {
            g_buffer_cache[i].anw->common.decRef(&g_buffer_cache[i].anw->common);
            g_buffer_cache[i].anw = NULL;
        }
        g_buffer_cache[i].id = 0;
    }
}

static uint32_t get_stride(int width, int height, int format, int usage) {
    buffer_handle_t handle = NULL;
    uint32_t stride = 0;

    int ret = hybris_gralloc_allocate(width, height, format, usage, &handle, &stride);

    membrane_assert(ret == 0);
    membrane_assert(handle);
    membrane_assert(stride > 0);

    hybris_gralloc_release(handle, 1);

    return stride;
}

static void membrane_send_cfg(int fd, HWC2DisplayConfig* cfg) {
    struct membrane_u2k_cfg u = {
        .w = cfg->width,
        .h = cfg->height,
        .r = (cfg->vsyncPeriod > 0) ? (int)lround(1e9 / cfg->vsyncPeriod) : 60,
        .__reserved = 0,
    };

    if (u.r <= 0)
        u.r = 60;

    int ret = ioctl(fd, DRM_IOCTL_MEMBRANE_CONFIG, &u);
    membrane_assert(ret == 0);

    membrane_debug("sent cfg %dx%d@%d", u.w, u.h, u.r);
}

static buffer_handle_t import_buffer_from_fds(int* fds, int num_fds) {
    if (num_fds < 2)
        return NULL;

    int meta_fd = fds[num_fds - 1];
    int plane_fds = num_fds - 1;

    struct stat sb;
    if (fstat(meta_fd, &sb) < 0 || sb.st_size <= 0)
        return NULL;

    int ints[64];
    if (sb.st_size > (off_t)sizeof(ints)) {
        membrane_err("metadata too large (%zd bytes)", (size_t)sb.st_size);
        return NULL;
    }

    int num_ints = sb.st_size / sizeof(int);

    lseek(meta_fd, 0, SEEK_SET);
    if (read(meta_fd, ints, sb.st_size) != (ssize_t)sb.st_size)
        return NULL;

    native_handle_t* nh = native_handle_create(plane_fds, num_ints);
    membrane_assert(nh);

    for (int i = 0; i < plane_fds; i++)
        nh->data[i] = fds[i];

    for (int i = 0; i < num_ints; i++)
        nh->data[plane_fds + i] = ints[i];

    buffer_handle_t handle = NULL;
    hybris_gralloc_import_buffer(nh, &handle);

    native_handle_delete(nh);

    return handle;
}

static void do_present_block(hwc2_compat_display_t* display, struct ANativeWindowBuffer* anw) {
    uint32_t numTypes = 0;
    uint32_t numReqs = 0;
    static bool needs_validate = true;

    if (anw != g_last_buffer || g_needs_revalidate) {
        hwc2_compat_layer_set_buffer(g_layer, 0, anw, -1);
    }

    if (needs_validate || g_needs_revalidate) {
        g_needs_revalidate = false;
        hwc2_error_t err = hwc2_compat_display_validate(display, &numTypes, &numReqs);

        if (err != HWC2_ERROR_NONE && err != HWC2_ERROR_HAS_CHANGES) {
            membrane_err("hwc2_compat_display_validate failed: err=%d", err);
        }

        if (numTypes || numReqs) {
            err = hwc2_compat_display_accept_changes(display);
            membrane_assert(err == HWC2_ERROR_NONE);
            needs_validate = true;
        } else {
            needs_validate = false;
        }
    }

    int32_t presentFence = -1;
    hwc2_error_t err = hwc2_compat_display_present(display, &presentFence);
    if (err != HWC2_ERROR_NONE) {
        membrane_err("hwc2_compat_display_present failed: err=%d (is compositor dead?)", err);
        goto out;
    }

    if (g_last_buffer != anw) {
        if (g_last_buffer != NULL) {
            g_last_buffer->common.decRef(&g_last_buffer->common);
        }
        g_last_buffer = anw;
        g_last_buffer->common.incRef(&g_last_buffer->common);
    }

out:
    if (presentFence != -1) {
        close(presentFence);
    }
}

static struct ANativeWindowBuffer* membrane_handle_present(int mfd) {
    struct membrane_get_present_fd arg = {};

    if (ioctl(mfd, DRM_IOCTL_MEMBRANE_GET_PRESENT_FD, &arg) < 0) {
        membrane_err("MEMBRANE_GET_PRESENT_FD: %s", strerror(errno));
        return NULL;
    }

    uint32_t slot = arg.buffer_id % BUFFER_CACHE_SIZE;
    if (g_buffer_cache[slot].anw && g_buffer_cache[slot].id == arg.buffer_id) {
        for (uint32_t i = 0; i < arg.num_fds; i++) {
            if (arg.fds[i] >= 0)
                close(arg.fds[i]);
        }
        struct ANativeWindowBuffer* anw = g_buffer_cache[slot].anw;
        anw->common.incRef(&anw->common);
        return anw;
    }

    if (arg.num_fds < 2) {
        membrane_err("insufficient fds (%u)", arg.num_fds);
        for (uint32_t i = 0; i < arg.num_fds; i++) {
            if (arg.fds[i] >= 0)
                close(arg.fds[i]);
        }
        return NULL;
    }

    buffer_handle_t handle = import_buffer_from_fds(arg.fds, arg.num_fds);

    for (uint32_t i = 0; i < arg.num_fds; i++) {
        if (arg.fds[i] >= 0)
            close(arg.fds[i]);
    }

    if (!handle)
        return NULL;

    rwb_t* rwb = rwb_new(handle);
    if (!rwb) {
        hybris_gralloc_release(handle, 1);
        return NULL;
    }

    struct ANativeWindowBuffer* anw = rwb_get_native(rwb);

    if (g_buffer_cache[slot].anw) {
        g_buffer_cache[slot].anw->common.decRef(&g_buffer_cache[slot].anw->common);
    }
    g_buffer_cache[slot].id = arg.buffer_id;
    g_buffer_cache[slot].anw = anw;
    anw->common.incRef(&anw->common);

    return anw;
}

static void handle_dpms_event(hwc2_compat_display_t* display, uint32_t value) {
    if (value == MEMBRANE_DPMS_NO_COMP) {
        clear_buffer_cache();
        membrane_debug("DPMS NO_COMP (cache cleared)");
        return;
    }

    g_display_enabled = (value == MEMBRANE_DPMS_ON);

    const bool change_backlight = g_has_backlight && g_droid_leds;

    if (!g_display_enabled && change_backlight && !g_backlight_slept) {
        droid_leds_set_backlight(g_droid_leds, 0, FALSE);
        g_backlight_slept = true;
    }

    hwc2_power_mode_t mode = g_display_enabled ? HWC2_POWER_MODE_ON : HWC2_POWER_MODE_OFF;

    if (hwc2_compat_display_set_power_mode(display, mode) != HWC2_ERROR_NONE)
        return;

    if (g_display_enabled && change_backlight && g_backlight_slept) {
        guint level = droid_leds_get_backlight(g_droid_leds);

        if (level == 0)
            level = 5;

        droid_leds_set_backlight(g_droid_leds, level, FALSE);
        g_backlight_slept = false;
    }

    if (g_display_enabled)
        g_needs_revalidate = true;

    membrane_debug("DPMS %s", g_display_enabled ? "ON" : "OFF");
}

static void membrane_event_loop(int mfd, hwc2_compat_display_t* display, HWC2DisplayConfig* cfg) {
    struct membrane_event ev;

    for (;;) {
        if (ioctl(mfd, DRM_IOCTL_MEMBRANE_SIGNAL, &ev) < 0) {
            if (errno == EINTR)
                continue;
            membrane_err("ioctl DRM_IOCTL_MEMBRANE_SIGNAL: %s", strerror(errno));
            continue;
        }

        if (ev.flags & MEMBRANE_DPMS_UPDATED) {
            handle_dpms_event(display, ev.value);
        }

        if (ev.flags & MEMBRANE_PRESENT_UPDATED) {
            struct ANativeWindowBuffer* anw = membrane_handle_present(mfd);

            if (anw) {
                do_present_block(display, anw);
                anw->common.decRef(&anw->common);
            }
        }
    }
}

static void on_vsync(HWC2EventListener* l, int32_t id, hwc2_display_t d, int64_t ts) { }

static void on_hotplug(HWC2EventListener* l, int32_t id, hwc2_display_t d, bool c, bool p) {
    membrane_debug("hotplug display=%lu connected=%d primary=%d", d, c, p);
}

static void on_refresh(HWC2EventListener* l, int32_t id, hwc2_display_t d) {
    membrane_debug("refresh display=%lu", d);
}

int main(void) {
    int mfd = open("/dev/dri/by-path/platform-membrane-card", O_RDWR | O_CLOEXEC);
    membrane_assert(mfd >= 0);

    drmDropMaster(mfd);

    hwc2_compat_device_t* device = hwc2_compat_device_new(false);
    membrane_assert(device);

    HWC2EventListener listener = {};
    listener.on_vsync_received = on_vsync;
    listener.on_hotplug_received = on_hotplug;
    listener.on_refresh_received = on_refresh;

    hwc2_compat_device_register_callback(device, &listener, 0);
    hwc2_compat_device_on_hotplug(device, 0, true);

    hwc2_compat_display_t* display = hwc2_compat_device_get_display_by_id(device, 0);
    membrane_assert(display);

    if (getenv("MEMBRANE_BACKLIGHT")) {
        GError* err = NULL;

        g_droid_leds = droid_leds_new(&err);
        if (err) {
            membrane_err("libdroid: init failed: %s", err->message);
            g_error_free(err);
            g_droid_leds = NULL;
        } else {
            g_has_backlight = true;

            (void)droid_leds_get_backlight(g_droid_leds);

            membrane_debug("libdroid: backlight control enabled");
        }
    }

    hwc2_compat_display_set_power_mode(display, HWC2_POWER_MODE_ON);
    hwc2_compat_display_set_vsync_enabled(display, HWC2_VSYNC_ENABLE);

    HWC2DisplayConfig* cfg = hwc2_compat_display_get_active_config(display);
    membrane_assert(cfg);

    g_layer = hwc2_compat_display_create_layer(display);
    membrane_assert(g_layer);

    hwc2_compat_layer_set_blend_mode(g_layer, HWC2_BLEND_MODE_NONE);
    hwc2_compat_layer_set_composition_type(g_layer, HWC2_COMPOSITION_DEVICE);
    hwc2_compat_layer_set_source_crop(g_layer, 0.0f, 0.0f, cfg->width, cfg->height);
    hwc2_compat_layer_set_display_frame(g_layer, 0, 0, cfg->width, cfg->height);
    hwc2_compat_layer_set_visible_region(g_layer, 0, 0, cfg->width, cfg->height);

    membrane_debug("Display %dx%d", cfg->width, cfg->height);

    g_stride = get_stride(cfg->width, cfg->height, HAL_PIXEL_FORMAT_RGBA_8888,
        GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_COMPOSER);

    membrane_debug("Using cached gralloc stride = %u (width = %u)", g_stride, cfg->width);

    membrane_send_cfg(mfd, cfg);

    rwb_set_properties(cfg->width, cfg->height, g_stride, HAL_PIXEL_FORMAT_RGBA_8888,
        GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_COMPOSER);

    membrane_event_loop(mfd, display, cfg);

    return 0;
}
