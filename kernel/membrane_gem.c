/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Deepak Meena <who53@disroot.org> */

#include "membrane_drv.h"

struct membrane_gem_object {
	struct drm_gem_object base;
	struct file *dmabuf_file;
};

static inline struct membrane_gem_object *
to_membrane_gem(struct drm_gem_object *obj)
{
	return container_of(obj, struct membrane_gem_object, base);
}

void membrane_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct membrane_gem_object *obj = to_membrane_gem(gem_obj);

	membrane_debug("gem_free_object: releasing file");

	if (obj->dmabuf_file)
		fput(obj->dmabuf_file);

	drm_gem_object_release(gem_obj);
	kfree(obj);
}

int membrane_prime_fd_to_handle(struct drm_device *dev,
				struct drm_file *file_priv, int prime_fd,
				uint32_t *handle)
{
	struct membrane_gem_object *obj;
	struct file *dmabuf_file;
	int ret;

	membrane_debug("prime_fd_to_handle: fd=%d file_priv=%p", prime_fd,
		       file_priv);

	dmabuf_file = fget(prime_fd);
	if (!dmabuf_file) {
		membrane_debug("prime_fd_to_handle: fget failed");
		return -EBADF;
	}

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		fput(dmabuf_file);
		return -ENOMEM;
	}

	ret = drm_gem_object_init(dev, &obj->base, PAGE_SIZE);
	if (ret) {
		membrane_debug(
			"prime_fd_to_handle: drm_gem_object_init failed");
		kfree(obj);
		fput(dmabuf_file);
		return ret;
	}

	obj->dmabuf_file = dmabuf_file;

	ret = drm_gem_handle_create(file_priv, &obj->base, handle);
	if (ret) {
		membrane_debug(
			"prime_fd_to_handle: drm_gem_handle_create failed");
		drm_gem_object_release(&obj->base);
		kfree(obj);
		fput(dmabuf_file);
		return ret;
	}

	drm_gem_object_put(&obj->base);

	membrane_debug(
		"prime_fd_to_handle: success, handle=%u gem_obj=%p dmabuf_file=%p",
		*handle, &obj->base, dmabuf_file);
	return 0;
}

int membrane_prime_handle_to_fd(struct drm_device *dev,
				struct drm_file *file_priv, uint32_t handle,
				uint32_t flags, int *prime_fd)
{
	return -ENOSYS;
}

struct file *membrane_gem_handle_to_file(struct drm_file *file_priv,
					 uint32_t handle)
{
	struct drm_gem_object *gem_obj;
	struct membrane_gem_object *obj;
	struct file *file;

	gem_obj = drm_gem_object_lookup(file_priv, handle);
	if (!gem_obj) {
		membrane_debug(
			"gem_handle_to_file: lookup failed for handle=%u file_priv=%p",
			handle, file_priv);
		return NULL;
	}

	obj = to_membrane_gem(gem_obj);
	file = obj->dmabuf_file;

	if (file) {
		get_file(file);
		membrane_debug(
			"gem_handle_to_file: success handle=%u gem_obj=%p file=%p",
			handle, gem_obj, file);
	} else {
		membrane_debug(
			"gem_handle_to_file: NULL dmabuf_file for handle=%u gem_obj=%p",
			handle, gem_obj);
	}

	drm_gem_object_put(gem_obj);
	return file;
}
