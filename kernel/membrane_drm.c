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
	struct drm_file *file;
	struct membrane_pending_event *p;
	unsigned long flags_irq;
	int ret;

	spin_lock_irqsave(&dev->event_lock, flags_irq);

	list_for_each_entry(file, &dev->filelist, lhead) {
		if (!file->event_space)
			continue;

		p = kzalloc(sizeof(*p), GFP_ATOMIC);
		if (!p)
			continue;

		p->event.base.type = DRM_MEMBRANE_EVENT;
		p->event.base.length = sizeof(p->event);
		p->event.flags = flags;
		p->event.present_id = present_id;
		p->event.num_fds = num_fds;

		p->base.event = &p->event.base;
		p->base.file_priv = file;

		ret = drm_event_reserve_init_locked(dev, file, &p->base,
						    &p->event.base);
		if (ret) {
			kfree(p);
			continue;
		}

		drm_send_event_locked(dev, &p->base);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags_irq);
}

int membrane_config(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct membrane_u2k_cfg *cfg = data;
	unsigned long flags;
	bool mode_changed = false;

	spin_lock_irqsave(&mdev->lock, flags);

	if (mdev->w != cfg->w || mdev->h != cfg->h || mdev->r != cfg->r) {
		mdev->w = cfg->w;
		mdev->h = cfg->h;
		mdev->r = cfg->r;
		mode_changed = true;
	}

	spin_unlock_irqrestore(&mdev->lock, flags);

	if (mode_changed)
		drm_kms_helper_hotplug_event(&mdev->dev);

	return 0;
}

static void membrane_fb_destroy(struct drm_framebuffer *fb)
{
	struct membrane_framebuffer *mfb = to_membrane_framebuffer(fb);
	struct membrane_device *mdev =
		container_of(fb->dev, struct membrane_device, dev);
	int i;
	unsigned long flags;

	spin_lock_irqsave(&mdev->idr_lock, flags);
	for (i = 0; i < 4; i++) {
		if (mfb->files[i]) {
			idr_remove(&mdev->handle_idr, mfb->handles[i]);
			fput(mfb->files[i]);
			mfb->files[i] = NULL;
		}
	}
	spin_unlock_irqrestore(&mdev->idr_lock, flags);

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
	unsigned long flags;

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

	spin_lock_irqsave(&mdev->idr_lock, flags);
	for (i = 0; i < 4; i++) {
		if (mode_cmd->handles[i] != 0) {
			struct file *file = idr_find(&mdev->handle_idr,
						     mode_cmd->handles[i]);
			if (!file) {
				membrane_debug("Handle %u not found",
					       mode_cmd->handles[i]);
				ret = -ENOENT;
				goto err_cleanup;
			}
			mfb->files[i] = file;
			mfb->handles[i] = mode_cmd->handles[i];
		}
	}
	spin_unlock_irqrestore(&mdev->idr_lock, flags);

	return &mfb->base;

err_cleanup:
	while (i--) {
		if (mfb->files[i]) {
			idr_remove(&mdev->handle_idr, mfb->handles[i]);
			fput(mfb->files[i]);
			mfb->files[i] = NULL;
		}
	}
	spin_unlock_irqrestore(&mdev->idr_lock, flags);

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
	unsigned long irq_flags;
	int i;

	membrane_debug("%s", __func__);

	p = kzalloc(sizeof(*p), GFP_ATOMIC);
	if (!p)
		return -ENOMEM;

	spin_lock_irqsave(&mdev->present_lock, irq_flags);
	p->id = mdev->next_present_id++;
	spin_unlock_irqrestore(&mdev->present_lock, irq_flags);

	p->owner = event ? event->base.file_priv : NULL;

	spin_lock(&mdev->idr_lock);
	for (i = 0; i < 4; i++) {
		if (mfb->files[i]) {
			get_file(mfb->files[i]);
			p->files[p->num_files++] = mfb->files[i];
		}
	}
	spin_unlock(&mdev->idr_lock);

	spin_lock_irqsave(&mdev->present_lock, irq_flags);
	list_add_tail(&p->link, &mdev->presents);
	spin_unlock_irqrestore(&mdev->present_lock, irq_flags);

	membrane_send_event(mdev, MEMBRANE_PRESENT_UPDATED, p->id,
			    p->num_files);

	if (event) {
		unsigned long flags;
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
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
	int ret;
	unsigned long flags;

	membrane_debug("%s", __func__);

	file = fget(prime_fd);
	if (!file)
		return -EBADF;

	spin_lock_irqsave(&mdev->idr_lock, flags);
	ret = idr_alloc(&mdev->handle_idr, file, 1, 0, GFP_ATOMIC);
	spin_unlock_irqrestore(&mdev->idr_lock, flags);

	if (ret < 0) {
		fput(file);
		return ret;
	}

	*handle = ret;
	return 0;
}

int membrane_prime_handle_to_fd(struct drm_device *dev,
				struct drm_file *file_priv, uint32_t handle,
				uint32_t flags, int *prime_fd)
{
	(void)*dev, (void)*file_priv, (void)handle, (void)flags,
		(void)*prime_fd;
	pr_err("membrane: %s shouldnt get called", __func__);
	return 0;
}

int membrane_get_present_fd(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct membrane_get_present_fd *args = data;
	struct membrane_present *p;
	unsigned long flags;
	struct file *f;
	int fd;
	int ret = -ENOENT;

	membrane_debug("%s", __func__);

	spin_lock_irqsave(&mdev->present_lock, flags);
	list_for_each_entry(p, &mdev->presents, link) {
		if (p->id == args->present_id) {
			if (args->index >= p->num_files) {
				ret = -EINVAL;
				goto out;
			}

			f = p->files[args->index];
			get_file(f);

			fd = get_unused_fd_flags(O_CLOEXEC);
			if (fd < 0) {
				fput(f);
				ret = fd;
				goto out;
			}

			fd_install(fd, f);
			args->fd = fd;

			p->sent++;
			if (p->sent == p->num_files) {
				int i;
				list_del(&p->link);
				for (i = 0; i < p->num_files; i++)
					fput(p->files[i]);
				kfree(p);
			}
			ret = 0;
			goto out;
		}
	}

out:
	spin_unlock_irqrestore(&mdev->present_lock, flags);
	return ret;
}
