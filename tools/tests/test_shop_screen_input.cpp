// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <vector>

namespace {

enum AppletOperationMode { AppletOperationMode_Handheld, AppletOperationMode_Console };
enum LblBacklightSwitchStatus { LblBacklightSwitchStatus_Disabled, LblBacklightSwitchStatus_Enabled };

struct ScreenService {
    AppletOperationMode mode{AppletOperationMode_Handheld};
    bool backlight_on{true};
    int result{}, requests{};
} screen;

using Mutex = std::mutex;
#define SCOPED_MUTEX(pointer) std::scoped_lock lock{*(pointer)}

Mutex g_screen_toggle_mutex;
unsigned g_screen_toggle_ref_count{};
bool g_screen_disabled{};

auto appletGetOperationMode() -> AppletOperationMode { return screen.mode; }

// The docked console reports its built-in LCD as off even though the external
// display is showing the progress widget.
[[maybe_unused]] auto lblGetBacklightSwitchStatus(LblBacklightSwitchStatus* status) -> int {
    *status = screen.backlight_on ? LblBacklightSwitchStatus_Enabled : LblBacklightSwitchStatus_Disabled;
    return 0;
}

auto appletSetLcdBacklightOffEnabled(bool disable) -> int {
    ++screen.requests;
    if (!screen.result) screen.backlight_on = !disable;
    return screen.result;
}

bool R_SUCCEEDED(int rc) { return rc == 0; }
bool R_FAILED(int rc) { return !R_SUCCEEDED(rc); }
[[maybe_unused]] auto R_VALUE(int rc) -> unsigned { return static_cast<unsigned>(rc); }
void log_write(const char*, ...) {}

namespace audio {
enum class State { Finished };
int GetProgress(int, std::nullptr_t, State*) { return -1; }
void SeekSong(int, int) {}
}

#define ENABLE_OWNFOIL
namespace ui {
struct InstallQueue {
    static auto Get() -> InstallQueue& {
        static InstallQueue queue;
        return queue;
    }
    void Update() {}
};
}

struct Controller {
    std::uint64_t m_kdown{}, m_kheld{}, m_kup{};
    void Reset() { m_kdown = m_kup = 0; }
};

struct Touch {
    bool is_touching{}, is_clicked{}, is_tap{}, is_scroll{}, is_end{};
};

struct Widget {
    Controller input;
    void Update(const Controller* controller, const Touch*) { input = *controller; }
    bool ShouldPop() const { return false; }
    void OnFocusGained() {}
};

struct App {
    int m_background_music{};
    Controller m_controller;
    Touch m_touch_info;
    std::vector<Widget*> m_widgets;

    void Update();
    static bool IsScreenToggleEnabled();
    static bool IsScreenDisabled();
    static void SetScreenDisabled(bool disable);
    static void Exit() { assert(false); }
};

#include "production_screen_input.inc"

constexpr std::uint64_t B = 1 << 1, X = 1 << 2;

struct Fixture {
    Widget widget;
    App app;

    explicit Fixture(AppletOperationMode mode) {
        screen = {mode, false, 0, 0};
        g_screen_disabled = false;
        g_screen_toggle_ref_count = 1;
        app.m_widgets.push_back(&widget);
    }

    void Press(std::uint64_t input, bool forwarded) {
        app.m_controller = {input, input, 0};
        app.Update();
        assert(widget.input.m_kdown == (forwarded ? input : 0));
        assert(widget.input.m_kheld == (forwarded ? input : 0));
    }
};

void CheckDockedButtons() {
    Fixture fixture{AppletOperationMode_Console};
    fixture.Press(B, true);
    fixture.Press(X, true);
    assert(!App::IsScreenToggleEnabled() && !App::IsScreenDisabled());
    App::SetScreenDisabled(true);
    assert(screen.requests == 0);
}

void CheckHandheldButtons() {
    Fixture fixture{AppletOperationMode_Handheld};
    assert(App::IsScreenToggleEnabled());
    fixture.Press(B, true);
    assert(screen.requests == 0);

    screen.result = 1;
    App::SetScreenDisabled(true);
    fixture.Press(B, true);
    assert(screen.requests == 1);

    screen.result = 0;
    App::SetScreenDisabled(true);
    assert(App::IsScreenDisabled() && !screen.backlight_on);
    fixture.Press(B, false);
    assert(!App::IsScreenDisabled() && screen.backlight_on && screen.requests == 3);
    fixture.Press(B, true);
    assert(screen.requests == 3);
}

void CheckDockingDuringScreenOff() {
    Fixture fixture{AppletOperationMode_Handheld};
    App::SetScreenDisabled(true);
    assert(App::IsScreenDisabled() && screen.requests == 1);
    screen.mode = AppletOperationMode_Console;
    fixture.Press(B, true);
    assert(!App::IsScreenDisabled());
    fixture.Press(X, true);
}

} // namespace

int main() {
    CheckDockedButtons();
    CheckHandheldButtons();
    CheckDockingDuringScreenOff();
    std::cout << "Shop docked controller and handheld wake input fixtures passed\n";
}
