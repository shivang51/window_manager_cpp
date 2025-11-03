#pragma once

#include <memory>
#include <string>
#include <functional>

namespace wm {

enum class WmError : int {
    None = 0,
    ConnectDisplayFailed,
    MissingGlobals,
    CreateSurfaceFailed,
    ShmFailed,
    ProtocolError,
};

enum class WmEvent : int {
    None = 0,
    WindowConfigured,
    WindowCloseRequested,
    WindowResized,
    WindowFocusGained,
    WindowFocusLost,
    Ping,
};

class Window;

using EventCallback = std::function<void(WmEvent, Window&)>;
using ErrorCallback = std::function<void(WmError, const std::string&)>;

enum class MouseButton : int {
    Left = 0x110,
    Right = 0x111,
    Middle = 0x112,
};

enum class MouseAction : int {
    Press = 0,
    Release = 1,
    Move = 2,
    Wheel = 3,
};

struct MouseEvent {
    double x = 0.0;
    double y = 0.0;
    MouseButton button = MouseButton::Left;
    MouseAction action = MouseAction::Press;
    double deltaX = 0.0;
    double deltaY = 0.0;
};

using MouseCallback = std::function<void(const MouseEvent&, Window&)>;

class Window {
public:
    virtual ~Window() = default;
    virtual void setTitle(const std::string &title) = 0;
    virtual void setAppId(const std::string &appId) = 0;
    virtual std::string getTitle() const = 0;
    virtual std::string getAppId() const = 0;
    virtual std::string getInitialTitle() const = 0;
    virtual std::string getInitialAppId() const = 0;
    virtual void show() = 0;
    virtual bool shouldClose() const = 0;
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
    virtual void setEventCallback(const EventCallback &cb) = 0;
    virtual void setMouseCallback(const MouseCallback &cb) = 0;
};

class WindowManager {
public:
    virtual ~WindowManager() = default;
    virtual std::shared_ptr<Window> createWindow(int width, int height, const std::string &title) = 0;
    virtual int run() = 0;
    virtual void requestQuit() = 0;
    virtual void pollEvents() = 0;
    virtual void waitEvents() = 0;
    virtual void setEventCallback(const EventCallback &cb) = 0;
    virtual void setErrorCallback(const ErrorCallback &cb) = 0;
    static std::unique_ptr<WindowManager> createDefault();
    static std::unique_ptr<WindowManager> createWayland();
};

}


