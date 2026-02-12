/*
 * Copyright (c) 2026 Deepak Meena <who53@disroot.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <nativewindowbase.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ws.h>

#include "linux-dmabuf-v1-client-protocol.h"
#include <EGL/eglext.h>
#include <drm_fourcc.h>

#include <wayland-egl-backend.h>

#include <hardware/gralloc.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <algorithm>
#include <utility>
#include <vector>
#include <windowbuffer.h>

extern "C" {
#include <eglplatformcommon.h>
void hybris_gralloc_initialize(int framebuffer);
int hybris_gralloc_allocate(
    int width, int height, int format, int usage, buffer_handle_t* handle, uint32_t* stride);
int hybris_gralloc_release(buffer_handle_t handle, int was_allocated);
int hybris_gralloc_import_buffer(buffer_handle_t raw_handle, buffer_handle_t* out_handle);
};

#include <log.h>

static PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR_func = NULL;
static PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR_func = NULL;
static PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR_func = NULL;

static void init_egl_sync_funcs() {
    if (eglCreateSyncKHR_func)
        return;
    eglCreateSyncKHR_func = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    eglDestroySyncKHR_func = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    eglClientWaitSyncKHR_func
        = (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
}

class MembraneNativeWindowBuffer : public BaseNativeWindowBuffer {
public:
    MembraneNativeWindowBuffer() {
        busy = 0;
        m_wl_buffer = NULL;
        m_num_fds = 0;
        m_meta_fd = -1;
    }

    bool allocate(unsigned int w, unsigned int h, unsigned int fmt, uint64_t usg) {
        busy = 0;
        ANativeWindowBuffer::width = w;
        ANativeWindowBuffer::height = h;
        ANativeWindowBuffer::format = fmt;
        ANativeWindowBuffer::usage = usg;
        ANativeWindowBuffer::handle = NULL;

        int ret = hybris_gralloc_allocate(w, h, fmt, (uint32_t)usg, &handle, (uint32_t*)&stride);
        if (ret != 0) {
            membrane_err("Failed to allocate buffer: %d", ret);
            return false;
        }

        native_handle_t* nh = (native_handle_t*)handle;
        m_num_fds = nh->numFds;
        for (int i = 0; i < m_num_fds && i < 4; i++) {
            m_cached_fds[i] = dup(nh->data[i]);
        }

        int meta_size = nh->numInts * sizeof(int);
        if (meta_size > 0) {
            m_meta_fd = memfd_create("membrane_meta", MFD_CLOEXEC);
            if (m_meta_fd >= 0) {
                if (ftruncate(m_meta_fd, meta_size) == -1) {
                    membrane_err("Failed to ftruncate meta buffer: %s", strerror(errno));
                    close(m_meta_fd);
                    m_meta_fd = -1;
                    return false;
                }
                if (write(m_meta_fd, &nh->data[nh->numFds], meta_size) != meta_size) {
                    membrane_err("Failed to write meta buffer: %s", strerror(errno));
                    close(m_meta_fd);
                    m_meta_fd = -1;
                    return false;
                }
            }
        }

        return true;
    }

    void release() {
        if (m_wl_buffer) {
            wl_buffer_destroy(m_wl_buffer);
            m_wl_buffer = NULL;
        }
        for (int i = 0; i < m_num_fds; i++) {
            if (m_cached_fds[i] >= 0) {
                close(m_cached_fds[i]);
                m_cached_fds[i] = -1;
            }
        }
        if (m_meta_fd >= 0) {
            close(m_meta_fd);
            m_meta_fd = -1;
        }
        if (ANativeWindowBuffer::handle) {
            hybris_gralloc_release(ANativeWindowBuffer::handle, 1);
            ANativeWindowBuffer::handle = NULL;
        }
        m_num_fds = 0;
        busy = 0;
    }

    virtual ~MembraneNativeWindowBuffer() { release(); }

    buffer_handle_t getHandle() { return ANativeWindowBuffer::handle; }
    void setWlBuffer(struct wl_buffer* buf) { m_wl_buffer = buf; }
    struct wl_buffer* getWlBuffer() { return m_wl_buffer; }
    void setBusy(int b) { busy = b; }
    int getBusy() { return busy; }

    int busy;
    struct wl_buffer* m_wl_buffer;
    int m_cached_fds[4];
    int m_num_fds;
    int m_meta_fd;
};

class MembraneNativeWindow : public BaseNativeWindow {
public:
    MembraneNativeWindow(struct wl_egl_window* wl_window, struct wl_display* wl_dpy,
        struct zwp_linux_dmabuf_v1* dmabuf)
        : BaseNativeWindow()
        , m_wl_window(wl_window)
        , m_wl_display(wl_dpy)
        , m_dmabuf(dmabuf)
        , m_bufferCount(3)
        , m_allocateBuffers(true)
        , m_damage_rects(NULL)
        , m_damage_n_rects(0)
        , m_frame_callback(NULL)
        , m_queuedBuffer(NULL)
        , m_attached_height(0)
        , m_swap_interval(1) {
        m_wl_surface = m_wl_window->surface;

        m_wl_window->driver_private = this;
        m_wl_window->resize_callback = resize_callback_static;

        m_format = HAL_PIXEL_FORMAT_RGBA_8888;
        m_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;

        reallocateBuffers();
    }

    virtual ~MembraneNativeWindow() {
        if (m_frame_callback)
            wl_callback_destroy(m_frame_callback);

        destroyBuffers();
        if (m_wl_window) {
            m_wl_window->driver_private = NULL;
            m_wl_window->resize_callback = NULL;
        }
    }

    virtual int setSwapInterval(int interval) override {
        m_swap_interval = interval;
        return 0;
    }
    virtual unsigned int type() const override { return NATIVE_WINDOW_SURFACE; }

    virtual int dequeueBuffer(BaseNativeWindowBuffer** buffer, int* fenceFd) override {
        if (m_allocateBuffers)
            reallocateBuffers();

        MembraneNativeWindowBuffer* mnb = NULL;

        for (;;) {
            for (int i = 0; i < m_bufferCount; i++) {
                if (m_buffers[i].getBusy() == 0) {
                    mnb = &m_buffers[i];
                    break;
                }
            }
            if (mnb)
                break;

            wl_display_flush(m_wl_display);
            if (wl_display_dispatch(m_wl_display) == -1) {
                return -1;
            }
        }

        mnb->setBusy(1);

        *buffer = mnb;
        *fenceFd = -1;

        return 0;
    }

    virtual int queueBuffer(BaseNativeWindowBuffer* buffer, int fenceFd) override {
        MembraneNativeWindowBuffer* mnb = (MembraneNativeWindowBuffer*)buffer;

        if (fenceFd >= 0) {
            if (eglCreateSyncKHR_func && eglClientWaitSyncKHR_func && eglDestroySyncKHR_func) {
                EGLDisplay dpy = eglGetCurrentDisplay();
                if (dpy != EGL_NO_DISPLAY) {
                    EGLSyncKHR sync = eglCreateSyncKHR_func(dpy, EGL_SYNC_FENCE_KHR, NULL);
                    if (sync != EGL_NO_SYNC_KHR) {
                        eglClientWaitSyncKHR_func(
                            dpy, sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);
                        eglDestroySyncKHR_func(dpy, sync);
                    }
                }
            }
            close(fenceFd);
        }

        if (!mnb->getWlBuffer()) {
            createWlBuffer(mnb);
        }

        if (mnb->getWlBuffer()) {
            membrane_assert(mnb->getBusy() == 1);
            mnb->setBusy(2);
            m_queuedBuffer = mnb;
        } else {
            membrane_err("Failed to create wl_buffer for queue");
            mnb->setBusy(0);
            return -1;
        }

        return 0;
    }

    virtual int cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd) override {
        MembraneNativeWindowBuffer* mnb = (MembraneNativeWindowBuffer*)buffer;
        if (fenceFd >= 0)
            close(fenceFd);

        mnb->setBusy(0);
        return 0;
    }

    virtual int lockBuffer(BaseNativeWindowBuffer* buffer) override {
        (void)buffer;
        return 0;
    }

    virtual unsigned int width() const override { return m_wl_window->width; }
    virtual unsigned int height() const override { return m_wl_window->height; }
    virtual unsigned int format() const override { return m_format; }
    virtual unsigned int defaultWidth() const override { return m_wl_window->width; }
    virtual unsigned int defaultHeight() const override { return m_wl_window->height; }
    virtual unsigned int queueLength() const override {
        int queued = 0;
        for (int i = 0; i < m_bufferCount; i++) {
            if (m_buffers[i].busy == 2)
                queued++;
        }
        return queued;
    }
    virtual unsigned int transformHint() const override { return 0; }
    virtual unsigned int getUsage() const override { return m_usage; }

    virtual int setBuffersFormat(int format) override {
        m_allocateBuffers |= (format != m_format);
        m_format = format;
        return NO_ERROR;
    }

    virtual int setBuffersDimensions(int width, int height) override {
        (void)width;
        (void)height;
        return NO_ERROR;
    }

    virtual int setUsage(uint64_t usage) override {
        uint64_t new_usage = usage | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
        if (new_usage != m_usage) {
            m_usage = new_usage;
            m_allocateBuffers = true;
        }
        return NO_ERROR;
    }

    virtual int setBufferCount(int cnt) override {
        if (cnt > 4)
            cnt = 4;
        if (m_bufferCount != cnt) {
            m_bufferCount = cnt;
            m_allocateBuffers = true;
        }
        return NO_ERROR;
    }

    void handleRelease(struct wl_buffer* wl_buf) {
        for (int i = 0; i < m_bufferCount; i++) {
            if (m_buffers[i].getWlBuffer() == wl_buf) {
                m_buffers[i].setBusy(0);
                if (m_queuedBuffer == &m_buffers[i])
                    m_queuedBuffer = NULL;
                break;
            }
        }
    }

    void finishSwap() {
        if (m_frame_callback && m_swap_interval > 0) {
            while (m_frame_callback) {
                if (wl_display_dispatch(m_wl_display) == -1) {
                    break;
                }
            }
        }

        if (m_swap_interval > 0) {
            m_frame_callback = wl_surface_frame(m_wl_surface);
            wl_callback_add_listener(m_frame_callback, &s_frame_listener, this);
        }

        MembraneNativeWindowBuffer* mnb = m_queuedBuffer;
        m_queuedBuffer = NULL;

        if (mnb) {
            wl_surface_attach(m_wl_surface, mnb->getWlBuffer(), 0, 0);
            m_attached_height = m_wl_window->height;
        }

        uint32_t surface_version = wl_proxy_get_version((struct wl_proxy*)m_wl_surface);

        if (m_damage_n_rects > 0 && m_damage_rects && m_attached_height > 0) {
            for (int i = 0; i < m_damage_n_rects; i++) {
                const int* rect = &m_damage_rects[i * 4];
                if (surface_version >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
                    wl_surface_damage_buffer(m_wl_surface, rect[0],
                        m_attached_height - rect[1] - rect[3], rect[2], rect[3]);
                } else {
                    wl_surface_damage(m_wl_surface, rect[0], m_attached_height - rect[1] - rect[3],
                        rect[2], rect[3]);
                }
            }
        } else if (mnb) {
            if (surface_version >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
                wl_surface_damage_buffer(m_wl_surface, 0, 0, INT32_MAX, INT32_MAX);
            } else {
                wl_surface_damage(m_wl_surface, 0, 0, INT32_MAX, INT32_MAX);
            }
        }

        m_damage_rects = NULL;
        m_damage_n_rects = 0;

        wl_surface_commit(m_wl_surface);
        wl_display_flush(m_wl_display);
    }

    void prepareSwap(EGLint* damage_rects, EGLint damage_n_rects) {
        m_damage_rects = damage_rects;
        m_damage_n_rects = damage_n_rects;
    }

private:
    struct wl_egl_window* m_wl_window;
    struct wl_display* m_wl_display;
    struct zwp_linux_dmabuf_v1* m_dmabuf;
    struct wl_surface* m_wl_surface;

    MembraneNativeWindowBuffer m_buffers[4];
    int m_bufferCount;
    bool m_allocateBuffers;
    uint64_t m_usage;
    int m_format;
    EGLint* m_damage_rects;
    EGLint m_damage_n_rects;

    struct wl_callback* m_frame_callback;
    MembraneNativeWindowBuffer* m_queuedBuffer;
    int m_attached_height;
    int m_swap_interval;

    void destroyBuffers() {
        m_queuedBuffer = NULL;
        for (int i = 0; i < 4; i++) {
            m_buffers[i].release();
        }
    }

    void reallocateBuffers() {
        int w = m_wl_window->width;
        int h = m_wl_window->height;

        if (m_bufferCount > 0 && m_buffers[0].getHandle() != NULL) {
            if (m_buffers[0].width == w && m_buffers[0].height == h
                && m_buffers[0].format == m_format && m_buffers[0].usage == m_usage) {
                m_allocateBuffers = false;
                return;
            }
        }

        destroyBuffers();

        for (int i = 0; i < m_bufferCount; i++) {
            m_buffers[i].allocate(w, h, m_format, m_usage);
            m_buffers[i].common.incRef(&m_buffers[i].common);
            createWlBuffer(&m_buffers[i]);
        }
        m_allocateBuffers = false;
    }

    void createWlBuffer(MembraneNativeWindowBuffer* mnb) {
        struct zwp_linux_buffer_params_v1* params;
        params = zwp_linux_dmabuf_v1_create_params(m_dmabuf);
        if (!params)
            return;

        for (int i = 0; i < mnb->m_num_fds; i++) {
            int fd = dup(mnb->m_cached_fds[i]);
            zwp_linux_buffer_params_v1_add(params, fd, i, 0, mnb->stride * 4, 0, 0);
            close(fd);
        }

        if (mnb->m_meta_fd >= 0) {
            lseek(mnb->m_meta_fd, 0, SEEK_SET);
            int meta_fd = dup(mnb->m_meta_fd);
            zwp_linux_buffer_params_v1_add(params, meta_fd, mnb->m_num_fds, 0, 1, 0, 0);
            close(meta_fd);
        }

        struct wl_buffer* wl_buf = zwp_linux_buffer_params_v1_create_immed(
            params, mnb->width, mnb->height, DRM_FORMAT_ARGB8888, 0);
        zwp_linux_buffer_params_v1_destroy(params);

        if (wl_buf) {
            wl_buffer_add_listener(wl_buf, &s_buffer_listener, this);
            mnb->setWlBuffer(wl_buf);
        } else {
            membrane_err("Failed to create wl_buffer from params");
        }
    }

    static void resize_callback_static(struct wl_egl_window* wl_win, void* data) {
        (void)wl_win;
        MembraneNativeWindow* win = static_cast<MembraneNativeWindow*>(data);
        win->m_allocateBuffers = true;
    }

    static void buffer_release_static(void* data, struct wl_buffer* wl_buffer) {
        MembraneNativeWindow* win = static_cast<MembraneNativeWindow*>(data);
        win->handleRelease(wl_buffer);
    }

    static void frame_callback_static(void* data, struct wl_callback* callback, uint32_t time) {
        (void)time;
        MembraneNativeWindow* win = static_cast<MembraneNativeWindow*>(data);
        if (win->m_frame_callback == callback) {
            win->m_frame_callback = NULL;
        }
        wl_callback_destroy(callback);
    }

    static const struct wl_buffer_listener s_buffer_listener;
    static const struct wl_callback_listener s_frame_listener;
};

const struct wl_buffer_listener MembraneNativeWindow::s_buffer_listener
    = { .release = MembraneNativeWindow::buffer_release_static };

const struct wl_callback_listener MembraneNativeWindow::s_frame_listener
    = { .done = MembraneNativeWindow::frame_callback_static };

struct MembraneDisplay : public _EGLDisplay {
    struct wl_display* wl_dpy;
    struct zwp_linux_dmabuf_v1* dmabuf;
};

static void registry_handle_global(void* data, struct wl_registry* registry, uint32_t id,
    const char* interface, uint32_t version) {
    MembraneDisplay* dpy = (MembraneDisplay*)data;
    if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0 && version >= 3) {
        dpy->dmabuf = (zwp_linux_dmabuf_v1*)wl_registry_bind(
            registry, id, &zwp_linux_dmabuf_v1_interface, 3);
    }
}

static void registry_handle_global_remove(void* data, struct wl_registry* registry, uint32_t id) {
    (void)data;
    (void)registry;
    (void)id;
}

static const struct wl_registry_listener registry_listener
    = { registry_handle_global, registry_handle_global_remove };

extern "C" void membranews_init_module(struct ws_egl_interface* egl_iface) {
    hybris_gralloc_initialize(1);
    eglplatformcommon_init(egl_iface);
    init_egl_sync_funcs();
}

extern "C" _EGLDisplay* membranews_GetDisplay(EGLNativeDisplayType display) {
    struct wl_display* wl_dpy = (struct wl_display*)display;
    if (!wl_dpy)
        return NULL;

    MembraneDisplay* dpy = new MembraneDisplay();
    memset(dpy, 0, sizeof(MembraneDisplay));
    dpy->wl_dpy = wl_dpy;

    return dpy;
}

extern "C" void membranews_Terminate(struct _EGLDisplay* display) {
    MembraneDisplay* dpy = (MembraneDisplay*)display;
    if (dpy) {
        delete dpy;
    }
}

extern "C" EGLNativeWindowType membranews_CreateWindow(
    EGLNativeWindowType win, struct _EGLDisplay* display) {
    MembraneDisplay* dpy = (MembraneDisplay*)display;
    struct wl_egl_window* wl_win = (struct wl_egl_window*)win;

    if (!wl_win)
        return 0;

    if (!dpy->dmabuf) {
        struct wl_registry* registry = wl_display_get_registry(dpy->wl_dpy);
        struct wl_event_queue* queue = wl_display_create_queue(dpy->wl_dpy);

        wl_proxy_set_queue((struct wl_proxy*)registry, queue);

        wl_registry_add_listener(registry, &registry_listener, dpy);

        wl_display_roundtrip_queue(dpy->wl_dpy, queue);

        if (dpy->dmabuf) {
            wl_proxy_set_queue((struct wl_proxy*)dpy->dmabuf, NULL);
        } else {
            membrane_err("zwp_linux_dmabuf_v1 not supported");
            wl_registry_destroy(registry);
            wl_event_queue_destroy(queue);
            return 0;
        }

        wl_registry_destroy(registry);
        wl_event_queue_destroy(queue);
    }

    MembraneNativeWindow* w = new MembraneNativeWindow(wl_win, dpy->wl_dpy, dpy->dmabuf);
    w->common.incRef(&w->common);
    return (EGLNativeWindowType) static_cast<ANativeWindow*>(w);
}

extern "C" void membranews_DestroyWindow(EGLNativeWindowType win) {
    MembraneNativeWindow* w = static_cast<MembraneNativeWindow*>((struct ANativeWindow*)win);
    w->common.decRef(&w->common);
}

extern "C" void membranews_releaseDisplay(struct _EGLDisplay* dpy) { (void)dpy; }

extern "C" __eglMustCastToProperFunctionPointerType membranews_eglGetProcAddress(
    const char* procname) {
    return eglplatformcommon_eglGetProcAddress(procname);
}

extern "C" void membranews_passthroughImageKHR(
    EGLContext* ctx, EGLenum* target, EGLClientBuffer* buffer, const EGLint** attrib_list) {
    if (*attrib_list) {
        int width = 0, height = 0, stride = 0;
        int usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_COMPOSER;
        std::vector<std::pair<int, int>> plane_fds;

        for (const EGLint* attr = *attrib_list; attr && attr[0] != EGL_NONE; attr += 2) {
            switch (attr[0]) {
            case EGL_WIDTH:
                width = attr[1];
                break;
            case EGL_HEIGHT:
                height = attr[1];
                break;
            case EGL_DMA_BUF_PLANE0_PITCH_EXT:
                stride = attr[1];
                break;
            case EGL_DMA_BUF_PLANE0_FD_EXT:
                plane_fds.push_back({ 0, attr[1] });
                break;
            case EGL_DMA_BUF_PLANE1_FD_EXT:
                plane_fds.push_back({ 1, attr[1] });
                break;
            case EGL_DMA_BUF_PLANE2_FD_EXT:
                plane_fds.push_back({ 2, attr[1] });
                break;
            case EGL_DMA_BUF_PLANE3_FD_EXT:
                plane_fds.push_back({ 3, attr[1] });
                break;
            }
        }

        std::sort(plane_fds.begin(), plane_fds.end());

        if (!plane_fds.empty()) {
            int meta_fd = plane_fds.back().second;
            plane_fds.pop_back();

            struct stat sb;
            if (fstat(meta_fd, &sb) == 0 && sb.st_size > 0) {
                int num_ints = sb.st_size / sizeof(int);
                int num_fds = plane_fds.size();
                std::vector<int> ints(num_ints);

                lseek(meta_fd, 0, SEEK_SET);
                if (read(meta_fd, ints.data(), num_ints * sizeof(int))
                    == (ssize_t)(num_ints * sizeof(int))) {
                    native_handle_t* nh = native_handle_create(num_fds, num_ints);
                    for (int i = 0; i < num_fds; i++)
                        nh->data[i] = plane_fds[i].second;
                    for (int i = 0; i < num_ints; i++)
                        nh->data[num_fds + i] = ints[i];

                    buffer_handle_t handle = nullptr;
                    hybris_gralloc_import_buffer(nh, &handle);
                    native_handle_delete(nh);

                    if (handle) {
                        RemoteWindowBuffer* anwb = new RemoteWindowBuffer(
                            width, height, stride / 4, HAL_PIXEL_FORMAT_RGBA_8888, usage, handle);
                        anwb->common.incRef(&anwb->common);
                        anwb->setAllocated(true);

                        *buffer = (EGLClientBuffer) static_cast<ANativeWindowBuffer*>(anwb);
                        *target = EGL_NATIVE_BUFFER_ANDROID;
                        *ctx = EGL_NO_CONTEXT;
                        *attrib_list = nullptr;
                    }
                } else {
                    membrane_err("Failed to read meta buffer: %s", strerror(errno));
                }
            }
        }
    }
}

extern "C" const char* membranews_eglQueryString(
    EGLDisplay dpy, EGLint name, const char* (*real_eglQueryString)(EGLDisplay dpy, EGLint name)) {
    const char* ret = eglplatformcommon_eglQueryString(dpy, name, real_eglQueryString);
    if (ret && name == EGL_EXTENSIONS) {
        static char eglextensionsbuf[2048];
        snprintf(eglextensionsbuf, 2046, "%s %s", ret,
            "EGL_EXT_swap_buffers_with_damage EGL_EXT_image_dma_buf_import "
            "EGL_EXT_image_dma_buf_import_modifiers");
        ret = eglextensionsbuf;
    }
    return ret;
}

extern "C" void membranews_prepareSwap(
    EGLDisplay dpy, EGLNativeWindowType win, EGLint* damage_rects, EGLint damage_n_rects) {
    (void)dpy;
    MembraneNativeWindow* window = static_cast<MembraneNativeWindow*>((struct ANativeWindow*)win);
    window->prepareSwap(damage_rects, damage_n_rects);
}

extern "C" void membranews_finishSwap(EGLDisplay dpy, EGLNativeWindowType win) {
    (void)dpy;
    MembraneNativeWindow* window = static_cast<MembraneNativeWindow*>((struct ANativeWindow*)win);
    window->finishSwap();
}

extern "C" void membranews_setSwapInterval(
    EGLDisplay dpy, EGLNativeWindowType win, EGLint interval) {
    (void)dpy;
    MembraneNativeWindow* window = static_cast<MembraneNativeWindow*>((struct ANativeWindow*)win);
    window->setSwapInterval(interval);
}

extern "C" void membranews_eglInitialized(struct _EGLDisplay* dpy) { (void)dpy; }

extern "C" EGLBoolean membranews_eglQueryDmaBufModifiersEXT(EGLDisplay dpy, EGLint format,
    EGLint max_modifiers, EGLuint64KHR* modifiers, EGLBoolean* external_only,
    EGLint* num_modifiers) {
    (void)dpy;

    if (format != DRM_FORMAT_ARGB8888 && format != DRM_FORMAT_XRGB8888)
        return EGL_FALSE;

    if (num_modifiers)
        *num_modifiers = 1;
    if (max_modifiers > 0 && modifiers)
        modifiers[0] = DRM_FORMAT_MOD_LINEAR;
    if (max_modifiers > 0 && external_only)
        external_only[0] = EGL_FALSE;

    return EGL_TRUE;
}

extern "C" EGLBoolean membranews_eglQueryDmaBufFormatsEXT(
    EGLDisplay dpy, EGLint max_formats, EGLint* formats, EGLint* num_formats) {
    (void)dpy;

    if (!num_formats)
        return EGL_FALSE;

    *num_formats = 2;
    if (max_formats > 0 && formats) {
        formats[0] = DRM_FORMAT_ARGB8888;
        if (max_formats > 1)
            formats[1] = DRM_FORMAT_XRGB8888;
    }

    return EGL_TRUE;
}

extern "C" EGLBoolean membranews_eglGetConfigAttrib(
    EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value) {
    if (attribute == EGL_NATIVE_VISUAL_ID) {
        *value = DRM_FORMAT_ARGB8888;
        return EGL_TRUE;
    }

    return EGL_FALSE;
}

struct ws_module ws_module_info = {
    membranews_init_module,
    membranews_GetDisplay,
    membranews_Terminate,
    membranews_CreateWindow,
    membranews_DestroyWindow,
    membranews_eglGetProcAddress,
    membranews_passthroughImageKHR,
    membranews_eglQueryString,
    membranews_prepareSwap,
    membranews_finishSwap,
    membranews_setSwapInterval,
    membranews_releaseDisplay,
    membranews_eglInitialized,
    membranews_eglGetConfigAttrib,
    membranews_eglQueryDmaBufModifiersEXT,
    membranews_eglQueryDmaBufFormatsEXT,
};
