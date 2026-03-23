#include "experiments/DatasetRunner.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace lg {

namespace {

std::string quote_json(const std::string& value) {
    std::ostringstream oss;
    oss << '"';
    for (char c : value) {
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"': oss << "\\\""; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    oss << '"';
    return oss.str();
}

bool looks_like_benchmark_file(const std::filesystem::path& path) {
    if (path.extension() != ".json") {
        return false;
    }
    const std::string name = path.filename().string();
    return name.find("_performance.json") != std::string::npos || name.find("_metrics.json") != std::string::npos;
}

std::filesystem::path resolve_artifact_path(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    std::filesystem::path path(value);
    if (path.is_absolute()) {
        return path;
    }
    return std::filesystem::absolute(path);
}

std::filesystem::path first_existing_path(std::initializer_list<std::filesystem::path> candidates) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path find_sequence_debug_buffer(
    const std::filesystem::path& root,
    const std::string& label,
    const std::string& suffix) {
    const std::filesystem::path dir = root / "debug" / "buffers" / label;
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return {};
    }
    std::vector<std::filesystem::path> matches;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.find(suffix) != std::string::npos) {
            matches.push_back(entry.path());
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches.empty() ? std::filesystem::path{} : matches.front();
}

std::filesystem::path derive_debug_buffer_path(
    const std::filesystem::path& benchmark_path,
    const std::string& label,
    const std::string& suffix) {
    const std::filesystem::path output_root = benchmark_path.parent_path().parent_path();
    const std::filesystem::path flat_dir = output_root / "debug" / "buffers";
    const std::filesystem::path flat_file = first_existing_path({
        flat_dir / (label + suffix + ".png"),
        flat_dir / (label + suffix + ".ppm"),
        flat_dir / (label + suffix + ".pgm")
    });
    if (!flat_file.empty()) {
        return flat_file;
    }
    return find_sequence_debug_buffer(output_root, label, suffix);
}

std::string derive_group_key(const BenchmarkResult& result) {
    const std::string input_key = result.config.input_path.empty() ? result.config.label : result.config.input_path;
    return result.config.input_mode + "|" + input_key;
}

}  // namespace

std::vector<ExperimentRecord> DatasetRunner::collect(const std::filesystem::path& root) const {
    std::vector<ExperimentRecord> records;
    if (!std::filesystem::exists(root)) {
        return records;
    }

    Benchmark benchmark;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !looks_like_benchmark_file(entry.path())) {
            continue;
        }

        BenchmarkResult result;
        std::string error;
        if (!benchmark.read_json(entry.path(), result, &error)) {
            continue;
        }

        ExperimentRecord record{};
        record.group_key = derive_group_key(result);
        record.label = result.config.label;
        record.benchmark_path = std::filesystem::absolute(entry.path()).string();
        record.input_mode = result.config.input_mode;
        record.input_path = resolve_artifact_path(result.config.input_path).string();
        record.performance_mode = result.config.performance_mode;
        record.ablation_variant = result.config.ablation_variant;
        record.width = result.config.width;
        record.height = result.config.height;
        record.half_res_specular = result.config.half_res_specular;
        record.cuda_graph = result.config.cuda_graph;
        record.ablation_refraction = result.config.ablation_refraction;
        record.ablation_reflection = result.config.ablation_reflection;
        record.ablation_specular = result.config.ablation_specular;
        record.ablation_legibility = result.config.ablation_legibility;
        record.ablation_temporal = result.config.ablation_temporal;
        record.ablation_blur_only = result.config.ablation_blur_only;
        record.specular_downsample = result.config.specular_downsample;
        record.legibility_downsample = result.config.legibility_downsample;
        record.frame_count = result.summary.frame_count;
        record.avg_cpu_ms = result.summary.frame_ms;
        record.avg_gpu_ms = result.summary.gpu_frame_ms;
        record.avg_fps = result.summary.fps;
        record.cold_start_cpu_ms = result.cold_start.cpu_ms;
        record.cold_start_gpu_ms = result.cold_start.gpu_ms;
        record.passes = result.summary.passes;

        const std::filesystem::path output_artifact = resolve_artifact_path(result.output_artifact);
        const std::filesystem::path disp_artifact = derive_debug_buffer_path(entry.path(), record.label, "_disp");
        const std::filesystem::path mask_artifact = derive_debug_buffer_path(entry.path(), record.label, "_mask");
        record.output_artifact = output_artifact.string();
        record.disp_debug_artifact = disp_artifact.string();
        record.mask_debug_artifact = mask_artifact.string();
        record.quality = analyze_quality_metrics(
            resolve_artifact_path(result.config.input_path),
            output_artifact,
            disp_artifact,
            mask_artifact);
        if (record.avg_gpu_ms > 0.0) {
            if (record.quality.contrast_available) {
                record.quality_performance_score = record.quality.contrast_preservation / record.avg_gpu_ms;
            } else if (record.quality.fold_over_available) {
                record.quality_performance_score = (1.0 - record.quality.fold_over_mean) / record.avg_gpu_ms;
            }
        }

        records.push_back(std::move(record));
    }

    std::sort(records.begin(), records.end(), [](const ExperimentRecord& lhs, const ExperimentRecord& rhs) {
        if (lhs.group_key != rhs.group_key) {
            return lhs.group_key < rhs.group_key;
        }
        if (lhs.ablation_variant != rhs.ablation_variant) {
            if (lhs.ablation_variant == "ours_full") {
                return true;
            }
            if (rhs.ablation_variant == "ours_full") {
                return false;
            }
            if (lhs.ablation_variant == "ours_fast") {
                return true;
            }
            if (rhs.ablation_variant == "ours_fast") {
                return false;
            }
            return lhs.ablation_variant < rhs.ablation_variant;
        }
        if (lhs.performance_mode != rhs.performance_mode) {
            if (lhs.performance_mode == "full") {
                return true;
            }
            if (rhs.performance_mode == "full") {
                return false;
            }
            return lhs.performance_mode < rhs.performance_mode;
        }
        return lhs.label < rhs.label;
    });
    return records;
}

bool DatasetRunner::write_json(const std::filesystem::path& path, const std::vector<ExperimentRecord>& records) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path);
    if (!out) {
        return false;
    }

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"record_count\": " << records.size() << ",\n";
    out << "  \"records\": [\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const ExperimentRecord& record = records[i];
        out << "    {\n";
        out << "      \"group_key\": " << quote_json(record.group_key) << ",\n";
        out << "      \"label\": " << quote_json(record.label) << ",\n";
        out << "      \"benchmark_path\": " << quote_json(record.benchmark_path) << ",\n";
        out << "      \"input_mode\": " << quote_json(record.input_mode) << ",\n";
        out << "      \"input_path\": " << quote_json(record.input_path) << ",\n";
        out << "      \"performance_mode\": " << quote_json(record.performance_mode) << ",\n";
        out << "      \"ablation_variant\": " << quote_json(record.ablation_variant) << ",\n";
        out << "      \"output_artifact\": " << quote_json(record.output_artifact) << ",\n";
        out << "      \"disp_debug_artifact\": " << quote_json(record.disp_debug_artifact) << ",\n";
        out << "      \"mask_debug_artifact\": " << quote_json(record.mask_debug_artifact) << ",\n";
        out << "      \"width\": " << record.width << ",\n";
        out << "      \"height\": " << record.height << ",\n";
        out << "      \"half_res_specular\": " << (record.half_res_specular ? "true" : "false") << ",\n";
        out << "      \"cuda_graph\": " << (record.cuda_graph ? "true" : "false") << ",\n";
        out << "      \"ablation_refraction\": " << (record.ablation_refraction ? "true" : "false") << ",\n";
        out << "      \"ablation_reflection\": " << (record.ablation_reflection ? "true" : "false") << ",\n";
        out << "      \"ablation_specular\": " << (record.ablation_specular ? "true" : "false") << ",\n";
        out << "      \"ablation_legibility\": " << (record.ablation_legibility ? "true" : "false") << ",\n";
        out << "      \"ablation_temporal\": " << (record.ablation_temporal ? "true" : "false") << ",\n";
        out << "      \"ablation_blur_only\": " << (record.ablation_blur_only ? "true" : "false") << ",\n";
        out << "      \"specular_downsample\": " << record.specular_downsample << ",\n";
        out << "      \"legibility_downsample\": " << record.legibility_downsample << ",\n";
        out << "      \"frame_count\": " << record.frame_count << ",\n";
        out << "      \"avg_cpu_ms\": " << record.avg_cpu_ms << ",\n";
        out << "      \"avg_gpu_ms\": " << record.avg_gpu_ms << ",\n";
        out << "      \"avg_fps\": " << record.avg_fps << ",\n";
        out << "      \"cold_start_cpu_ms\": " << record.cold_start_cpu_ms << ",\n";
        out << "      \"cold_start_gpu_ms\": " << record.cold_start_gpu_ms << ",\n";
        out << "      \"fold_over_available\": " << (record.quality.fold_over_available ? "true" : "false") << ",\n";
        out << "      \"fold_over_mean\": " << record.quality.fold_over_mean << ",\n";
        out << "      \"fold_over_peak\": " << record.quality.fold_over_peak << ",\n";
        out << "      \"contrast_available\": " << (record.quality.contrast_available ? "true" : "false") << ",\n";
        out << "      \"contrast_preservation\": " << record.quality.contrast_preservation << ",\n";
        out << "      \"quality_performance_score\": " << record.quality_performance_score << ",\n";
        out << "      \"passes\": [\n";
        for (size_t j = 0; j < record.passes.size(); ++j) {
            const PassMetricsSummary& pass = record.passes[j];
            out << "        {\"name\": " << quote_json(pass.name)
                << ", \"avg_cpu_ms\": " << pass.avg_cpu_ms
                << ", \"avg_gpu_ms\": " << pass.avg_gpu_ms << "}";
            out << (j + 1 == record.passes.size() ? "\n" : ",\n");
        }
        out << "      ]\n";
        out << "    }" << (i + 1 == records.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

}
