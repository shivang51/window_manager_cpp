// Public API for cross-platform window management (Wayland implementation currently)

#pragma once

#include <memory>
#include <string>

namespace wm {

class Window {
public:
    virtual ~Window() = default;
    virtual void set_title(const std::string &title) = 0;
    virtual void show() = 0;
    virtual bool should_close() const = 0;
};

class WindowManager {
public:
    virtual ~WindowManager() = default;

    // Create a window of given size and title
    virtual std::shared_ptr<Window> create_window(int width, int height, const std::string &title) = 0;

    // Run the main loop until quit
    virtual int run() = 0;
    virtual void request_quit() = 0;

    virtual void poll_events() = 0;   // process pending events without blocking
    virtual void wait_events() = 0;   // block until at least one event is processed

    // Factories
    static std::unique_ptr<WindowManager> create_default();
    static std::unique_ptr<WindowManager> create_wayland();
};

}


