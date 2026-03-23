#pragma once
#include "core/Config.h"
#include "liquid_glass/GlassPipeline.h"
#include "liquid_glass/ShapeSDF.h"
#include <vector>

struct GLFWwindow;

namespace lg {

struct MouseState {
    double x = 0.0;
    double y = 0.0;
    bool left_down = false;
    bool dragging = false;
};

struct FrameInput {
    MouseState mouse{};
    bool toggle_overlay = false;
    bool toggle_pause = false;
    bool restart_playback = false;
    bool toggle_loop = false;
    bool seek_backward = false;
    bool seek_forward = false;
    bool view_final = false;
    bool view_mask = false;
    bool view_sdf = false;
    bool view_normal = false;
    bool view_disp = false;
    bool profile_prev = false;
    bool profile_next = false;
};

FrameInput capture_input_from_window(GLFWwindow* window);

class InteractiveControl {
public:
    virtual ~InteractiveControl() = default;
    virtual bool hit(float x, float y) const = 0;
    virtual bool begin_pointer(float x, float y) = 0;
    virtual void update_pointer(float x, float y) = 0;
    virtual void end_pointer(float x, float y) = 0;
    virtual void set_hovered(bool hovered) = 0;
};

enum class DebugViewMode {
    Final,
    Mask,
    Sdf,
    Normal,
    Disp
};

struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(double px, double py) const {
        return px >= x && py >= y && px < (x + width) && py < (y + height);
    }
};

struct DebugSliderView {
    const char* label = "";
    RectI bounds{};
    float min_value = 0.0f;
    float max_value = 1.0f;
    float value = 0.0f;
    bool active = false;
};

struct DebugButtonView {
    DebugViewMode mode = DebugViewMode::Final;
    RectI bounds{};
    bool active = false;
};

struct PlaybackUiState {
    bool available = false;
    bool paused = false;
    bool loop = true;
    float progress = 0.0f;
    float buffered_progress = 0.0f;
};

struct PlaybackCommand {
    bool toggle_pause = false;
    bool restart = false;
    bool toggle_loop = false;
    int seek_direction = 0;
    bool seek_to_ratio = false;
    float seek_ratio = 0.0f;
};

struct PlaybackTimelineView {
    RectI bounds{};
    float progress = 0.0f;
    float buffered_progress = 0.0f;
    bool paused = false;
    bool loop = true;
    bool active = false;
    bool available = false;
};

class InteractionController {
public:
    void initialize(int viewport_width, int viewport_height);
    void set_viewport(int width, int height);
    void update(
        const FrameInput& input,
        const std::vector<InteractiveControl*>& controls,
        TabBarConfig& tabbar,
        GlassConfig& glass,
        const PlaybackUiState* playback = nullptr,
        PlaybackCommand* playback_command = nullptr);

    DebugViewMode debug_view() const { return debug_view_; }
    bool overlay_visible() const { return overlay_visible_; }
    const std::vector<DebugSliderView>& sliders() const { return sliders_; }
    const std::vector<DebugButtonView>& debug_buttons() const { return debug_buttons_; }
    const PlaybackTimelineView& playback_timeline() const { return playback_timeline_; }

private:
    enum class SliderTarget {
        TabWidth,
        TabHeight,
        CornerRadius,
        Thickness,
        Refraction
    };

    struct SliderModel {
        SliderTarget target = SliderTarget::TabWidth;
        DebugSliderView view{};
    };

    void rebuild_layout();
    void refresh_views(const TabBarConfig& tabbar, const GlassConfig& glass, const PlaybackUiState* playback);
    void apply_slider_from_pointer(size_t slider_index, double pointer_x, TabBarConfig& tabbar, GlassConfig& glass);
    float slider_value(SliderTarget target, const TabBarConfig& tabbar, const GlassConfig& glass) const;
    void set_slider_value(SliderTarget target, float value, TabBarConfig& tabbar, GlassConfig& glass) const;
    bool point_hits_overlay(double x, double y) const;
    void apply_playback_seek(double pointer_x, PlaybackCommand* command) const;

    int viewport_width_ = 0;
    int viewport_height_ = 0;
    bool overlay_visible_ = true;
    DebugViewMode debug_view_ = DebugViewMode::Final;
    FrameInput previous_{};
    InteractiveControl* active_control_ = nullptr;
    int active_slider_ = -1;
    std::vector<SliderModel> slider_models_{};
    std::vector<DebugSliderView> sliders_{};
    std::vector<DebugButtonView> debug_buttons_{};
    PlaybackTimelineView playback_timeline_{};
    bool active_playback_seek_ = false;
};

GlassBufferId buffer_for_debug_view(DebugViewMode mode);
const char* debug_view_name(DebugViewMode mode);

}
