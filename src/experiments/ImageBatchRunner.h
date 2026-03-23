#pragma once
#include "core/Config.h"
#include <filesystem>
#include <string>
#include <vector>

namespace lg {

struct BatchValidationRecord {
    std::string label;
    std::string input_path;
    std::string output_dir;
    std::string output_artifact;
    std::string benchmark_artifact;
    bool success = false;
    std::string error;
};

struct BatchValidationReport {
    std::string template_config_path;
    std::string dataset_root;
    std::string output_root;
    size_t requested_limit = 0;
    size_t image_count = 0;
    size_t success_count = 0;
    size_t failure_count = 0;
    std::vector<BatchValidationRecord> records;
};

class ImageBatchRunner {
public:
    std::vector<std::filesystem::path> collect_images(const std::filesystem::path& root, size_t limit = 0) const;
    std::string make_label(const std::filesystem::path& dataset_root, const std::filesystem::path& image_path) const;
    AppConfig make_config(
        const AppConfig& base,
        const std::filesystem::path& dataset_root,
        const std::filesystem::path& image_path,
        const std::filesystem::path& output_root) const;
    BatchValidationReport run(
        const std::filesystem::path& template_config_path,
        const std::filesystem::path& dataset_root,
        const std::filesystem::path& output_root,
        size_t limit = 0) const;
    bool write_json(const std::filesystem::path& path, const BatchValidationReport& report) const;
};

}
