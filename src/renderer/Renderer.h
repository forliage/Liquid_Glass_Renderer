#pragma once
#include "core/Timer.h"
#include "liquid_glass/GlassPipeline.h"
#include "ui/TabBar.h"
#include "ui/Interaction.h"
#include "core/Config.h"
#include "experiments/Benchmark.h"
#include "io/FrameSource.h"
#include <memory>
namespace lg {
class Renderer {
public:
    bool initialize(const AppConfig& config);
    void set_source(std::shared_ptr<IFrameSource> source);
    bool tick(const FrameInput& input);
    bool render_frame(
        const CpuFrame& source_frame,
        const FrameInput& input,
        int frame_index,
        const PlaybackUiState* playback = nullptr,
        PlaybackCommand* playback_command = nullptr);
    TabBar& tabbar() { return *tabbar_; }
    const CpuFrame& final_frame() const { return final_frame_; }
    const CpuFrame& presented_frame() const { return presented_; }
    const InteractionController& interaction() const { return interaction_; }
    double last_cpu_ms() const { return last_cpu_ms_; }
    double last_gpu_ms() const { return last_gpu_ms_; }
    const FrameTiming& last_timing() const { return last_timing_; }
    bool outputs_saved() const { return outputs_saved_; }
    bool can_direct_present() const { return direct_present_ready_ && pipeline_.display_buffer(); }
    const GpuImage* direct_present_buffer() const { return pipeline_.display_buffer(); }
    void mark_direct_present_result(bool success);
private:
    bool save_image_outputs_once();
    bool save_video_outputs_for_frame(int frame_index);
    CpuFrame build_view_frame();
    void build_display_overlay();
    GlassParams build_params() const;

    AppConfig config_{};
    std::shared_ptr<IFrameSource> source_{};
    std::unique_ptr<TabBar> tabbar_{};
    InteractionController interaction_{};
    GlassPipeline pipeline_{};
    CpuFrame current_{};
    CpuFrame prepared_{};
    CpuFrame final_frame_{};
    CpuFrame viewed_{};
    CpuFrame presented_{};
    bool outputs_saved_ = false;
    double last_cpu_ms_ = 0.0;
    double last_gpu_ms_ = 0.0;
    FrameTiming last_timing_{};
    CpuTimer image_capture_timer_{};
    bool image_capture_started_ = false;
    bool image_measurement_started_ = false;
    int image_frames_seen_ = 0;
    std::vector<BenchmarkSample> image_samples_{};
    bool direct_present_ready_ = false;
    bool direct_present_supported_ = true;
    bool direct_present_confirmed_ = false;
};
}
