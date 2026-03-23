#pragma once
#include "experiments/Benchmark.h"
#include "experiments/Metrics.h"
#include <filesystem>
#include <string>
#include <vector>

namespace lg {

struct ExperimentRecord {
    std::string group_key;
    std::string label;
    std::string benchmark_path;
    std::string input_mode;
    std::string input_path;
    std::string performance_mode;
    std::string ablation_variant = "standard";
    std::string output_artifact;
    std::string disp_debug_artifact;
    std::string mask_debug_artifact;
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
    size_t frame_count = 0;
    double avg_cpu_ms = 0.0;
    double avg_gpu_ms = 0.0;
    double avg_fps = 0.0;
    double cold_start_cpu_ms = 0.0;
    double cold_start_gpu_ms = 0.0;
    QualityMetricsSummary quality{};
    double quality_performance_score = 0.0;
    std::vector<PassMetricsSummary> passes{};
};

class DatasetRunner {
public:
    std::vector<ExperimentRecord> collect(const std::filesystem::path& root) const;
    bool write_json(const std::filesystem::path& path, const std::vector<ExperimentRecord>& records) const;
};

}
