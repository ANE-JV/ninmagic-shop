#pragma once

#include "ui/widget.hpp"
#include <string_view>

namespace sphaira::ui {

// todo: remove fixed values from appstore
struct ScrollableText final : Widget {
    ScrollableText(const std::string& text, float x, float y, float y_clip, float w, float font_size);
    void Draw(NVGcontext* vg, Theme* theme) override;

    std::string m_text;
    // static constexpr float m_y_off_base = 374;
    // float m_y_off = m_y_off_base;
    // static constexpr float m_clip_y = 250.0F;

    static constexpr inline float m_step = 30;
    const float m_font_size;
    const float m_y_off_base;
    const float m_clip_y;
    const float m_end_w;

    float m_y_off{};
    int m_index{};
    float m_bounds[4]{};
};

class MessageOfDayBox final : public Widget {
public:
    explicit MessageOfDayBox(std::string message);
    MessageOfDayBox(const MessageOfDayBox&) = delete;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void Scroll(int rows);
    auto LastTopRow() const -> int;

    const std::string m_message;
    std::vector<std::string_view> m_lines;
    int m_scroll_row{};
    int m_drag_rows{};
};

} // namespace sphaira::ui
