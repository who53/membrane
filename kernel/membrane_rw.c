/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>

#include "membrane_drv.h"

ssize_t membrane_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct drm_file *file_priv = f->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct membrane_k2u_msg ev;
	unsigned long flags;
	int ret;

	membrane_debug("%s", __func__);
	if (len < sizeof(ev))
		return -EINVAL;

	ret = wait_event_interruptible(mdev->rw_wq,
				       !kfifo_is_empty(&mdev->kfifo));
	if (ret)
		return ret;

	spin_lock_irqsave(&mdev->rw_lock, flags);
	ret = kfifo_get(&mdev->kfifo, &ev);
	spin_unlock_irqrestore(&mdev->rw_lock, flags);

	if (ret == 0)
		return 0;

	if (copy_to_user(buf, &ev, sizeof(ev)))
		return -EFAULT;

	return sizeof(ev);
}

ssize_t membrane_write(struct file *f, const char __user *buf, size_t len,
		       loff_t *off)
{
	struct drm_file *file_priv = f->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct membrane_device *mdev =
		container_of(dev, struct membrane_device, dev);
	struct membrane_u2k_cfg cfg;
	unsigned long flags;
	bool mode_changed = false;

	membrane_debug("%s", __func__);
	if (len != sizeof(cfg))
		return -EINVAL;

	if (copy_from_user(&cfg, buf, sizeof(cfg)))
		return -EFAULT;

	spin_lock_irqsave(&mdev->rw_lock, flags);

	if (mdev->w != cfg.w || mdev->h != cfg.h || mdev->r != cfg.r) {
		mdev->w = cfg.w;
		mdev->h = cfg.h;
		mdev->r = cfg.r;
		mode_changed = true;
	}

	spin_unlock_irqrestore(&mdev->rw_lock, flags);

	if (mode_changed)
		drm_kms_helper_hotplug_event(&mdev->dev);

	return sizeof(cfg);
}
