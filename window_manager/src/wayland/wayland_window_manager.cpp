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
}
#endif
#include <sys/select.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

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
        return;
    }
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &REGISTRY_LISTENER, this);
    wl_display_roundtrip(m_display);
}

WaylandWindowManager::~WaylandWindowManager()
{
    // Windows should have been destroyed before display disconnect
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

std::shared_ptr<wm::Window> WaylandWindowManager::create_window(int width, int height, const std::string &title)
{
    auto win = std::make_shared<WaylandWindow>(*this, width, height, title);
    m_windows.emplace_back(win);
    win->show();
    return win;
}

int WaylandWindowManager::run()
{
    if (!m_display) return 1;
    while (!m_should_quit && wl_display_dispatch(m_display) != -1) {
    }
    return 0;
}

void WaylandWindowManager::request_quit()
{
    m_should_quit = true;
}

void WaylandWindowManager::poll_events()
{
    if (!m_display) return;
    // Dispatch already-read events
    wl_display_dispatch_pending(m_display);
    // Flush outgoing requests
    wl_display_flush(m_display);

    // Non-blocking read: check fd readability and dispatch if data is available
    const int fd = wl_display_get_fd(m_display);
    if (fd >= 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        timeval tv{}; // zero timeout
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0 && FD_ISSET(fd, &fds)) {
            wl_display_dispatch(m_display);
        }
    }
}

void WaylandWindowManager::wait_events()
{
    if (!m_display) return;
    wl_display_flush(m_display);
    wl_display_dispatch(m_display); // blocks until one batch processed
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
    }
}

void WaylandWindowManager::handle_global_remove(void *data, wl_registry *registry, const uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

void WaylandWindowManager::handle_wm_base_ping(void *data, xdg_wm_base *wm, const uint32_t serial)
{
    (void)data; xdg_wm_base_pong(wm, serial);
}

// ============== Window ==============

static constexpr xdg_surface_listener XDG_SURFACE_LISTENER = {
    .configure = WaylandWindow::handle_xdg_surface_configure,
};

static constexpr xdg_toplevel_listener XDG_TOPLEVEL_LISTENER = {
    .configure = WaylandWindow::handle_toplevel_configure,
    .close = WaylandWindow::handle_toplevel_close,
};

WaylandWindow::WaylandWindow(WaylandWindowManager &mgr, const int width, const int height, const std::string &title)
    : m_mgr(mgr)
{
    m_surface = wl_compositor_create_surface(mgr.compositor());
    m_xdg_surface = xdg_wm_base_get_xdg_surface(mgr.wm_base(), m_surface);
    xdg_surface_add_listener(m_xdg_surface, &XDG_SURFACE_LISTENER, this);
    m_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
    xdg_toplevel_add_listener(m_toplevel, &XDG_TOPLEVEL_LISTENER, this);
    set_title(title);
    wl_surface_commit(m_surface);

    create_buffer(width, height, 0xFF2BB3AA);
}

WaylandWindow::~WaylandWindow()
{
    if (m_toplevel) xdg_toplevel_destroy(m_toplevel);
    if (m_xdg_surface) xdg_surface_destroy(m_xdg_surface);
    if (m_surface) wl_surface_destroy(m_surface);
    if (m_buf.buffer) wl_buffer_destroy(m_buf.buffer);
    if (m_buf.data && m_buf.data != MAP_FAILED) munmap(m_buf.data, m_buf.size);
    if (m_buf.fd >= 0) close(m_buf.fd);
}

void WaylandWindow::set_title(const std::string &title)
{
    if (m_toplevel) xdg_toplevel_set_title(m_toplevel, title.c_str());
}

void WaylandWindow::show()
{
    // Wait until configured to attach
    while (!m_configured) {
        if (wl_display_roundtrip(m_mgr.display()) < 0) break;
    }
    if (m_buf.buffer && m_surface) {
        wl_surface_attach(m_surface, m_buf.buffer, 0, 0);
        wl_surface_commit(m_surface);
    }
}

void WaylandWindow::handle_xdg_surface_configure(void *data, xdg_surface *xdg_surface_obj, const uint32_t serial)
{
    auto *self = static_cast<WaylandWindow *>(data);
    xdg_surface_ack_configure(xdg_surface_obj, serial);
    self->m_configured = true;
}

void WaylandWindow::handle_toplevel_configure(void *data, xdg_toplevel *toplevel, const int32_t width, const int32_t height, wl_array *states)
{
    (void)data; (void)toplevel; (void)width; (void)height; (void)states;
}

void WaylandWindow::handle_toplevel_close(void *data, xdg_toplevel *toplevel)
{
    auto *self = static_cast<WaylandWindow *>(data);
    (void)toplevel;
    self->m_should_close = true;
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

    m_buf.data = mmap(nullptr, m_buf.size, PROT_READ | PROT_WRITE, MAP_SHARED, m_buf.fd, 0);
    if (m_buf.data == MAP_FAILED) {
        close(m_buf.fd);
        m_buf.fd = -1;
        return false;
    }

    wl_shm_pool *pool = wl_shm_create_pool(m_mgr.shm(), m_buf.fd, static_cast<int>(m_buf.size));
    m_buf.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, m_buf.stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    auto *pixels = static_cast<uint32_t *>(m_buf.data);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels[y * width + x] = xrgb;
        }
    }
    return true;
}

} // namespace wm::wayland_impl


