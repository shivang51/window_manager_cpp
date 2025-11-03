#pragma once

#include <wayland-client.h>
#include <sys/mman.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "window_manager/window_manager.hpp"

struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace wm::wayland_impl {

struct MmapDeleter {
    size_t m_size = 0;
    void operator()(void *ptr) const {
        if (ptr && ptr != MAP_FAILED) {
            munmap(ptr, m_size);
        }
    }
};
using MmapUniquePtr = std::unique_ptr<void, MmapDeleter>;

struct ShmBuffer {
    wl_buffer *buffer = nullptr;
    MmapUniquePtr data{};
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

    std::shared_ptr<wm::Window> createWindow(int width, int height, const std::string &title) override;
    int run() override;
    void requestQuit() override;
    void pollEvents() override;
    void waitEvents() override;

    void setEventCallback(const wm::EventCallback &cb) override { m_eventCb = cb; }
    void setErrorCallback(const wm::ErrorCallback &cb) override { m_errorCb = cb; }

    wl_display *display() const { return m_display; }
    wl_compositor *compositor() const { return m_compositor; }
    wl_shm *shm() const { return m_shm; }
    xdg_wm_base *wm_base() const { return m_xdg_wm_base; }
    wl_seat *seat() const { return m_seat; }

    static std::unique_ptr<wm::WindowManager> create();

    static void handle_global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void handle_global_remove(void *data, wl_registry *registry, uint32_t name);
    static void handle_wm_base_ping(void *data, xdg_wm_base *wm, uint32_t serial);
    static void handle_seat_capabilities(void *data, wl_seat *seat, uint32_t caps);
    static void handle_seat_name(void *data, wl_seat *seat, const char *name);

private:
    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_shm *m_shm = nullptr;
    xdg_wm_base *m_xdg_wm_base = nullptr;
    wl_seat *m_seat = nullptr;
    bool m_should_quit = false;

    std::vector<std::weak_ptr<WaylandWindow>> m_windows;

    wm::EventCallback m_eventCb{};
    wm::ErrorCallback m_errorCb{};
};

class WaylandWindow final : public wm::Window, public std::enable_shared_from_this<WaylandWindow> {
public:
    WaylandWindow(WaylandWindowManager &mgr, int width, int height, const std::string &title);
    ~WaylandWindow() override;

    void setTitle(const std::string &title) override;
    void show() override;
    bool shouldClose() const override { return m_shouldClose; }
    void setEventCallback(const wm::EventCallback &cb) override { m_windowEventCb = cb; }
    void setMouseCallback(const wm::MouseCallback &cb) override { m_mouseCb = cb; }

    void setup_pointer(WaylandWindowManager *mgr);

    static void handle_xdg_surface_configure(void *data, xdg_surface *xdg_surface_obj, uint32_t serial);
    static void handle_toplevel_configure(void *data, xdg_toplevel *toplevel, int32_t width, int32_t height, wl_array *states);
    static void handle_toplevel_close(void *data, xdg_toplevel *toplevel);
    static void handle_pointer_enter(void *data, wl_pointer *pointer, uint32_t serial, wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
    static void handle_pointer_leave(void *data, wl_pointer *pointer, uint32_t serial, wl_surface *surface);
    static void handle_pointer_motion(void *data, wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void handle_pointer_button(void *data, wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    static void handle_pointer_axis(void *data, wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
    static void handle_pointer_frame(void *data, wl_pointer *pointer);
    static void handle_pointer_axis_source(void *data, wl_pointer *pointer, uint32_t axis_source);
    static void handle_pointer_axis_stop(void *data, wl_pointer *pointer, uint32_t time, uint32_t axis);
    static void handle_pointer_axis_discrete(void *data, wl_pointer *pointer, uint32_t axis, int32_t discrete);
    static void handle_pointer_axis_value120(void *data, wl_pointer *pointer, uint32_t axis, int32_t value120);

private:
    static int create_shm_file(size_t size);
    bool create_buffer(int width, int height, uint32_t xrgb);

    WaylandWindowManager &m_mgr;
    wl_surface *m_surface = nullptr;
    xdg_surface *m_xdg_surface = nullptr;
    xdg_toplevel *m_toplevel = nullptr;
    wl_pointer *m_pointer = nullptr;
    ShmBuffer m_buf{};
    bool m_configured = false;
    bool m_shouldClose = false;
    double m_pointerX = 0.0;
    double m_pointerY = 0.0;
    wm::EventCallback m_windowEventCb{};
    wm::MouseCallback m_mouseCb{};
};

}


