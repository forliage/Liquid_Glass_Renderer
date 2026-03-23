#pragma once
#include "experiments/Metrics.h"
#include <filesystem>
#include <string>
#include <vector>

namespace lg {

struct BenchmarkConfig {
    std::string label;
    std::string input_mode;
    std::string input_path;
    std::string performance_mode;
    std::string ablation_variant = "standard";
    int width = 0;
    int height = 0;
    bool half_res_specular = false;
    bool cuda_graph = false;
    bool ablation_refraction = true;
    bool ablation_reflection = true;
    bool ablation_specular = true;
    bool ablation_legibility = true;
    bool ablation_temporal = true;
    bool ablation_blur_only = false;
    int specular_downsample = 1;
    int legibility_downsample = 1;
    int warmup_frames = 0;
    int benchmark_frames = 0;
};

struct BenchmarkSample {
    int frame_index = 0;
    double pts = 0.0;
    FrameTiming timing{};
    bool warmup = false;
};

struct BenchmarkResult {
    BenchmarkConfig config{};
    MetricsSummary summary{};
    std::vector<BenchmarkSample> samples{};
    std::string output_artifact{};
    double input_fps = 0.0;
    double total_wall_ms = 0.0;
    double measured_wall_ms = 0.0;
    size_t warmup_frame_count = 0;
    size_t measured_frame_count = 0;
    FrameTiming cold_start{};
};

class Benchmark {
public:
    BenchmarkResult build(
        const BenchmarkConfig& config,
        const std::vector<BenchmarkSample>& samples,
        double total_wall_ms,
        double measured_wall_ms,
        double input_fps = 0.0,
        const std::string& output_artifact = {}) const;
    bool read_json(const std::filesystem::path& path, BenchmarkResult& result, std::string* error = nullptr) const;
    bool write_json(const std::filesystem::path& path, const BenchmarkResult& result) const;
};

}
