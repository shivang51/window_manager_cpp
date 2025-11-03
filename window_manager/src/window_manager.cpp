#include <memory>

#include "window_manager/window_manager.hpp"
#include "wayland/wayland_window_manager.hpp"

namespace wm {

std::unique_ptr<WindowManager> WindowManager::create_default()
{
#if defined(__linux__)
    return WindowManager::create_wayland();
#else
    return nullptr;
#endif
}

std::unique_ptr<WindowManager> WindowManager::create_wayland()
{
    return wayland_impl::WaylandWindowManager::create();
}

}

