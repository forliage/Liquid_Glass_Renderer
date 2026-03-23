#pragma once
#include "core/Config.h"
#include "ui/Interaction.h"
namespace lg {
class TabBar : public InteractiveControl {
public:
    explicit TabBar(const TabBarConfig& cfg): cfg_(cfg) {}
    const TabBarConfig& config() const { return cfg_; }
    TabBarConfig& mutable_config() { return cfg_; }
    bool hit(float x, float y) const override;
    bool begin_pointer(float x, float y) override;
    void update_pointer(float x, float y) override;
    void end_pointer(float x, float y) override;
    void set_hovered(bool hovered) override { hovered_ = hovered; }

    void begin_drag(float x, float y);
    void drag_to(float x, float y);
    void end_drag();

    bool hovered() const { return hovered_; }
    bool pressed() const { return pressed_; }
    bool selected() const { return selected_; }
private:
    TabBarConfig cfg_{};
    bool dragging_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool selected_ = false;
    float drag_dx_ = 0.f;
    float drag_dy_ = 0.f;
    float press_x_ = 0.f;
    float press_y_ = 0.f;
};
}
