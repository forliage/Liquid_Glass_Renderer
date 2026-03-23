#include "liquid_glass/ShapeSDF.h"
#include <algorithm>
#include <cmath>
namespace lg {

namespace {

float capsule_skeleton_distance(float x, float y, const GlassParams& p) {
    const float half_width = std::max(0.5f * p.tab_w - p.radius, 0.0f);
    const float clamped_x = std::clamp(x - p.cx, -half_width, half_width);
    const float dx = x - (p.cx + clamped_x);
    const float dy = y - p.cy;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

float round_rect_sdf(float x,float y,const GlassParams& p) {
    float px=std::fabs(x-p.cx)-(p.tab_w*0.5f-p.radius);
    float py=std::fabs(y-p.cy)-(p.tab_h*0.5f-p.radius);
    float qx=std::max(px,0.0f), qy=std::max(py,0.0f);
    return std::sqrt(qx*qx+qy*qy)+std::min(std::max(px,py),0.0f)-p.radius;
}

ThicknessProfileKind thickness_profile_kind_from_string(const std::string& name) {
    if (name == "parabola") {
        return ThicknessProfileKind::Parabola;
    }
    if (name == "super_quadric" || name == "superquadric") {
        return ThicknessProfileKind::SuperQuadric;
    }
    return ThicknessProfileKind::EdgeRoll;
}

const char* thickness_profile_name(ThicknessProfileKind kind) {
    switch (kind) {
        case ThicknessProfileKind::Parabola: return "parabola";
        case ThicknessProfileKind::SuperQuadric: return "super_quadric";
        case ThicknessProfileKind::EdgeRoll: return "edge_roll";
    }
    return "edge_roll";
}

ThicknessProfileKind cycle_thickness_profile(ThicknessProfileKind current, int direction) {
    constexpr int kProfileCount = 3;
    int index = static_cast<int>(current);
    index = (index + direction) % kProfileCount;
    if (index < 0) {
        index += kProfileCount;
    }
    return static_cast<ThicknessProfileKind>(index);
}

ShapeSample sample_tabbar_shape(float x, float y, const GlassParams& p) {
    ShapeSample sample{};
    sample.sdf = round_rect_sdf(x, y, p);
    sample.skeleton_distance = capsule_skeleton_distance(x, y, p);
    sample.local_radius = std::max(0.5f * p.tab_h, 1.0f);
    sample.normalized_radius = sample.skeleton_distance / sample.local_radius;
    return sample;
}

float evaluate_thickness_profile(ThicknessProfileKind kind, float normalized_radius, const GlassParams& p) {
    const float r = std::clamp(normalized_radius, 0.0f, 1.6f);
    const float parabola = std::max(0.0f, 1.0f - r * r);
    const float super_quad = std::pow(std::max(0.0f, 1.0f - std::pow(r, p.profile_p)), p.profile_q);
    const float edge_roll = parabola + p.edge_boost * std::exp(-((1.0f - r) * (1.0f - r)) / 0.02f);

    switch (kind) {
        case ThicknessProfileKind::Parabola: return parabola;
        case ThicknessProfileKind::SuperQuadric: return super_quad;
        case ThicknessProfileKind::EdgeRoll: return std::max(0.0f, edge_roll);
    }
    return parabola;
}

float evaluate_edge_weight(float sdf, float sigma) {
    const float safe_sigma = std::max(1e-3f, sigma);
    return std::exp(-(sdf * sdf) / (2.0f * safe_sigma * safe_sigma));
}

DisplacementGuardResult apply_displacement_guard(float dx, float dy, const GlassParams& p) {
    DisplacementGuardResult guarded{};
    const float limit = std::max(1e-3f, p.displacement_limit);
    const float magnitude = std::sqrt(dx * dx + dy * dy);
    float scale = 1.0f;
    if (magnitude > limit) {
        scale = limit / magnitude;
    }
    guarded.dx = dx * scale;
    guarded.dy = dy * scale;
    guarded.risk = std::clamp(magnitude / limit, 0.0f, 1.0f);
    return guarded;
}

}
