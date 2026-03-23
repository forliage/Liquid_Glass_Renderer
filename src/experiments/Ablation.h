#pragma once
#include "experiments/DatasetRunner.h"
#include <filesystem>
#include <string>
#include <vector>

namespace lg {

struct AblationComparison {
    std::string group_key;
    std::string baseline_label;
    std::string baseline_mode;
    std::string baseline_ablation_variant;
    std::string variant_label;
    std::string variant_mode;
    std::string variant_ablation_variant;
    double fps_delta = 0.0;
    double fps_ratio = 0.0;
    double gpu_ms_delta = 0.0;
    double gpu_speedup = 0.0;
    double contrast_delta = 0.0;
    double fold_over_delta = 0.0;
    double quality_performance_delta = 0.0;
};

struct AblationReport {
    size_t experiment_count = 0;
    std::vector<ExperimentRecord> experiments{};
    std::vector<AblationComparison> comparisons{};
};

class Ablation {
public:
    AblationReport build(const std::vector<ExperimentRecord>& records) const;
    bool write_json(const std::filesystem::path& path, const AblationReport& report) const;
};

}
