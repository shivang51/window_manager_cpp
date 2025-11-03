// Use the window_manager API to create and run a Wayland window

#include <cstdio>
#include <thread>
#include <chrono>
#include "window_manager/window_manager.hpp"

int main(int, char **)
{
    const auto manager = wm::WindowManager::create_default();
    if (!manager) {
        std::fprintf(stderr, "Failed to create WindowManager (Wayland)\n");
        return 1;
    }

    auto win1 = manager->create_window(640, 400, "Window Manager Test");
    if (!win1) {
        std::fprintf(stderr, "Failed to create window\n");
        return 1;
    }

    // GLFW-like loop using poll_events; render ticks at ~60fps
    while (!win1->should_close()) {
        manager->poll_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}