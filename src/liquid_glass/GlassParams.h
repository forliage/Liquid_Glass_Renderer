#pragma once
#include <cstddef>
namespace lg {
enum class GpuPixelFormat {
    U8,
    F32
};

struct GlassParams {
    int width=0, height=0;
    float cx=0, cy=0, tab_w=0, tab_h=0, radius=0;
    float h0=22.f, refraction_strength=18.f, center_strength=6.f, edge_sigma=24.f;
    float fresnel_strength=1.f, specular_strength=0.8f, temporal_alpha=0.18f;
    float edge_boost=0.35f, edge_glow_strength=0.14f, displacement_limit=24.f, jacobian_guard=0.82f;
    float foreground_protect=0.25f, legibility_boost=0.20f, specular_power=48.f;
    float profile_p=2.6f, profile_q=1.8f;
    int thickness_profile=2;
    float absorb[3]{0.03f,0.03f,0.025f};
    float tint[3]{0.18f,0.18f,0.20f};
    float target_contrast=0.45f;
    int tile_size=16;
    int legibility_enabled=1;
    int refraction_enabled=1;
    int reflection_enabled=1;
    int specular_enabled=1;
    int temporal_enabled=1;
    int blur_background_only=0;
    int specular_step=1;
    int legibility_step=1;
    int use_cuda_graph=0;
};
struct GpuImage { void* ptr=nullptr; int width=0,height=0,channels=4; size_t pitch=0; GpuPixelFormat format=GpuPixelFormat::U8; };
}
