#include "ui/Interaction.h"
#include <GLFW/glfw3.h>
#include <algorithm>

namespace lg {

namespace {

constexpr float kMinTabSize = 64.0f;
constexpr float kMinCornerRadius = 4.0f;

bool rising_edge(bool current, bool previous) {
    return current && !previous;
}

}  // namespace

FrameInput capture_input_from_window(GLFWwindow* window) {
    FrameInput input{};
    if (!window) {
        return input;
    }

    glfwGetCursorPos(window, &input.mouse.x, &input.mouse.y);
    input.mouse.left_down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    input.toggle_overlay = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
    input.toggle_pause = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    input.restart_playback = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    input.toggle_loop = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
    input.seek_backward = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;
    input.seek_forward = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
    input.view_final = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
    input.view_mask = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
    input.view_sdf = glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS;
    input.view_normal = glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS;
    input.view_disp = glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS;
    input.profile_prev = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
    input.profile_next = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    return input;
}

void InteractionController::initialize(int viewport_width, int viewport_height) {
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;

    slider_models_.clear();
    slider_models_.push_back({SliderTarget::TabWidth, {"tab_width", {}, 160.0f, 1400.0f, 0.0f, false}});
    slider_models_.push_back({SliderTarget::TabHeight, {"tab_height", {}, 48.0f, 320.0f, 0.0f, false}});
    slider_models_.push_back({SliderTarget::CornerRadius, {"corner_radius", {}, 4.0f, 160.0f, 0.0f, false}});
    slider_models_.push_back({SliderTarget::Thickness, {"thickness", {}, 0.0f, 48.0f, 0.0f, false}});
    slider_models_.push_back({SliderTarget::Refraction, {"refraction", {}, 0.0f, 32.0f, 0.0f, false}});

    debug_buttons_.clear();
    debug_buttons_.push_back({DebugViewMode::Final, {}, true});
    debug_buttons_.push_back({DebugViewMode::Mask, {}, false});
    debug_buttons_.push_back({DebugViewMode::Sdf, {}, false});
    debug_buttons_.push_back({DebugViewMode::Normal, {}, false});
    debug_buttons_.push_back({DebugViewMode::Disp, {}, false});

    rebuild_layout();
}

void InteractionController::set_viewport(int width, int height) {
    viewport_width_ = width;
    viewport_height_ = height;
    rebuild_layout();
}

void InteractionController::rebuild_layout() {
    sliders_.clear();
    const int origin_x = 20;
    const int origin_y = 20;
    const int slider_width = std::min(260, std::max(160, viewport_width_ / 4));
    const int slider_height = 18;
    const int slider_gap = 12;

    for (size_t i = 0; i < slider_models_.size(); ++i) {
        slider_models_[i].view.bounds = {origin_x, origin_y + static_cast<int>(i) * (slider_height + slider_gap), slider_width, slider_height};
        sliders_.push_back(slider_models_[i].view);
    }

    const int button_y = origin_y + static_cast<int>(slider_models_.size()) * (slider_height + slider_gap) + 10;
    const int button_size = 20;
    const int button_gap = 8;
    for (size_t i = 0; i < debug_buttons_.size(); ++i) {
        debug_buttons_[i].bounds = {origin_x + static_cast<int>(i) * (button_size + button_gap), button_y, button_size, button_size};
    }

    playback_timeline_.bounds = {
        20,
        std::max(button_y + button_size + 18, viewport_height_ - 38),
        std::max(120, viewport_width_ - 40),
        14
    };
}

float InteractionController::slider_value(SliderTarget target, const TabBarConfig& tabbar, const GlassConfig& glass) const {
    switch (target) {
        case SliderTarget::TabWidth: return tabbar.width;
        case SliderTarget::TabHeight: return tabbar.height;
        case SliderTarget::CornerRadius: return tabbar.corner_radius;
        case SliderTarget::Thickness: return glass.h0;
        case SliderTarget::Refraction: return glass.refraction_strength;
    }
    return 0.0f;
}

void InteractionController::set_slider_value(SliderTarget target, float value, TabBarConfig& tabbar, GlassConfig& glass) const {
    switch (target) {
        case SliderTarget::TabWidth:
            tabbar.width = std::max(kMinTabSize, value);
            tabbar.corner_radius = std::min(tabbar.corner_radius, 0.5f * std::min(tabbar.width, tabbar.height));
            break;
        case SliderTarget::TabHeight:
            tabbar.height = std::max(kMinTabSize, value);
            tabbar.corner_radius = std::min(tabbar.corner_radius, 0.5f * std::min(tabbar.width, tabbar.height));
            break;
        case SliderTarget::CornerRadius:
            tabbar.corner_radius = std::clamp(value, kMinCornerRadius, 0.5f * std::min(tabbar.width, tabbar.height));
            break;
        case SliderTarget::Thickness:
            glass.h0 = std::max(0.0f, value);
            break;
        case SliderTarget::Refraction:
            glass.refraction_strength = std::max(0.0f, value);
            break;
    }
}

void InteractionController::refresh_views(const TabBarConfig& tabbar, const GlassConfig& glass, const PlaybackUiState* playback) {
    sliders_.clear();
    for (size_t i = 0; i < slider_models_.size(); ++i) {
        SliderModel& model = slider_models_[i];
        model.view.value = slider_value(model.target, tabbar, glass);
        model.view.active = static_cast<int>(i) == active_slider_;
        sliders_.push_back(model.view);
    }
    for (DebugButtonView& button : debug_buttons_) {
        button.active = button.mode == debug_view_;
    }
    if (playback && playback->available) {
        playback_timeline_.available = true;
        playback_timeline_.progress = std::clamp(playback->progress, 0.0f, 1.0f);
        playback_timeline_.buffered_progress = std::clamp(playback->buffered_progress, playback_timeline_.progress, 1.0f);
        playback_timeline_.paused = playback->paused;
        playback_timeline_.loop = playback->loop;
        playback_timeline_.active = active_playback_seek_;
    } else {
        playback_timeline_.available = false;
        playback_timeline_.progress = 0.0f;
        playback_timeline_.buffered_progress = 0.0f;
        playback_timeline_.paused = false;
        playback_timeline_.loop = true;
        playback_timeline_.active = false;
    }
}

void InteractionController::apply_slider_from_pointer(size_t slider_index, double pointer_x, TabBarConfig& tabbar, GlassConfig& glass) {
    if (slider_index >= slider_models_.size()) {
        return;
    }
    const DebugSliderView& view = slider_models_[slider_index].view;
    const float t = std::clamp(static_cast<float>((pointer_x - view.bounds.x) / std::max(1, view.bounds.width - 1)), 0.0f, 1.0f);
    const float value = view.min_value + t * (view.max_value - view.min_value);
    set_slider_value(slider_models_[slider_index].target, value, tabbar, glass);
}

bool InteractionController::point_hits_overlay(double x, double y) const {
    for (const DebugSliderView& slider : sliders_) {
        if (slider.bounds.contains(x, y)) {
            return true;
        }
    }
    for (const DebugButtonView& button : debug_buttons_) {
        if (button.bounds.contains(x, y)) {
            return true;
        }
    }
    if (playback_timeline_.available && playback_timeline_.bounds.contains(x, y)) {
        return true;
    }
    return false;
}

void InteractionController::apply_playback_seek(double pointer_x, PlaybackCommand* command) const {
    if (!command || !playback_timeline_.available) {
        return;
    }
    const float t = std::clamp(
        static_cast<float>((pointer_x - playback_timeline_.bounds.x) / std::max(1, playback_timeline_.bounds.width - 1)),
        0.0f,
        1.0f);
    command->seek_to_ratio = true;
    command->seek_ratio = t;
}

void InteractionController::update(
    const FrameInput& input,
    const std::vector<InteractiveControl*>& controls,
    TabBarConfig& tabbar,
    GlassConfig& glass,
    const PlaybackUiState* playback,
    PlaybackCommand* playback_command) {
    if (playback_command) {
        *playback_command = {};
    }
    if (rising_edge(input.toggle_overlay, previous_.toggle_overlay)) {
        overlay_visible_ = !overlay_visible_;
    }
    if (playback_command) {
        if (rising_edge(input.toggle_pause, previous_.toggle_pause)) playback_command->toggle_pause = true;
        if (rising_edge(input.restart_playback, previous_.restart_playback)) playback_command->restart = true;
        if (rising_edge(input.toggle_loop, previous_.toggle_loop)) playback_command->toggle_loop = true;
        if (rising_edge(input.seek_backward, previous_.seek_backward)) playback_command->seek_direction = -1;
        if (rising_edge(input.seek_forward, previous_.seek_forward)) playback_command->seek_direction = 1;
    }
    if (rising_edge(input.view_final, previous_.view_final)) debug_view_ = DebugViewMode::Final;
    if (rising_edge(input.view_mask, previous_.view_mask)) debug_view_ = DebugViewMode::Mask;
    if (rising_edge(input.view_sdf, previous_.view_sdf)) debug_view_ = DebugViewMode::Sdf;
    if (rising_edge(input.view_normal, previous_.view_normal)) debug_view_ = DebugViewMode::Normal;
    if (rising_edge(input.view_disp, previous_.view_disp)) debug_view_ = DebugViewMode::Disp;
    if (rising_edge(input.profile_prev, previous_.profile_prev)) {
        const ThicknessProfileKind kind = thickness_profile_kind_from_string(glass.thickness_profile);
        glass.thickness_profile = thickness_profile_name(cycle_thickness_profile(kind, -1));
    }
    if (rising_edge(input.profile_next, previous_.profile_next)) {
        const ThicknessProfileKind kind = thickness_profile_kind_from_string(glass.thickness_profile);
        glass.thickness_profile = thickness_profile_name(cycle_thickness_profile(kind, 1));
    }

    const bool just_pressed = input.mouse.left_down && !previous_.mouse.left_down;
    const bool just_released = !input.mouse.left_down && previous_.mouse.left_down;

    for (InteractiveControl* control : controls) {
        if (control) {
            control->set_hovered(control->hit(static_cast<float>(input.mouse.x), static_cast<float>(input.mouse.y)));
        }
    }

    if (just_pressed && overlay_visible_) {
        for (size_t i = 0; i < debug_buttons_.size(); ++i) {
            if (debug_buttons_[i].bounds.contains(input.mouse.x, input.mouse.y)) {
                debug_view_ = debug_buttons_[i].mode;
                refresh_views(tabbar, glass, playback);
                previous_ = input;
                return;
            }
        }
        if (playback_timeline_.available && playback_timeline_.bounds.contains(input.mouse.x, input.mouse.y)) {
            active_playback_seek_ = true;
            apply_playback_seek(input.mouse.x, playback_command);
            refresh_views(tabbar, glass, playback);
            previous_ = input;
            return;
        }
        for (size_t i = 0; i < slider_models_.size(); ++i) {
            if (slider_models_[i].view.bounds.contains(input.mouse.x, input.mouse.y)) {
                active_slider_ = static_cast<int>(i);
                apply_slider_from_pointer(i, input.mouse.x, tabbar, glass);
                refresh_views(tabbar, glass, playback);
                previous_ = input;
                return;
            }
        }
    }

    if (active_slider_ >= 0) {
        if (input.mouse.left_down) {
            apply_slider_from_pointer(static_cast<size_t>(active_slider_), input.mouse.x, tabbar, glass);
        }
        if (just_released) {
            active_slider_ = -1;
        }
        refresh_views(tabbar, glass, playback);
        previous_ = input;
        return;
    }

    if (active_playback_seek_) {
        if (input.mouse.left_down) {
            apply_playback_seek(input.mouse.x, playback_command);
        }
        if (just_released) {
            active_playback_seek_ = false;
        }
        refresh_views(tabbar, glass, playback);
        previous_ = input;
        return;
    }

    if (just_pressed && (!overlay_visible_ || !point_hits_overlay(input.mouse.x, input.mouse.y))) {
        for (InteractiveControl* control : controls) {
            if (control && control->begin_pointer(static_cast<float>(input.mouse.x), static_cast<float>(input.mouse.y))) {
                active_control_ = control;
                break;
            }
        }
    }

    if (active_control_) {
        if (input.mouse.left_down) {
            active_control_->update_pointer(static_cast<float>(input.mouse.x), static_cast<float>(input.mouse.y));
        }
        if (just_released) {
            active_control_->end_pointer(static_cast<float>(input.mouse.x), static_cast<float>(input.mouse.y));
            active_control_ = nullptr;
        }
    }

    refresh_views(tabbar, glass, playback);
    previous_ = input;
}

GlassBufferId buffer_for_debug_view(DebugViewMode mode) {
    switch (mode) {
        case DebugViewMode::Final: return GlassBufferId::Final;
        case DebugViewMode::Mask: return GlassBufferId::Mask;
        case DebugViewMode::Sdf: return GlassBufferId::Sdf;
        case DebugViewMode::Normal: return GlassBufferId::Normal;
        case DebugViewMode::Disp: return GlassBufferId::Disp;
    }
    return GlassBufferId::Final;
}

const char* debug_view_name(DebugViewMode mode) {
    switch (mode) {
        case DebugViewMode::Final: return "final";
        case DebugViewMode::Mask: return "mask";
        case DebugViewMode::Sdf: return "sdf";
        case DebugViewMode::Normal: return "normal";
        case DebugViewMode::Disp: return "disp";
    }
    return "final";
}

}
