#pragma once
#include "core/Config.h"
#include <filesystem>
#include <string>
#include <vector>

namespace lg {

struct AblationBatchRecord {
    std::string label;
    std::string input_path;
    std::string variant;
    std::string output_dir;
    std::string output_artifact;
    std::string benchmark_artifact;
    bool success = false;
    std::string error;
};

struct AblationBatchReport {
    std::string template_config_path;
    std::string dataset_root;
    std::string output_root;
    size_t requested_limit = 0;
    size_t image_count = 0;
    size_t variant_count = 0;
    size_t success_count = 0;
    size_t failure_count = 0;
    std::string dataset_index_path;
    std::string ablation_report_path;
    std::vector<AblationBatchRecord> records;
};

class AblationBatchRunner {
public:
    std::vector<std::string> default_variants() const;
    AppConfig make_variant_config(
        const AppConfig& base,
        const std::filesystem::path& dataset_root,
        const std::filesystem::path& image_path,
        const std::filesystem::path& output_root,
        const std::string& variant) const;
    AblationBatchReport run(
        const std::filesystem::path& template_config_path,
        const std::filesystem::path& dataset_root,
        const std::filesystem::path& output_root,
        size_t limit = 0) const;
    bool write_json(const std::filesystem::path& path, const AblationBatchReport& report) const;
};

}
