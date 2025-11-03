#include "wayland_window_manager.hpp"
#include <wayland-client.h>
#if __has_include(<xdg-shell-client-protocol.h>)
#include <xdg-shell-client-protocol.h>
#else
extern "C" {
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_wm_base_listener { void (*ping)(void*, xdg_wm_base*, uint32_t); };
struct xdg_surface_listener { void (*configure)(void*, xdg_surface*, uint32_t); };
struct xdg_toplevel_listener { void (*configure)(void*, xdg_toplevel*, int32_t, int32_t, wl_array*); void (*close)(void*, xdg_toplevel*); };
extern const struct wl_interface xdg_wm_base_interface;
extern const struct wl_interface xdg_surface_interface;
extern const struct wl_interface xdg_toplevel_interface;
enum xdg_toplevel_state { XDG_TOPLEVEL_STATE_ACTIVATED = 0 };
xdg_surface* xdg_wm_base_get_xdg_surface(xdg_wm_base*, wl_surface*);
int xdg_wm_base_add_listener(xdg_wm_base*, const xdg_wm_base_listener*, void*);
void xdg_wm_base_pong(xdg_wm_base*, uint32_t);
int xdg_surface_add_listener(xdg_surface*, const xdg_surface_listener*, void*);
xdg_toplevel* xdg_surface_get_toplevel(xdg_surface*);
void xdg_surface_destroy(xdg_surface*);
void xdg_surface_ack_configure(xdg_surface*, uint32_t);
int xdg_toplevel_add_listener(xdg_toplevel*, const xdg_toplevel_listener*, void*);
void xdg_toplevel_destroy(xdg_toplevel*);
void xdg_toplevel_set_title(xdg_toplevel*, const char*);
void xdg_toplevel_set_app_id(xdg_toplevel*, const char*);
}
#endif
#include <sys/select.h>
#include <linux/input-event-codes.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#endif

namespace wm::wayland_impl {

static constexpr xdg_wm_base_listener XDG_WM_BASE_LISTENER = {
    .ping = WaylandWindowManager::handle_wm_base_ping,
};

static constexpr wl_registry_listener REGISTRY_LISTENER = {
    .global = WaylandWindowManager::handle_global,
    .global_remove = WaylandWindowManager::handle_global_remove,
};

WaylandWindowManager::WaylandWindowManager()
{
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        std::fprintf(stderr, "[WM] Failed to connect to Wayland display\n");
        if (m_errorCb) m_errorCb(wm::WmError::ConnectDisplayFailed, "wl_display_connect failed");
        return;
    }
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &REGISTRY_LISTENER, this);
    wl_display_roundtrip(m_display);

    if (!m_compositor || !m_shm || !m_xdg_wm_base) {
        if (m_errorCb) m_errorCb(wm::WmError::MissingGlobals, "Required globals missing");
    }
}

WaylandWindowManager::~WaylandWindowManager()
{
    if (m_display) {
        wl_display_disconnect(m_display);
        m_display = nullptr;
    }
}

std::unique_ptr<wm::WindowManager> WaylandWindowManager::create()
{
    auto mgr = std::make_unique<WaylandWindowManager>();
    if (!mgr->m_display || !mgr->m_compositor || !mgr->m_shm || !mgr->m_xdg_wm_base) {
        return nullptr;
    }
    return std::unique_ptr<wm::WindowManager>(mgr.release());
}

std::vector<std::string> WaylandWindowManager::getVulkanInstanceExtensions() const
{
    return {"VK_KHR_surface", "VK_KHR_wayland_surface"};
}

VkResult WaylandWindowManager::createVulkanWindowSurface(
    VkInstance instance,
    wm::Window &window,
    const VkAllocationCallbacks *allocator,
    VkSurfaceKHR *surface
) const
{
#if __has_include(<vulkan/vulkan.h>)
    // Downcast to access Wayland-native handles
    auto *wlWin = dynamic_cast<WaylandWindow *>(&window);
    if (!wlWin) {
        return (VkResult)(-1);
    }
    wl_display *wlDisplay = m_display;
    wl_surface *wlSurface = wlWin->m_surface; // friend access
    if (!wlDisplay || !wlSurface) {
        return (VkResult)(-1);
    }

    VkWaylandSurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = nullptr;
    createInfo.flags = 0;
    createInfo.display = wlDisplay;
    createInfo.surface = wlSurface;

    auto fpCreateWaylandSurfaceKHR = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR")
    );
    if (!fpCreateWaylandSurfaceKHR) {
        return (VkResult)(-1);
    }
    return fpCreateWaylandSurfaceKHR(instance, &createInfo, allocator, surface);
#else
    (void)instance; (void)window; (void)allocator; (void)surface;
    return (VkResult)(-1);
#endif
}

std::shared_ptr<wm::Window> WaylandWindowManager::createWindow(int width, int height, const std::string &title)
{
    auto win = std::make_shared<WaylandWindow>(*this, width, height, title);
    m_windows.emplace_back(win);
    return win;
}

int WaylandWindowManager::run()
{
    if (!m_display) return 1;
    while (!m_should_quit && wl_display_dispatch(m_display) != -1) {
        for (auto &weak_win : m_windows) {
            if (auto win = weak_win.lock()) {
                static_cast<WaylandWindow*>(win.get())->mapIfNeeded();
            }
        }
    }
    return 0;
}

void WaylandWindowManager::requestQuit()
{
    m_should_quit = true;
}

void WaylandWindowManager::pollEvents()
{
    if (!m_display) return;
    for (auto &weak_win : m_windows) {
        if (auto win = weak_win.lock()) {
            static_cast<WaylandWindow*>(win.get())->mapIfNeeded();
        }
    }
    wl_display_dispatch_pending(m_display);
    wl_display_flush(m_display);

    const int fd = wl_display_get_fd(m_display);
    if (fd >= 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        timeval tv{};
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0 && FD_ISSET(fd, &fds)) {
            wl_display_dispatch(m_display);
        }
    }
}

void WaylandWindowManager::waitEvents()
{
    if (!m_display) return;
    for (auto &weak_win : m_windows) {
        if (auto win = weak_win.lock()) {
            static_cast<WaylandWindow*>(win.get())->mapIfNeeded();
        }
    }
    wl_display_flush(m_display);
    wl_display_dispatch(m_display);
}

void WaylandWindowManager::handle_global(void *data, wl_registry *registry, const uint32_t name, const char *interface, const uint32_t version)
{
    auto *self = static_cast<WaylandWindowManager *>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        self->m_compositor = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        self->m_shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        self->m_xdg_wm_base = static_cast<xdg_wm_base *>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(self->m_xdg_wm_base, &XDG_WM_BASE_LISTENER, self);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        self->m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, version < 7 ? version : 7));
        static constexpr wl_seat_listener SEAT_LISTENER = {
            .capabilities = WaylandWindowManager::handle_seat_capabilities,
            .name = WaylandWindowManager::handle_seat_name,
        };
        wl_seat_add_listener(self->m_seat, &SEAT_LISTENER, self);
    }
}

void WaylandWindowManager::handle_global_remove(void *data, wl_registry *registry, const uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

void WaylandWindowManager::handle_wm_base_ping(void *data, xdg_wm_base *wm, const uint32_t serial)
{
    auto *self = static_cast<WaylandWindowManager *>(data);
    xdg_wm_base_pong(wm, serial);
}

void WaylandWindowManager::handle_seat_capabilities(void *data, wl_seat *seat, const uint32_t caps)
{
    auto *self = static_cast<WaylandWindowManager *>(data);
    (void)seat;
    if (!(caps & WL_SEAT_CAPABILITY_POINTER)) return;
    
    for (auto &weak_win : self->m_windows) {
        if (auto win = weak_win.lock()) {
            static_cast<WaylandWindow *>(win.get())->setup_pointer(self);
        }
    }
}

void WaylandWindowManager::handle_seat_name(void *data, wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static constexpr xdg_surface_listener XDG_SURFACE_LISTENER = {
    .configure = WaylandWindow::handle_xdg_surface_configure,
};

static constexpr xdg_toplevel_listener XDG_TOPLEVEL_LISTENER = {
    .configure = WaylandWindow::handle_toplevel_configure,
    .close = WaylandWindow::handle_toplevel_close,
};

WaylandWindow::WaylandWindow(WaylandWindowManager &mgr, const int width, const int height, const std::string &title)
    : m_mgr(mgr), m_width(width), m_height(height), m_title(title), m_initialTitle(title)
{
    m_surface = wl_compositor_create_surface(mgr.compositor());
    m_xdg_surface = xdg_wm_base_get_xdg_surface(mgr.wm_base(), m_surface);
    xdg_surface_add_listener(m_xdg_surface, &XDG_SURFACE_LISTENER, this);
    m_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
    xdg_toplevel_add_listener(m_toplevel, &XDG_TOPLEVEL_LISTENER, this);
    xdg_toplevel_set_title(m_toplevel, title.c_str());

    setup_pointer(&mgr);
    create_buffer(width, height, 0xFF2BB3AA);
}

void WaylandWindow::setup_pointer(WaylandWindowManager *mgr)
{
    if (!mgr || m_pointer || !mgr->seat()) return;
    m_pointer = wl_seat_get_pointer(mgr->seat());
    if (!m_pointer) return;
    static constexpr wl_pointer_listener POINTER_LISTENER = {
        .enter = handle_pointer_enter,
        .leave = handle_pointer_leave,
        .motion = handle_pointer_motion,
        .button = handle_pointer_button,
        .axis = handle_pointer_axis,
        .frame = handle_pointer_frame,
        .axis_source = handle_pointer_axis_source,
        .axis_stop = handle_pointer_axis_stop,
        .axis_discrete = handle_pointer_axis_discrete,
        .axis_value120 = handle_pointer_axis_value120,
    };
    wl_pointer_add_listener(m_pointer, &POINTER_LISTENER, this);
}

void WaylandWindow::mapIfNeeded()
{
    if (!m_surface) return;

    if (!m_initialCommitted) {
        if (m_toplevel) {
            const char *appIdToUse = !m_appId.empty() ? m_appId.c_str() : (!m_initialAppId.empty() ? m_initialAppId.c_str() : nullptr);
            if (appIdToUse) xdg_toplevel_set_app_id(m_toplevel, appIdToUse);
            if (!m_title.empty()) xdg_toplevel_set_title(m_toplevel, m_title.c_str());
        }
        wl_surface_commit(m_surface);
        m_initialCommitted = true;
        return;
    }

    if (m_configured && !m_mapped && m_buf.buffer) {
        if (m_toplevel) {
            const char *appIdToUse = !m_appId.empty() ? m_appId.c_str() : (!m_initialAppId.empty() ? m_initialAppId.c_str() : nullptr);
            if (appIdToUse) xdg_toplevel_set_app_id(m_toplevel, appIdToUse);
            if (!m_title.empty()) xdg_toplevel_set_title(m_toplevel, m_title.c_str());
        }
        wl_surface_attach(m_surface, m_buf.buffer, 0, 0);
        wl_surface_commit(m_surface);
        m_mapped = true;
    }
}

WaylandWindow::~WaylandWindow()
{
    if (m_pointer) wl_pointer_destroy(m_pointer);
    if (m_toplevel) xdg_toplevel_destroy(m_toplevel);
    if (m_xdg_surface) xdg_surface_destroy(m_xdg_surface);
    if (m_surface) wl_surface_destroy(m_surface);
    if (m_buf.buffer) wl_buffer_destroy(m_buf.buffer);
    m_buf.data.reset();
    if (m_buf.fd >= 0) close(m_buf.fd);
}

void WaylandWindow::setTitle(const std::string &title)
{
    m_title = title;
    if (m_toplevel) xdg_toplevel_set_title(m_toplevel, title.c_str());
}

void WaylandWindow::setAppId(const std::string &appId)
{
    if (!appId.empty()) {
        if (m_initialAppId.empty()) {
            m_initialAppId = appId;
        }
        m_appId = appId;
        if (m_toplevel) {
            xdg_toplevel_set_app_id(m_toplevel, appId.c_str());
            if (!m_configured) {
                wl_surface_commit(m_surface);
            }
        }
    } else {
        m_appId = appId;
    }
}

void WaylandWindow::show() {
    if (m_toplevel) {
        if (!m_initialAppId.empty()) {
            xdg_toplevel_set_app_id(m_toplevel, m_initialAppId.c_str());
        }
        if (!m_title.empty()) {
            xdg_toplevel_set_title(m_toplevel, m_title.c_str());
        }
    }

    // Ensure an initial commit occurs so the compositor can send the first configure
    if (m_surface) {
        wl_surface_commit(m_surface);
    }

    while (!m_configured) {
        if (wl_display_roundtrip(m_mgr.display()) < 0) break;
    }
    if (m_buf.buffer && m_surface) {
        if (m_toplevel) {
            // Re-assert app_id and title in the same commit as the first buffer attach
            const char *appIdToUse = !m_appId.empty()
                                     ? m_appId.c_str()
                                     : (!m_initialAppId.empty() ? m_initialAppId.c_str() : nullptr);
            if (appIdToUse) xdg_toplevel_set_app_id(m_toplevel, appIdToUse);
            if (!m_title.empty()) xdg_toplevel_set_title(m_toplevel, m_title.c_str());
        }
        wl_surface_attach(m_surface, m_buf.buffer, 0, 0);
        wl_surface_commit(m_surface);
    }
}

void WaylandWindow::handle_xdg_surface_configure(void *data, xdg_surface *xdg_surface_obj, const uint32_t serial)
{
    auto *self = static_cast<WaylandWindow *>(data);
    xdg_surface_ack_configure(xdg_surface_obj, serial);
    self->m_configured = true;
    if (self->m_windowEventCb) self->m_windowEventCb(wm::WmEvent::WindowConfigured, *self);
}

void WaylandWindow::handle_toplevel_configure(void *data, xdg_toplevel *toplevel, const int32_t width, const int32_t height, wl_array *states)
{
    auto *self = static_cast<WaylandWindow *>(data);
    (void)toplevel;
    if (!self || !self->m_windowEventCb) return;
    
    if (width > 0 && height > 0) {
        self->m_width = width;
        self->m_height = height;
        self->create_buffer(width, height, 0xFF030303);
        self->m_windowEventCb(wm::WmEvent::WindowResized, *self);
    }
    
    bool hasFocus = false;
    if (states) {
        const uint32_t *state = static_cast<const uint32_t *>(states->data);
        const size_t count = states->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; ++i) {
            if (state[i] == 0) {
                hasFocus = true;
                break;
            }
        }
    }
    
    const bool wasFocused = self->m_hasFocus;
    self->m_hasFocus = hasFocus;
    
    if (hasFocus && !wasFocused) {
        self->m_windowEventCb(wm::WmEvent::WindowFocusGained, *self);
    } else if (!hasFocus && wasFocused) {
        self->m_windowEventCb(wm::WmEvent::WindowFocusLost, *self);
    }
}

void WaylandWindow::handle_toplevel_close(void *data, xdg_toplevel *toplevel)
{
    auto *self = static_cast<WaylandWindow *>(data);
    (void)toplevel;
    self->m_shouldClose = true;
    if (self->m_windowEventCb) self->m_windowEventCb(wm::WmEvent::WindowCloseRequested, *self);
}

int WaylandWindow::create_shm_file(const size_t size)
{
    static int counter = 0;
    char name[64];
    std::snprintf(name, sizeof(name), "/wm-shm-%d-%d", getpid(), counter++);
    const int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    shm_unlink(name);
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool WaylandWindow::create_buffer(const int width, const int height, const uint32_t xrgb)
{
    m_buf.width = width;
    m_buf.height = height;
    m_buf.stride = width * 4;
    m_buf.size = static_cast<size_t>(m_buf.stride) * height;

    m_buf.fd = create_shm_file(m_buf.size);
    if (m_buf.fd < 0) return false;

    void *raw_data = mmap(nullptr, m_buf.size, PROT_READ | PROT_WRITE, MAP_SHARED, m_buf.fd, 0);
    if (raw_data == MAP_FAILED) {
        close(m_buf.fd);
        m_buf.fd = -1;
        return false;
    }

    MmapDeleter deleter{.m_size = m_buf.size};
    m_buf.data = MmapUniquePtr(raw_data, deleter);

    wl_shm_pool *pool = wl_shm_create_pool(m_mgr.shm(), m_buf.fd, static_cast<int>(m_buf.size));
    m_buf.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, m_buf.stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    auto *pixels = static_cast<uint32_t *>(m_buf.data.get());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels[y * width + x] = xrgb;
        }
    }
    return true;
}

void WaylandWindow::handle_pointer_enter(void *data, wl_pointer *pointer, const uint32_t serial, wl_surface *surface, const wl_fixed_t x, const wl_fixed_t y)
{
    auto *self = static_cast<WaylandWindow *>(data);
    (void)pointer; (void)serial; (void)surface;
    if (!self) return;
    
    self->m_pointerX = wl_fixed_to_double(x);
    self->m_pointerY = wl_fixed_to_double(y);
}

void WaylandWindow::handle_pointer_leave(void *data, wl_pointer *pointer, const uint32_t serial, wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
}

void WaylandWindow::handle_pointer_motion(void *data, wl_pointer *pointer, const uint32_t time, const wl_fixed_t x, const wl_fixed_t y)
{
    auto *self = static_cast<WaylandWindow *>(data);
    (void)pointer; (void)time;
    if (!self || !self->m_mouseCb) return;
    
    self->m_pointerX = wl_fixed_to_double(x);
    self->m_pointerY = wl_fixed_to_double(y);
    
    wm::MouseEvent ev{
        .x = self->m_pointerX,
        .y = self->m_pointerY,
        .action = wm::MouseAction::Move
    };
    self->m_mouseCb(ev, *self);
}

void WaylandWindow::handle_pointer_button(void *data, wl_pointer *pointer, const uint32_t serial, const uint32_t time, const uint32_t button, const uint32_t state)
{
    auto *self = static_cast<WaylandWindow *>(data);
    (void)pointer; (void)serial; (void)time;
    if (!self || !self->m_mouseCb) return;

    wm::MouseButton mb = wm::MouseButton::Left;
    if (button == BTN_LEFT) mb = wm::MouseButton::Left;
    else if (button == BTN_RIGHT) mb = wm::MouseButton::Right;
    else if (button == BTN_MIDDLE) mb = wm::MouseButton::Middle;

    wm::MouseEvent ev{
        .x = self->m_pointerX,
        .y = self->m_pointerY,
        .button = mb,
        .action = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? wm::MouseAction::Press : wm::MouseAction::Release
    };
    self->m_mouseCb(ev, *self);
}

void WaylandWindow::handle_pointer_axis(void *data, wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    auto *self = static_cast<WaylandWindow *>(data);
    (void)pointer; (void)time;
    if (!self || !self->m_mouseCb) return;
    
    const double delta = wl_fixed_to_double(value);
    wm::MouseEvent ev{
        .x = self->m_pointerX,
        .y = self->m_pointerY,
        .action = wm::MouseAction::Wheel
    };
    
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        ev.deltaY = delta;
    } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        ev.deltaX = delta;
    }
    
    self->m_mouseCb(ev, *self);
}

void WaylandWindow::handle_pointer_frame(void *data, wl_pointer *pointer)
{
    (void)data; (void)pointer;
}

void WaylandWindow::handle_pointer_axis_source(void *data, wl_pointer *pointer, uint32_t axis_source)
{
    (void)data; (void)pointer; (void)axis_source;
}

void WaylandWindow::handle_pointer_axis_stop(void *data, wl_pointer *pointer, uint32_t time, uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

void WaylandWindow::handle_pointer_axis_discrete(void *data, wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

void WaylandWindow::handle_pointer_axis_value120(void *data, wl_pointer *pointer, uint32_t axis, int32_t value120)
{
    (void)data; (void)pointer; (void)axis; (void)value120;
}

} // namespace wm::wayland_impl


