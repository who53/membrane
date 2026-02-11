/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include "membrane_drv.h"

struct membrane_pending_event {
    struct drm_pending_event base;
    struct drm_membrane_event event;
};

void membrane_send_event(struct membrane_device* mdev, u32 flags, u32 value) {
    struct drm_device* dev = &mdev->dev;
    struct drm_file* file = READ_ONCE(mdev->event_consumer);
    struct membrane_pending_event* p;
    int ret;

    if (!file)
        return;

    p = kzalloc(sizeof(*p), GFP_ATOMIC);
    if (!p)
        return;

    p->event.base.type = DRM_MEMBRANE_EVENT;
    p->event.base.length = sizeof(p->event);
    p->event.flags = flags;
    if (flags & MEMBRANE_DPMS_UPDATED)
        p->event.dpms_state = value;
    else
        p->event.num_fds = value;

    p->base.event = &p->event.base;
    p->base.file_priv = file;

    ret = drm_event_reserve_init(dev, file, &p->base, &p->event.base);
    if (ret) {
        kfree(p);
        return;
    }

    drm_send_event(dev, &p->base);
}

void membrane_present_free(struct membrane_present* p) {
    int i;
    if (!p)
        return;
    for (i = 0; i < MEMBRANE_MAX_FDS; i++) {
        if (p->files[i])
            fput(p->files[i]);
    }
    kfree(p);
}

enum hrtimer_restart membrane_vblank_timer_fn(struct hrtimer* timer) {
    struct membrane_device* mdev = container_of(timer, struct membrane_device, vblank_timer);
    struct membrane_present *p, *old;
    u32 num_fds = 0;

    p = xchg(&mdev->pending_state, NULL);
    if (p) {
        num_fds = p->num_files;
        old = xchg(&mdev->active_state, p);
        membrane_present_free(old);
        membrane_send_event(mdev, MEMBRANE_PRESENT_UPDATED, num_fds);
    }

    drm_crtc_handle_vblank(&mdev->crtc);

    if (READ_ONCE(mdev->pending_state)) {
        int r = READ_ONCE(mdev->r);
        if (r <= 0)
            r = 60;
        hrtimer_forward_now(timer, ns_to_ktime(1000000000ULL / r));
        return HRTIMER_RESTART;
    }

    return HRTIMER_NORESTART;
}

int membrane_config(struct drm_device* dev, void* data, struct drm_file* file_priv) {
    struct membrane_device* mdev = container_of(dev, struct membrane_device, dev);
    struct membrane_u2k_cfg* cfg = data;
    bool mode_changed = false;

    if (!READ_ONCE(mdev->event_consumer))
        WRITE_ONCE(mdev->event_consumer, file_priv);

    if (READ_ONCE(mdev->w) != cfg->w || READ_ONCE(mdev->h) != cfg->h
        || READ_ONCE(mdev->r) != cfg->r) {
        WRITE_ONCE(mdev->w, cfg->w);
        WRITE_ONCE(mdev->h, cfg->h);
        WRITE_ONCE(mdev->r, cfg->r);
        mode_changed = true;
    }

    if (mode_changed)
        drm_kms_helper_hotplug_event(&mdev->dev);

    return 0;
}

static void membrane_fb_destroy(struct drm_framebuffer* fb) {
    struct membrane_framebuffer* mfb = to_membrane_framebuffer(fb);
    int i;

    for (i = 0; i < MEMBRANE_MAX_FDS; i++) {
        if (mfb->files[i]) {
            fput(mfb->files[i]);
            mfb->files[i] = NULL;
        }
    }

    drm_framebuffer_cleanup(fb);
    kfree(fb);
}

static const struct drm_framebuffer_funcs membrane_fb_funcs = {
    .destroy = membrane_fb_destroy,
};

struct drm_framebuffer* membrane_fb_create(
    struct drm_device* dev, struct drm_file* file_priv, const struct drm_mode_fb_cmd2* mode_cmd) {
    struct membrane_framebuffer* mfb;
    int ret, i;

    membrane_debug("%s", __func__);
    mfb = kzalloc(sizeof(*mfb), GFP_KERNEL);
    if (!mfb) {
        return ERR_PTR(-ENOMEM);
    }

    drm_helper_mode_fill_fb_struct(&mfb->base, mode_cmd);

    ret = drm_framebuffer_init(dev, &mfb->base, &membrane_fb_funcs);
    if (ret) {
        membrane_debug("failed to initialize framebuffer");
        kfree(mfb);
        return ERR_PTR(ret);
    }

    for (i = 0; i < MEMBRANE_MAX_FDS; i++) {
        if (mode_cmd->handles[i] != 0) {
            mfb->files[i] = membrane_gem_handle_to_file(file_priv, mode_cmd->handles[i]);
            if (!mfb->files[i]) {
                membrane_debug("Failed to get file for handle %u", mode_cmd->handles[i]);
                ret = -ENOENT;
                goto err_cleanup;
            }
            mfb->handles[i] = mode_cmd->handles[i];
        }
    }

    return &mfb->base;

err_cleanup:
    while (i--) {
        if (mfb->files[i]) {
            fput(mfb->files[i]);
        }
    }

    drm_framebuffer_cleanup(&mfb->base);
    kfree(mfb);
    return ERR_PTR(ret);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
void membrane_crtc_enable(struct drm_crtc* crtc) {
#else
void membrane_crtc_enable(struct drm_crtc* crtc, struct drm_atomic_state* state) {
#endif
    struct membrane_device* mdev = container_of(crtc->dev, struct membrane_device, dev);

    membrane_debug("%s", __func__);

    membrane_send_event(mdev, MEMBRANE_DPMS_UPDATED, MEMBRANE_DPMS_ON);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
void membrane_crtc_disable(struct drm_crtc* crtc, struct drm_crtc_state* old_state) {
#else
void membrane_crtc_disable(struct drm_crtc* crtc, struct drm_atomic_state* state) {
#endif
    struct membrane_device* mdev = container_of(crtc->dev, struct membrane_device, dev);

    membrane_debug("%s", __func__);

    membrane_present_free(xchg(&mdev->active_state, NULL));
    membrane_present_free(xchg(&mdev->pending_state, NULL));

    hrtimer_cancel(&mdev->vblank_timer);

    membrane_send_event(mdev, MEMBRANE_DPMS_UPDATED, MEMBRANE_DPMS_OFF);
}

int membrane_cursor_set2(struct drm_crtc* crtc, struct drm_file* file_priv, uint32_t handle,
    uint32_t width, uint32_t height, int32_t hot_x, int32_t hot_y) {
    membrane_debug("%s", __func__);
    return 0;
}

int membrane_cursor_move(struct drm_crtc* crtc, int x, int y) {
    membrane_debug("%s", __func__);
    return 0;
}

int membrane_gamma_set(struct drm_crtc* crtc, u16* r, u16* g, u16* b, uint32_t size) {
    membrane_debug("%s", __func__);
    return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
void membrane_plane_atomic_update(struct drm_plane* plane, struct drm_plane_state* old_state) {
    struct drm_plane_state* new_state = plane->state;
#else
void membrane_plane_atomic_update(struct drm_plane* plane, struct drm_atomic_state* state) {
    struct drm_plane_state* new_state = drm_atomic_get_new_plane_state(state, plane);
#endif
    struct drm_framebuffer* fb = new_state->fb;
    struct membrane_device* mdev = container_of(plane->dev, struct membrane_device, dev);
    struct membrane_framebuffer* mfb;
    struct membrane_present *p, *old;
    int i;

    membrane_debug("%s", __func__);

    if (!fb)
        return;

    mfb = to_membrane_framebuffer(fb);

    p = kzalloc(sizeof(*p), GFP_ATOMIC);
    if (!p)
        return;

    p->buffer_id = fb->base.id;
    p->num_files = 0;
    for (i = 0; i < MEMBRANE_MAX_FDS; i++) {
        if (mfb->files[i]) {
            get_file(mfb->files[i]);
            p->files[i] = mfb->files[i];
            p->num_files++;
        }
    }

    old = xchg(&mdev->pending_state, p);
    membrane_present_free(old);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
void membrane_plane_atomic_disable(struct drm_plane* plane, struct drm_plane_state* old_state) {
#else
void membrane_plane_atomic_disable(struct drm_plane* plane, struct drm_atomic_state* state) {
#endif
    membrane_debug("%s", __func__);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
void membrane_crtc_atomic_flush(struct drm_crtc* crtc, struct drm_crtc_state* old_crtc_state) {
#else
void membrane_crtc_atomic_flush(struct drm_crtc* crtc, struct drm_atomic_state* state) {
#endif
    struct membrane_device* mdev = container_of(crtc->dev, struct membrane_device, dev);
    struct drm_pending_vblank_event* event = crtc->state->event;

    membrane_debug("%s", __func__);

    if (READ_ONCE(mdev->pending_state) && !hrtimer_active(&mdev->vblank_timer)) {
        int r = READ_ONCE(mdev->r);
        if (r <= 0)
            r = 60;
        hrtimer_start(&mdev->vblank_timer, ns_to_ktime(1000000000ULL / r), HRTIMER_MODE_REL);
    }

    if (event) {
        crtc->state->event = NULL;
        spin_lock_irq(&crtc->dev->event_lock);
        drm_crtc_send_vblank_event(crtc, event);
        spin_unlock_irq(&crtc->dev->event_lock);
    }

    drm_crtc_handle_vblank(crtc);
}

int membrane_get_present_fd(struct drm_device* dev, void* data, struct drm_file* file) {
    struct membrane_device* mdev = container_of(dev, struct membrane_device, dev);
    struct membrane_get_present_fd* args = data;
    struct membrane_present *p, *next;
    int i, count = 0;

    membrane_debug("%s", __func__);

    p = xchg(&mdev->active_state, NULL);
    if (!p) {
        args->buffer_id = 0;
        args->num_fds = 0;
        for (i = 0; i < MEMBRANE_MAX_FDS; i++)
            args->fds[i] = -1;
        return 0;
    }

    args->buffer_id = p->buffer_id;
    args->num_fds = p->num_files;

    for (i = 0; i < MEMBRANE_MAX_FDS; i++) {
        struct file* f = p->files[i];
        int fd;

        if (!f) {
            args->fds[i] = -1;
            continue;
        }

        fd = get_unused_fd_flags(O_CLOEXEC);
        if (fd < 0) {
            args->fds[i] = -1;
            continue;
        }

        get_file(f);
        fd_install(fd, f);
        args->fds[i] = fd;
        count++;
    }

    args->num_fds = count;

    next = cmpxchg(&mdev->active_state, NULL, p);
    if (next != NULL) {
        membrane_present_free(p);
    }

    return 0;
}

u32 membrane_get_vblank_counter(struct drm_device* dev, unsigned int pipe) { return 0; }

int membrane_enable_vblank(struct drm_device* dev, unsigned int pipe) { return 0; }

void membrane_disable_vblank(struct drm_device* dev, unsigned int pipe) { }
