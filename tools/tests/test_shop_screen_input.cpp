// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
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
std::optional<AppletOperationMode> g_screen_blanked_mode;

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
    static bool AcquireScreenToggle();
    static void ReleaseScreenToggle();
    static bool IsScreenToggleEnabled();
    static bool IsScreenDisabled();
    static void SetScreenDisabled(bool disable);
    static void Exit() { assert(false); }
};

#include "production_screen_input.inc"

constexpr std::uint64_t B = 1 << 1, X = 1 << 2, Y = 1 << 3;

struct Fixture {
    Widget widget;
    App app;

    explicit Fixture(AppletOperationMode mode) {
        screen = {mode, mode == AppletOperationMode_Handheld, 0, 0};
        assert(!App::IsScreenToggleEnabled() && !App::IsScreenDisabled());
        assert(App::AcquireScreenToggle());
        assert(App::IsScreenToggleEnabled() && screen.requests == 0);
        app.m_widgets.push_back(&widget);
    }

    ~Fixture() { App::ReleaseScreenToggle(); }

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
    assert(!App::IsScreenDisabled() && !screen.backlight_on);

    fixture.Press(Y, true);
    App::SetScreenDisabled(true);
    assert(App::IsScreenDisabled() && screen.requests == 0);
    fixture.Press(B, false);
    assert(!App::IsScreenDisabled() && screen.requests == 0);
    fixture.Press(B, true);
    fixture.Press(X, true);
    fixture.Press(Y, true);
}

void CheckHandheldButtons() {
    Fixture fixture{AppletOperationMode_Handheld};
    screen.backlight_on = false;
    fixture.Press(B, true);
    assert(screen.requests == 0);

    screen.backlight_on = true;
    screen.result = 1;
    App::SetScreenDisabled(true);
    fixture.Press(B, true);
    assert(screen.backlight_on && screen.requests == 1);

    screen.result = 0;
    App::SetScreenDisabled(true);
    assert(App::IsScreenDisabled() && !screen.backlight_on);
    fixture.Press(B, false);
    assert(!App::IsScreenDisabled() && screen.backlight_on && screen.requests == 3);
    fixture.Press(B, true);
    assert(screen.requests == 3);
}

void CheckModeTransition(AppletOperationMode origin, AppletOperationMode target) {
    Fixture fixture{origin};
    App::SetScreenDisabled(true);
    assert(App::IsScreenDisabled());
    screen.mode = target;
    fixture.Press(B, true);
    assert(!App::IsScreenDisabled());
    fixture.Press(X, true);
}

void CheckRelease(AppletOperationMode mode) {
    Fixture fixture{mode};
    const bool handheld = mode == AppletOperationMode_Handheld;
    assert(App::AcquireScreenToggle());
    App::SetScreenDisabled(true);
    assert(App::IsScreenDisabled() && screen.requests == (handheld ? 1 : 0));
    App::ReleaseScreenToggle();
    assert(App::IsScreenToggleEnabled() && App::IsScreenDisabled());
    App::ReleaseScreenToggle();
    assert(!App::IsScreenToggleEnabled() && !App::IsScreenDisabled());
    App::SetScreenDisabled(true);
    assert(!App::IsScreenDisabled());
    assert(screen.backlight_on == handheld && screen.requests == (handheld ? 2 : 0));
    fixture.Press(B, true);
}

} // namespace

int main() {
    CheckDockedButtons();
    CheckHandheldButtons();
    CheckModeTransition(AppletOperationMode_Handheld, AppletOperationMode_Console);
    CheckModeTransition(AppletOperationMode_Console, AppletOperationMode_Handheld);
    CheckRelease(AppletOperationMode_Console);
    CheckRelease(AppletOperationMode_Handheld);
    std::cout << "Shop docked and handheld screen toggle, wake and controller fixtures passed\n";
}
