/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drmP.h>
#include <drm/drm_plane_helper.h>
#include "membrane_drv.h"

static const struct drm_mode_config_funcs membrane_mode_config_funcs = {
	.fb_create = NULL, // TODO
};

static const struct drm_plane_funcs membrane_plane_funcs = {
	.update_plane = drm_plane_helper_update,
	.disable_plane = drm_plane_helper_disable,
	.destroy = drm_plane_cleanup,
};

static const struct drm_crtc_helper_funcs membrane_crtc_helper_funcs = {
	.enable = NULL, // TODO
	.disable = NULL, // TODO
};

static const struct drm_crtc_funcs membrane_crtc_funcs = {
	.cursor_set2 = NULL, // TODO
	.cursor_move = NULL, // TODO
	.gamma_set = NULL, // TODO
	.destroy = drm_crtc_cleanup,
	.set_config = NULL, // TODO
	.page_flip = NULL, // TODO
};

static const struct drm_encoder_funcs membrane_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int membrane_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode)
		return 0;

	mode->hdisplay = 800; // TODO
	mode->hsync_start = mode->hdisplay + 60; // TODO
	mode->hsync_end = mode->hsync_start + 14; // TODO
	mode->htotal = mode->hsync_end + 170; // TODO
	mode->vdisplay = 1280; // TODO
	mode->vsync_start = mode->vdisplay + 32, // TODO
		mode->vsync_end = mode->vsync_start + 8, // TODO
		mode->vtotal = mode->vsync_end + 184, // TODO
		mode->clock = mode->htotal * mode->vtotal * 60 / 1000; // TODO

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);

	return 1;
}

static const struct drm_connector_helper_funcs membrane_connector_helper_funcs = {
	.get_modes = membrane_connector_get_modes,
};

static const struct drm_connector_funcs membrane_connector_funcs = {
	// TODO ?
};

static const uint32_t membrane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static int membrane_load(struct membrane_device *mdev)
{
	struct drm_device *dev = &mdev->dev;
	int ret;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;
	dev->mode_config.funcs = &membrane_mode_config_funcs;

	ret = drm_universal_plane_init(dev, &mdev->plane, 0,
				       &membrane_plane_funcs, membrane_formats,
				       ARRAY_SIZE(membrane_formats),
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(&mdev->crtc, &membrane_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(dev, &mdev->crtc, &mdev->plane, NULL,
					&membrane_crtc_funcs, NULL);
	if (ret)
		return ret;

	ret = drm_encoder_init(dev, &mdev->encoder, &membrane_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		return ret;

	mdev->encoder.possible_crtcs = 1 << drm_crtc_index(&mdev->crtc);

	ret = drm_connector_init(dev, &mdev->connector,
				 &membrane_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;

	drm_connector_helper_add(&mdev->connector,
				 &membrane_connector_helper_funcs);

	ret = drm_mode_connector_attach_encoder(&mdev->connector, &mdev->encoder);
	if (ret)
		return ret;

	ret = drm_mode_crtc_set_gamma_size(&mdev->crtc, 256);
	if (ret)
		return ret;

	return 0;
}

static const struct file_operations membrane_fops = {
	.owner = THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= noop_llseek,
};

static struct drm_driver membrane_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &membrane_fops,
	.name = "membrane",
	.desc = "membrane",
	.date = "20260119",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
};

static int membrane_probe(struct platform_device *pdev)
{
	struct membrane_device *mdev;
	struct drm_device *drm;
	int ret;

	mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	drm = &mdev->dev;
	platform_set_drvdata(pdev, drm);

	drm_dev_alloc(&membrane_driver, &pdev->dev);

	ret = membrane_load(mdev);
	if (ret)
		goto err_free;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_free;

	return 0;

err_free:
	drm_dev_put(drm);
	return ret;
}

static int membrane_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	drm_dev_put(drm);

	return 0;
}

static struct platform_driver membrane_platform_driver = {
    .probe = membrane_probe,
    .remove = membrane_remove,
    .driver = {
        .name = "membrane",
    },
};

module_platform_driver(membrane_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Deepak Meena <who53@disroot.org>");
MODULE_DESCRIPTION("Membrane DRM Driver");
