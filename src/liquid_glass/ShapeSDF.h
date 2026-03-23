#pragma once
#include "liquid_glass/GlassParams.h"
#include <string>
namespace lg {
#if defined(__CUDACC__)
#define LG_HOST_DEVICE_INLINE __host__ __device__ inline
#else
#define LG_HOST_DEVICE_INLINE inline
#endif

enum class ThicknessProfileKind {
    Parabola = 0,
    SuperQuadric = 1,
    EdgeRoll = 2
};

struct ShapeSample {
    float sdf = 0.0f;
    float skeleton_distance = 0.0f;
    float local_radius = 1.0f;
    float normalized_radius = 0.0f;
};

struct DisplacementGuardResult {
    float dx = 0.0f;
    float dy = 0.0f;
    float risk = 0.0f;
};

struct LegibilityResponse {
    float adapt_scale = 1.0f;
    float tint_mix = 0.0f;
    float protect = 0.0f;
    float variance = 0.0f;
};

LG_HOST_DEVICE_INLINE float clamp_legibility_scalar(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

LG_HOST_DEVICE_INLINE float abs_legibility_scalar(float value) {
    return value < 0.0f ? -value : value;
}

LG_HOST_DEVICE_INLINE LegibilityResponse compute_legibility_response(float mean_luma, float variance, const GlassParams& p) {
    LegibilityResponse response{};
    if (!p.legibility_enabled) {
        return response;
    }
    const float centered_luma = clamp_legibility_scalar(mean_luma, 0.0f, 1.0f);
    response.variance = variance < 0.0f ? 0.0f : variance;
    response.adapt_scale = centered_luma < 0.42f ? 1.0f + p.legibility_boost : 1.0f - 0.5f * p.legibility_boost;
    response.tint_mix = clamp_legibility_scalar(
        0.08f + response.variance * 1.6f + ((p.target_contrast - abs_legibility_scalar(0.5f - centered_luma)) > 0.0f
            ? (p.target_contrast - abs_legibility_scalar(0.5f - centered_luma)) * 0.25f
            : 0.0f),
        0.05f,
        0.55f);
    response.protect = clamp_legibility_scalar(p.foreground_protect + response.variance * 0.5f, 0.0f, 0.85f);
    return response;
}

LG_HOST_DEVICE_INLINE LegibilityResponse lerp_legibility_response(
    const LegibilityResponse& a,
    const LegibilityResponse& b,
    float t) {
    const float u = clamp_legibility_scalar(t, 0.0f, 1.0f);
    LegibilityResponse out{};
    out.adapt_scale = a.adapt_scale + (b.adapt_scale - a.adapt_scale) * u;
    out.tint_mix = a.tint_mix + (b.tint_mix - a.tint_mix) * u;
    out.protect = a.protect + (b.protect - a.protect) * u;
    out.variance = a.variance + (b.variance - a.variance) * u;
    return out;
}

LG_HOST_DEVICE_INLINE LegibilityResponse bilerp_legibility_response(
    const LegibilityResponse& c00,
    const LegibilityResponse& c10,
    const LegibilityResponse& c01,
    const LegibilityResponse& c11,
    float tx,
    float ty) {
    const LegibilityResponse top = lerp_legibility_response(c00, c10, tx);
    const LegibilityResponse bottom = lerp_legibility_response(c01, c11, tx);
    return lerp_legibility_response(top, bottom, ty);
}

float round_rect_sdf(float x,float y,const GlassParams& p);
ThicknessProfileKind thickness_profile_kind_from_string(const std::string& name);
const char* thickness_profile_name(ThicknessProfileKind kind);
ThicknessProfileKind cycle_thickness_profile(ThicknessProfileKind current, int direction);
ShapeSample sample_tabbar_shape(float x, float y, const GlassParams& p);
float evaluate_thickness_profile(ThicknessProfileKind kind, float normalized_radius, const GlassParams& p);
float evaluate_edge_weight(float sdf, float sigma);
DisplacementGuardResult apply_displacement_guard(float dx, float dy, const GlassParams& p);
}
