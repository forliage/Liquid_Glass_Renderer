#include "experiments/Ablation.h"
#include <fstream>
#include <iomanip>
#include <map>
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

const ExperimentRecord* choose_baseline(const std::vector<ExperimentRecord>& group) {
    for (const ExperimentRecord& record : group) {
        if (record.ablation_variant == "ours_full") {
            return &record;
        }
    }
    for (const ExperimentRecord& record : group) {
        if (record.performance_mode == "full") {
            return &record;
        }
    }
    return group.empty() ? nullptr : &group.front();
}

}  // namespace

AblationReport Ablation::build(const std::vector<ExperimentRecord>& records) const {
    AblationReport report{};
    report.experiment_count = records.size();
    report.experiments = records;

    std::map<std::string, std::vector<ExperimentRecord>> groups;
    for (const ExperimentRecord& record : records) {
        groups[record.group_key].push_back(record);
    }

    for (const auto& [group_key, group] : groups) {
        const ExperimentRecord* baseline = choose_baseline(group);
        if (!baseline) {
            continue;
        }
        for (const ExperimentRecord& record : group) {
            if (&record == baseline) {
                continue;
            }
            AblationComparison comparison{};
            comparison.group_key = group_key;
            comparison.baseline_label = baseline->label;
            comparison.baseline_mode = baseline->performance_mode;
            comparison.baseline_ablation_variant = baseline->ablation_variant;
            comparison.variant_label = record.label;
            comparison.variant_mode = record.performance_mode;
            comparison.variant_ablation_variant = record.ablation_variant;
            comparison.fps_delta = record.avg_fps - baseline->avg_fps;
            comparison.fps_ratio = baseline->avg_fps > 0.0 ? record.avg_fps / baseline->avg_fps : 0.0;
            comparison.gpu_ms_delta = record.avg_gpu_ms - baseline->avg_gpu_ms;
            comparison.gpu_speedup = record.avg_gpu_ms > 0.0 ? baseline->avg_gpu_ms / record.avg_gpu_ms : 0.0;
            comparison.contrast_delta = record.quality.contrast_preservation - baseline->quality.contrast_preservation;
            comparison.fold_over_delta = record.quality.fold_over_mean - baseline->quality.fold_over_mean;
            comparison.quality_performance_delta = record.quality_performance_score - baseline->quality_performance_score;
            report.comparisons.push_back(std::move(comparison));
        }
    }
    return report;
}

bool Ablation::write_json(const std::filesystem::path& path, const AblationReport& report) const {
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
    out << "  \"experiment_count\": " << report.experiment_count << ",\n";
    out << "  \"experiments\": [\n";
    for (size_t i = 0; i < report.experiments.size(); ++i) {
        const ExperimentRecord& experiment = report.experiments[i];
        out << "    {\n";
        out << "      \"group_key\": " << quote_json(experiment.group_key) << ",\n";
        out << "      \"label\": " << quote_json(experiment.label) << ",\n";
        out << "      \"input_mode\": " << quote_json(experiment.input_mode) << ",\n";
        out << "      \"input_path\": " << quote_json(experiment.input_path) << ",\n";
        out << "      \"performance_mode\": " << quote_json(experiment.performance_mode) << ",\n";
        out << "      \"ablation_variant\": " << quote_json(experiment.ablation_variant) << ",\n";
        out << "      \"output_artifact\": " << quote_json(experiment.output_artifact) << ",\n";
        out << "      \"avg_cpu_ms\": " << experiment.avg_cpu_ms << ",\n";
        out << "      \"avg_gpu_ms\": " << experiment.avg_gpu_ms << ",\n";
        out << "      \"avg_fps\": " << experiment.avg_fps << ",\n";
        out << "      \"fold_over_mean\": " << experiment.quality.fold_over_mean << ",\n";
        out << "      \"fold_over_peak\": " << experiment.quality.fold_over_peak << ",\n";
        out << "      \"contrast_preservation\": " << experiment.quality.contrast_preservation << ",\n";
        out << "      \"quality_performance_score\": " << experiment.quality_performance_score << ",\n";
        out << "      \"passes\": [\n";
        for (size_t j = 0; j < experiment.passes.size(); ++j) {
            const PassMetricsSummary& pass = experiment.passes[j];
            out << "        {\"name\": " << quote_json(pass.name)
                << ", \"avg_cpu_ms\": " << pass.avg_cpu_ms
                << ", \"avg_gpu_ms\": " << pass.avg_gpu_ms << "}";
            out << (j + 1 == experiment.passes.size() ? "\n" : ",\n");
        }
        out << "      ]\n";
        out << "    }" << (i + 1 == report.experiments.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"comparisons\": [\n";
    for (size_t i = 0; i < report.comparisons.size(); ++i) {
        const AblationComparison& comparison = report.comparisons[i];
        out << "    {\n";
        out << "      \"group_key\": " << quote_json(comparison.group_key) << ",\n";
        out << "      \"baseline_label\": " << quote_json(comparison.baseline_label) << ",\n";
        out << "      \"baseline_mode\": " << quote_json(comparison.baseline_mode) << ",\n";
        out << "      \"baseline_ablation_variant\": " << quote_json(comparison.baseline_ablation_variant) << ",\n";
        out << "      \"variant_label\": " << quote_json(comparison.variant_label) << ",\n";
        out << "      \"variant_mode\": " << quote_json(comparison.variant_mode) << ",\n";
        out << "      \"variant_ablation_variant\": " << quote_json(comparison.variant_ablation_variant) << ",\n";
        out << "      \"fps_delta\": " << comparison.fps_delta << ",\n";
        out << "      \"fps_ratio\": " << comparison.fps_ratio << ",\n";
        out << "      \"gpu_ms_delta\": " << comparison.gpu_ms_delta << ",\n";
        out << "      \"gpu_speedup\": " << comparison.gpu_speedup << ",\n";
        out << "      \"contrast_delta\": " << comparison.contrast_delta << ",\n";
        out << "      \"fold_over_delta\": " << comparison.fold_over_delta << ",\n";
        out << "      \"quality_performance_delta\": " << comparison.quality_performance_delta << "\n";
        out << "    }" << (i + 1 == report.comparisons.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

}
