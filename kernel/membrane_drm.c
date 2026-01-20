/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include "membrane_drv.h"

static void membrane_fb_destroy(struct drm_framebuffer *fb)
{
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
	struct drm_framebuffer *fb;
	int ret;
	membrane_debug("%s", __func__);
	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb) {
		return ERR_PTR(-ENOMEM);
	}

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	ret = drm_framebuffer_init(dev, fb, &membrane_fb_funcs);
	if (ret) {
		membrane_debug("failed to initialize framebuffer");
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

void membrane_crtc_enable(struct drm_crtc *crtc)
{
	struct membrane_device *mdev =
		container_of(crtc->dev, struct membrane_device, dev);
	struct membrane_k2u_msg ev = {
		.flags = MEMBRANE_DPMS_UPDATED,
		.dpms = 0,
	};
	unsigned long flags;

	membrane_debug("%s", __func__);

	spin_lock_irqsave(&mdev->rw_lock, flags);
	if (!kfifo_is_full(&mdev->kfifo))
		kfifo_put(&mdev->kfifo, ev);
	spin_unlock_irqrestore(&mdev->rw_lock, flags);

	wake_up_interruptible(&mdev->rw_wq);
}

void membrane_crtc_disable(struct drm_crtc *crtc)
{
	struct membrane_device *mdev =
		container_of(crtc->dev, struct membrane_device, dev);
	struct membrane_k2u_msg ev = {
		.flags = MEMBRANE_DPMS_UPDATED,
		.dpms = 1,
	};
	unsigned long flags;

	membrane_debug("%s", __func__);

	spin_lock_irqsave(&mdev->rw_lock, flags);
	if (!kfifo_is_full(&mdev->kfifo))
		kfifo_put(&mdev->kfifo, ev);
	spin_unlock_irqrestore(&mdev->rw_lock, flags);

	wake_up_interruptible(&mdev->rw_wq);
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
	struct membrane_k2u_msg ev = {
		.flags = MEMBRANE_PRESENT_UPDATED,
		.present = 1,
	};
	unsigned long irq_flags;

	membrane_debug("%s", __func__);

	if (event) {
		unsigned long flags;
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	}

	spin_lock_irqsave(&mdev->rw_lock, irq_flags);
	if (!kfifo_is_full(&mdev->kfifo))
		kfifo_put(&mdev->kfifo, ev);
	spin_unlock_irqrestore(&mdev->rw_lock, irq_flags);

	wake_up_interruptible(&mdev->rw_wq);

	return 0;
}

int membrane_prime_fd_to_handle(struct drm_device *dev,
				struct drm_file *file_priv, int prime_fd,
				uint32_t *handle)
{
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct file *file;
	unsigned long flags;

	membrane_debug("%s", __func__);

	file = fget(prime_fd);
	if (!file)
		return -EBADF;

	spin_lock_irqsave(&mdev->rw_lock, flags);

	if (!kfifo_put(&mdev->fd_fifo, file)) {
		spin_unlock_irqrestore(&mdev->rw_lock, flags);
		fput(file);
		return -ENOSPC;
	}

	spin_unlock_irqrestore(&mdev->rw_lock, flags);

	return 0;
}

int membrane_pop_fd(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct membrane_pop_fd *args = data;
	unsigned long flags;
	struct file *f;
	int fd;

	membrane_debug("%s", __func__);

	spin_lock_irqsave(&mdev->rw_lock, flags);
	if (!kfifo_get(&mdev->fd_fifo, &f)) {
		spin_unlock_irqrestore(&mdev->rw_lock, flags);
		return -EAGAIN;
	}
	spin_unlock_irqrestore(&mdev->rw_lock, flags);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		fput(f);
		return fd;
	}

	fd_install(fd, f);
	args->fd = fd;
	return 0;
}
