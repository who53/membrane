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

	membrane_debug("entry");

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		membrane_debug("drm_mode_create failed");
		return 0;
	}

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
	drm_mode_probed_add(connector, mode);

	membrane_debug("exit");
	return 1;
}

static const struct drm_connector_helper_funcs membrane_connector_helper_funcs = {
	.get_modes = membrane_connector_get_modes,
};

static enum drm_connector_status
membrane_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs membrane_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = membrane_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
};

static const uint32_t membrane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static int membrane_load(struct membrane_device *mdev)
{
	struct drm_device *dev = &mdev->dev;
	int ret;

	membrane_debug("entry");

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
	if (ret) {
		membrane_debug("drm_universal_plane_init failed: %d", ret);
		return ret;
	}

	drm_crtc_helper_add(&mdev->crtc, &membrane_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(dev, &mdev->crtc, &mdev->plane, NULL,
					&membrane_crtc_funcs, NULL);
	if (ret) {
		membrane_debug("drm_crtc_init_with_planes failed: %d", ret);
		return ret;
	}

	ret = drm_encoder_init(dev, &mdev->encoder, &membrane_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret) {
		membrane_debug("drm_encoder_init failed: %d", ret);
		return ret;
	}

	mdev->encoder.possible_crtcs = 1 << drm_crtc_index(&mdev->crtc);

	ret = drm_connector_init(dev, &mdev->connector,
				 &membrane_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		membrane_debug("drm_connector_init failed: %d", ret);
		return ret;
	}

	drm_connector_helper_add(&mdev->connector,
				 &membrane_connector_helper_funcs);

	ret = drm_mode_connector_attach_encoder(&mdev->connector,
						&mdev->encoder);
	if (ret) {
		membrane_debug("drm_mode_connector_attach_encoder failed: %d",
			       ret);
		return ret;
	}

	ret = drm_mode_crtc_set_gamma_size(&mdev->crtc, 256);
	if (ret) {
		membrane_debug("drm_mode_crtc_set_gamma_size failed: %d", ret);
		return ret;
	}

	membrane_debug("exit success");
	return 0;
}

static const struct file_operations membrane_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
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

	membrane_debug("entry");

	mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		membrane_debug("devm_kzalloc failed");
		return -ENOMEM;
	}

	drm = &mdev->dev;

	membrane_debug("calling drm_dev_init");
	ret = drm_dev_init(drm, &membrane_driver, &pdev->dev);
	if (ret) {
		membrane_debug("drm_dev_init failed: %d", ret);
		return ret;
	}
	membrane_debug("called drm_dev_init");

	platform_set_drvdata(pdev, drm);

	ret = membrane_load(mdev);
	if (ret) {
		membrane_debug("membrane_load failed: %d", ret);
		goto err_free;
	}

	ret = drm_dev_register(drm, 0);
	if (ret) {
		membrane_debug("drm_dev_register failed: %d", ret);
		goto err_free;
	}

	membrane_debug("probe success");
	return 0;

err_free:
	drm_dev_put(drm);
	membrane_debug("probe failed");
	return ret;
}

static int membrane_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	membrane_debug("entry");

	drm_dev_unregister(drm);
	drm_dev_put(drm);

	membrane_debug("exit");
	return 0;
}

static struct platform_driver membrane_platform_driver = {
    .probe = membrane_probe,
    .remove = membrane_remove,
    .driver = {
        .name = "membrane",
    },
};

static struct platform_device *membrane_pdev;

static int __init membrane_init(void)
{
	int ret;

	membrane_debug("entry");

	ret = platform_driver_register(&membrane_platform_driver);
	if (ret) {
		membrane_debug("platform_driver_register failed");
		return ret;
	}

	membrane_pdev =
		platform_device_register_simple("membrane", -1, NULL, 0);
	if (IS_ERR(membrane_pdev)) {
		membrane_debug("platform_device_register_simple failed");
		platform_driver_unregister(&membrane_platform_driver);
		return PTR_ERR(membrane_pdev);
	}

	return 0;
}

static void __exit membrane_exit(void)
{
	membrane_debug("exit");
	platform_device_unregister(membrane_pdev);
	platform_driver_unregister(&membrane_platform_driver);
}

module_init(membrane_init);
module_exit(membrane_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Deepak Meena <who53@disroot.org>");
MODULE_DESCRIPTION("Membrane DRM Driver");
