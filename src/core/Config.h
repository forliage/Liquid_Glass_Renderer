#pragma once
#include <string>
#include <vector>
namespace lg {
struct WindowConfig { int width=1600; int height=900; bool vsync=true; };
struct InputConfig {
    std::string mode="image";
    std::string path;
    bool loop=true;
    std::string realtime_source="file";
    int device_index=0;
};
struct TabBarConfig { float cx=800, cy=740, width=980, height=180, corner_radius=90; };
struct GlassConfig {
    std::string thickness_profile="edge_roll";
    float h0=22.f, refraction_strength=18.f, center_strength=6.f, edge_sigma=24.f;
    float fresnel_strength=1.f, specular_strength=0.8f, temporal_alpha=0.18f;
    float edge_boost=0.35f, edge_glow_strength=0.14f, displacement_limit=24.f, jacobian_guard=0.82f;
    float foreground_protect=0.25f, legibility_boost=0.20f, specular_power=48.f;
    float profile_p=2.6f, profile_q=1.8f;
    float absorb[3]{0.03f,0.03f,0.025f};
    float tint[3]{0.18f,0.18f,0.20f};
};
struct LegibilityConfig { bool enabled=true; float target_contrast=0.45f; int tile_size=16; };
struct AblationConfig {
    std::string variant="standard";
    bool refraction=true;
    bool reflection=true;
    bool specular=true;
    bool legibility=true;
    bool temporal=true;
    bool blur_only=false;
};
struct PerformanceConfig {
    std::string mode="full";
    bool half_res_specular=false;
    bool cuda_graph=false;
    bool opengl_interop=true;
    int specular_downsample=1;
    int legibility_downsample=1;
    int warmup_frames=2;
    int benchmark_frames=12;
};
struct OutputConfig {
    bool save_frames=false;
    bool save_debug_buffers=false;
    bool headless=false;
    std::string out_dir="results";
};
struct AppConfig {
    WindowConfig window; InputConfig input; TabBarConfig tabbar; GlassConfig glass;
    LegibilityConfig legibility; AblationConfig ablation; PerformanceConfig performance; OutputConfig output;
};
class ConfigLoader {
public:
    static AppConfig load(const std::string& path);
    static void save_template(const std::string& path);
    static std::string describe(const AppConfig& cfg);
    static std::vector<std::string> output_directories(const AppConfig& cfg);
    static void ensure_output_directories(const AppConfig& cfg);
};
}
