#include <memory>

#include "window_manager/window_manager.hpp"
#include "wayland/wayland_window_manager.hpp"

namespace wm {

std::unique_ptr<WindowManager> WindowManager::createDefault()
{
#ifdef WM_USE_VULKAN
    std::printf("[WindowManager] With support for vulkan, make sure you create the surface\n");
#endif
#if defined(__linux__)
    return WindowManager::createWayland();
#else
    return nullptr;
#endif
}

std::unique_ptr<WindowManager> WindowManager::createWayland()
{
    return wayland_impl::WaylandWindowManager::create();
}

}

