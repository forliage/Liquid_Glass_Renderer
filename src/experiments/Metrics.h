#pragma once
#include "core/Timer.h"
#include "io/FrameSource.h"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace lg {

struct PassMetricsSummary {
    std::string name;
    double avg_cpu_ms = 0.0;
    double avg_gpu_ms = 0.0;
};

struct MetricsSummary {
    double fps = 0.0;
    double frame_ms = 0.0;
    double gpu_frame_ms = 0.0;
    double flip_risk = 0.0;
    double wall_ms = 0.0;
    size_t frame_count = 0;
    std::vector<PassMetricsSummary> passes;
};

struct QualityMetricsSummary {
    bool fold_over_available = false;
    bool contrast_available = false;
    double fold_over_mean = 0.0;
    double fold_over_peak = 0.0;
    double contrast_preservation = 0.0;
};

MetricsSummary summarize_metrics(const std::vector<FrameTiming>& frames, double wall_ms);
double compute_fold_over_mean(const CpuFrame& disp, const CpuFrame* mask = nullptr);
double compute_fold_over_peak(const CpuFrame& disp, const CpuFrame* mask = nullptr);
double compute_local_contrast_preservation(const CpuFrame& input, const CpuFrame& output, const CpuFrame* mask = nullptr);
QualityMetricsSummary analyze_quality_metrics(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const std::filesystem::path& disp_path,
    const std::filesystem::path& mask_path = {});

}
