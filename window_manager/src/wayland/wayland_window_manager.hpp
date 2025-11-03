#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "window_manager/window_manager.hpp"

// Forward-declare xdg-shell types; defined in generated header
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace wm::wayland_impl {

struct ShmBuffer {
    wl_buffer *buffer = nullptr;
    void *data = nullptr;
    int fd = -1;
    int width = 0;
    int height = 0;
    int stride = 0;
    size_t size = 0;
};

class WaylandWindow;

class WaylandWindowManager final : public wm::WindowManager {
public:
    WaylandWindowManager();
    ~WaylandWindowManager() override;

    std::shared_ptr<wm::Window> create_window(int width, int height, const std::string &title) override;
    int run() override;
    void request_quit() override;
    void poll_events() override;
    void wait_events() override;

    wl_display *display() const { return m_display; }
    wl_compositor *compositor() const { return m_compositor; }
    wl_shm *shm() const { return m_shm; }
    xdg_wm_base *wm_base() const { return m_xdg_wm_base; }

    static std::unique_ptr<wm::WindowManager> create();

    // listener callbacks
    static void handle_global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void handle_global_remove(void *data, wl_registry *registry, uint32_t name);
    static void handle_wm_base_ping(void *data, xdg_wm_base *wm, uint32_t serial);

private:
    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_shm *m_shm = nullptr;
    xdg_wm_base *m_xdg_wm_base = nullptr;
    bool m_should_quit = false;

    std::vector<std::weak_ptr<WaylandWindow>> m_windows;
};

class WaylandWindow final : public wm::Window, public std::enable_shared_from_this<WaylandWindow> {
public:
    WaylandWindow(WaylandWindowManager &mgr, int width, int height, const std::string &title);
    ~WaylandWindow() override;

    void set_title(const std::string &title) override;
    void show() override;
    bool should_close() const override { return m_should_close; }

    // listeners
    static void handle_xdg_surface_configure(void *data, xdg_surface *xdg_surface_obj, uint32_t serial);
    static void handle_toplevel_configure(void *data, xdg_toplevel *toplevel, int32_t width, int32_t height, wl_array *states);
    static void handle_toplevel_close(void *data, xdg_toplevel *toplevel);

private:

    static int create_shm_file(size_t size);
    bool create_buffer(int width, int height, uint32_t xrgb);

private:
    WaylandWindowManager &m_mgr;
    wl_surface *m_surface = nullptr;
    xdg_surface *m_xdg_surface = nullptr;
    xdg_toplevel *m_toplevel = nullptr;
    ShmBuffer m_buf{};
    bool m_configured = false;
    bool m_should_close = false;
};

}


