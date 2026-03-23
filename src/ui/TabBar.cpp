#include "ui/TabBar.h"
#include <cmath>
namespace lg {
static float sdf_round_rect(float x,float y,float cx,float cy,float w,float h,float r){
    float px=std::fabs(x-cx)-(w*0.5f-r), py=std::fabs(y-cy)-(h*0.5f-r);
    float qx=std::max(px,0.0f), qy=std::max(py,0.0f);
    return std::sqrt(qx*qx+qy*qy)+std::min(std::max(px,py),0.0f)-r;
}
bool TabBar::hit(float x,float y) const { return sdf_round_rect(x,y,cfg_.cx,cfg_.cy,cfg_.width,cfg_.height,cfg_.corner_radius)<=0.f; }
bool TabBar::begin_pointer(float x,float y){
    if (!hit(x, y)) {
        return false;
    }
    pressed_ = true;
    press_x_ = x;
    press_y_ = y;
    begin_drag(x, y);
    return true;
}
void TabBar::update_pointer(float x,float y){ drag_to(x, y); }
void TabBar::end_pointer(float x,float y){
    const float dx = x - press_x_;
    const float dy = y - press_y_;
    const float movement_sq = dx * dx + dy * dy;
    if (hit(x, y) && movement_sq <= 36.0f) {
        selected_ = !selected_;
    }
    pressed_ = false;
    end_drag();
}
void TabBar::begin_drag(float x,float y){ dragging_=true; drag_dx_=x-cfg_.cx; drag_dy_=y-cfg_.cy; }
void TabBar::drag_to(float x,float y){ if(dragging_){ cfg_.cx=x-drag_dx_; cfg_.cy=y-drag_dy_; } }
void TabBar::end_drag(){ dragging_=false; }
}
