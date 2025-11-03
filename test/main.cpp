#include "window_manager/window_manager.hpp"

#include <cstdio>
#include <thread>
#include <chrono>

int main(int, char **)
{
    const auto manager = wm::WindowManager::createDefault();
    if (!manager) {
        std::fprintf(stderr, "Failed to create WindowManager (Wayland)\n");
        return 1;
    }

    auto win1 = manager->createWindow(640, 400, "Window Manager Test");
    if (!win1) {
        std::fprintf(stderr, "Failed to create window\n");
        return 1;
    }

    win1->setAppId("com.shivang51.test");
    win1->setTitle("Test Window");

    manager->setErrorCallback([](wm::WmError err, const std::string &msg){
        std::fprintf(stderr, "[WM ERROR] %d: %s\n", static_cast<int>(err), msg.c_str());
    });

    win1->setEventCallback([](wm::WmEvent ev, wm::Window &win){
        switch (ev) {
            case wm::WmEvent::WindowConfigured: std::fprintf(stderr, "[EVENT] configured\n"); break;
            case wm::WmEvent::WindowResized: 
                std::fprintf(stderr, "[EVENT] resized to %dx%d\n", win.getWidth(), win.getHeight()); 
                break;
            case wm::WmEvent::WindowFocusGained: std::fprintf(stderr, "[EVENT] focus gained\n"); break;
            case wm::WmEvent::WindowFocusLost: std::fprintf(stderr, "[EVENT] focus lost\n"); break;
            case wm::WmEvent::WindowCloseRequested: std::fprintf(stderr, "[EVENT] close requested\n"); break;
            default: break;
        }
    });

    win1->setMouseCallback([](const wm::MouseEvent &ev, wm::Window &){
        if (ev.action == wm::MouseAction::Move) {
            std::fprintf(stderr, "[MOUSE] move at (%.1f, %.1f)\n", ev.x, ev.y);
        } else if (ev.action == wm::MouseAction::Wheel) {
            std::fprintf(stderr, "[MOUSE] wheel (%.1f, %.1f) at (%.1f, %.1f)\n", ev.deltaX, ev.deltaY, ev.x, ev.y);
        } else {
            const char *btn = (ev.button == wm::MouseButton::Left) ? "L" : 
                             (ev.button == wm::MouseButton::Right) ? "R" : "M";
            const char *act = (ev.action == wm::MouseAction::Press) ? "press" : "release";
            std::fprintf(stderr, "[MOUSE] %s %s at (%.1f, %.1f)\n", btn, act, ev.x, ev.y);
        }
    });

    int frameCount = 0;
    while (!win1->shouldClose()) {
        manager->pollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        win1->setTitle(std::format("Frame Count {}", frameCount++));
    }
    return 0;
}