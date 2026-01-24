/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include "membrane_drv.h"

struct membrane_pending_event {
	struct drm_pending_event base;
	struct drm_membrane_event event;
};

void membrane_send_event(struct membrane_device *mdev, u32 flags,
			 u32 present_id, u32 num_fds)
{
	struct drm_device *dev = &mdev->dev;
	struct drm_file *file = READ_ONCE(mdev->event_consumer);
	struct membrane_pending_event *p;
	int ret;

	if (!file)
		return;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;

	p->event.base.type = DRM_MEMBRANE_EVENT;
	p->event.base.length = sizeof(p->event);
	p->event.flags = flags;
	p->event.present_id = present_id;
	p->event.num_fds = num_fds;

	p->base.event = &p->event.base;
	p->base.file_priv = file;

	ret = drm_event_reserve_init(dev, file, &p->base, &p->event.base);
	if (ret) {
		kfree(p);
		return;
	}

	drm_send_event(dev, &p->base);
}

enum hrtimer_restart membrane_vblank_timer_fn(struct hrtimer *timer)
{
	struct membrane_device *mdev =
		container_of(timer, struct membrane_device, vblank_timer);
	u64 val;
	u32 present_id, num_fds;

	val = atomic64_xchg(&mdev->pending_present, 0);
	if (val) {
		present_id = val >> 32;
		num_fds = val & 0xFFFFFFFF;
		membrane_send_event(mdev, MEMBRANE_PRESENT_UPDATED, present_id,
				    num_fds);
	}

	if (atomic64_read(&mdev->pending_present)) {
		int r = READ_ONCE(mdev->r);
		if (r <= 0)
			r = 60;
		hrtimer_forward_now(timer, ns_to_ktime(1000000000ULL / r));
		return HRTIMER_RESTART;
	}

	return HRTIMER_NORESTART;
}

static void membrane_queue_present(struct membrane_device *mdev, u32 present_id,
				   u32 num_fds)
{
	u64 val = ((u64)present_id << 32) | num_fds;
	atomic64_set(&mdev->pending_present, val);

	if (!hrtimer_active(&mdev->vblank_timer)) {
		int r = READ_ONCE(mdev->r);
		hrtimer_start(&mdev->vblank_timer,
			      ns_to_ktime(1000000000ULL / (r > 0 ? r : 60)),
			      HRTIMER_MODE_REL);
	}
}

int membrane_config(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct membrane_u2k_cfg *cfg = data;
	bool mode_changed = false;

	if (!READ_ONCE(mdev->event_consumer))
		WRITE_ONCE(mdev->event_consumer, file_priv);

	if (READ_ONCE(mdev->w) != cfg->w || READ_ONCE(mdev->h) != cfg->h ||
	    READ_ONCE(mdev->r) != cfg->r) {
		WRITE_ONCE(mdev->w, cfg->w);
		WRITE_ONCE(mdev->h, cfg->h);
		WRITE_ONCE(mdev->r, cfg->r);
		mode_changed = true;
	}

	if (mode_changed)
		drm_kms_helper_hotplug_event(&mdev->dev);

	return 0;
}

static void membrane_fb_destroy(struct drm_framebuffer *fb)
{
	struct membrane_framebuffer *mfb = to_membrane_framebuffer(fb);
	int i;

	for (i = 0; i < 4; i++) {
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

struct drm_framebuffer *
membrane_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct membrane_framebuffer *mfb;
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
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

	for (i = 0; i < 4; i++) {
		if (mode_cmd->handles[i] != 0) {
			uint32_t h = mode_cmd->handles[i];
			if (h < 1 || h > 4) {
				membrane_debug("Invalid handle %u", h);
				ret = -EINVAL;
				goto err_cleanup;
			}
			mfb->files[i] = mdev->imported_files[h - 1];
			if (!mfb->files[i]) {
				membrane_debug("No file for handle %u", h);
				ret = -ENOENT;
				goto err_cleanup;
			}
			get_file(mfb->files[i]);
			mfb->handles[i] = h;
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

void membrane_crtc_enable(struct drm_crtc *crtc)
{
	struct membrane_device *mdev =
		container_of(crtc->dev, struct membrane_device, dev);

	membrane_debug("%s", __func__);

	membrane_send_event(mdev, MEMBRANE_DPMS_UPDATED, 1, 0);
}

void membrane_crtc_disable(struct drm_crtc *crtc)
{
	struct membrane_device *mdev =
		container_of(crtc->dev, struct membrane_device, dev);

	membrane_debug("%s", __func__);

	if (!crtc->dev->master) {
		membrane_debug("compositor has died");
		return;
	}

	atomic64_set(&mdev->pending_present, 0);
	hrtimer_cancel(&mdev->vblank_timer);

	membrane_send_event(mdev, MEMBRANE_DPMS_UPDATED, 0, 0);
}

int membrane_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
			 uint32_t handle, uint32_t width, uint32_t height,
			 int32_t hot_x, int32_t hot_y)
{
	membrane_debug("%s", __func__);
	return 0;
}

int membrane_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	membrane_debug("%s", __func__);
	return 0;
}

int membrane_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		       uint32_t size)
{
	membrane_debug("%s", __func__);
	return 0;
}

int membrane_set_config(struct drm_mode_set *set)
{
	membrane_debug("%s", __func__);
	if (!set->fb || set->num_connectors == 0) {
		membrane_crtc_disable(set->crtc);
		return 0;
	}

	membrane_crtc_enable(set->crtc);

	return 0;
}

int membrane_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct drm_pending_vblank_event *event, uint32_t flags)
{
	struct membrane_device *mdev =
		container_of(crtc->dev, struct membrane_device, dev);
	struct membrane_framebuffer *mfb = to_membrane_framebuffer(fb);
	struct membrane_present *p;
	u32 id;
	int i;

	membrane_debug("%s", __func__);

	id = (u32)atomic_inc_return(&mdev->next_present_id);
	p = &mdev->presents[id % MAX_PRESENTS];

	for (i = 0; i < 4; i++) {
		if (p->files[i] != mfb->files[i]) {
			if (p->files[i])
				fput(p->files[i]);
			p->files[i] = mfb->files[i];
			if (p->files[i])
				get_file(p->files[i]);
		}
	}

	p->num_files = 0;
	for (i = 0; i < 4; i++) {
		if (p->files[i])
			p->num_files++;
	}

	p->id = id;

	membrane_queue_present(mdev, p->id, p->num_files);

	if (event) {
		unsigned long flags_irq;
		spin_lock_irqsave(&crtc->dev->event_lock, flags_irq);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags_irq);
	}

	return 0;
}

int membrane_prime_fd_to_handle(struct drm_device *dev,
				struct drm_file *file_priv, int prime_fd,
				uint32_t *handle)
{
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct file *file;
	int slot;

	membrane_debug("%s", __func__);

	file = fget(prime_fd);
	if (!file)
		return -EBADF;

	slot = (u32)atomic_inc_return(&mdev->next_present_id) % 4;

	if (mdev->imported_files[slot])
		fput(mdev->imported_files[slot]);

	mdev->imported_files[slot] = file;
	*handle = slot + 1;

	return 0;
}

int membrane_prime_handle_to_fd(struct drm_device *dev,
				struct drm_file *file_priv, uint32_t handle,
				uint32_t flags, int *prime_fd)
{
	return -ENOSYS;
}

int membrane_get_present_fd(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct membrane_get_present_fd *args = data;
	struct membrane_present *p;
	int i, count = 0;

	membrane_debug("%s", __func__);

	p = &mdev->presents[args->present_id % MAX_PRESENTS];

	if (READ_ONCE(p->id) != args->present_id)
		return -ENOENT;

	smp_rmb();

	if (!p->fds_valid) {
		for (i = 0; i < p->num_files && i < MEMBRANE_MAX_FDS; i++) {
			struct file *f = p->files[i];
			int fd;

			if (!f) {
				p->fds[i] = -1;
				continue;
			}

			fd = get_unused_fd_flags(O_CLOEXEC);
			if (fd < 0)
				return -ENOMEM;

			get_file(f);
			fd_install(fd, f);
			p->fds[i] = fd;
			count++;
		}
		p->fds_valid = true;
	} else {
		for (i = 0; i < p->num_files && i < MEMBRANE_MAX_FDS; i++) {
			if (p->fds[i] >= 0)
				count++;
		}
	}

	for (i = 0; i < MEMBRANE_MAX_FDS; i++)
		args->fds[i] = p->fds[i];

	args->num_fds = count;
	return 0;
}
