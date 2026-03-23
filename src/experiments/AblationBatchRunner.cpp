#include "experiments/AblationBatchRunner.h"
#include "core/Engine.h"
#include "experiments/ImageBatchRunner.h"
#include <algorithm>
#include <fstream>
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

void apply_variant_defaults(AppConfig& cfg) {
    cfg.ablation.variant = "standard";
    cfg.ablation.refraction = true;
    cfg.ablation.reflection = true;
    cfg.ablation.specular = true;
    cfg.ablation.legibility = true;
    cfg.ablation.temporal = true;
    cfg.ablation.blur_only = false;
    cfg.performance.mode = "full";
    cfg.performance.half_res_specular = false;
    cfg.performance.specular_downsample = 1;
    cfg.performance.legibility_downsample = 1;
}

void configure_variant(AppConfig& cfg, const std::string& variant) {
    apply_variant_defaults(cfg);
    cfg.ablation.variant = variant;

    if (variant == "blur_only") {
        cfg.ablation.refraction = false;
        cfg.ablation.reflection = false;
        cfg.ablation.specular = false;
        cfg.ablation.legibility = false;
        cfg.ablation.temporal = false;
        cfg.ablation.blur_only = true;
        return;
    }

    if (variant == "refraction_only") {
        cfg.ablation.reflection = false;
        cfg.ablation.specular = false;
        cfg.ablation.legibility = false;
        cfg.ablation.temporal = false;
        return;
    }

    if (variant == "reflection_specular_off") {
        cfg.ablation.reflection = false;
        cfg.ablation.specular = false;
        return;
    }

    if (variant == "legibility_off") {
        cfg.ablation.legibility = false;
        return;
    }

    if (variant == "temporal_off") {
        cfg.ablation.temporal = false;
        return;
    }

    if (variant == "ours_fast") {
        cfg.performance.mode = "fast";
        cfg.performance.half_res_specular = true;
        cfg.performance.specular_downsample = std::max(2, cfg.performance.specular_downsample);
        cfg.performance.legibility_downsample = std::max(2, cfg.performance.legibility_downsample);
        return;
    }

    cfg.ablation.variant = "ours_full";
}

}  // namespace

std::vector<std::string> AblationBatchRunner::default_variants() const {
    return {
        "blur_only",
        "refraction_only",
        "reflection_specular_off",
        "legibility_off",
        "temporal_off",
        "ours_fast",
        "ours_full"
    };
}

AppConfig AblationBatchRunner::make_variant_config(
    const AppConfig& base,
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& image_path,
    const std::filesystem::path& output_root,
    const std::string& variant) const {
    ImageBatchRunner helper;
    AppConfig cfg = base;
    cfg.input.mode = "image";
    cfg.input.path = image_path.string();
    cfg.input.loop = true;
    cfg.output.save_frames = true;
    cfg.output.save_debug_buffers = true;
    cfg.output.headless = true;
    cfg.performance.opengl_interop = false;
    configure_variant(cfg, variant);
    cfg.output.out_dir = (output_root / helper.make_label(dataset_root, image_path) / cfg.ablation.variant).string();
    return cfg;
}

AblationBatchReport AblationBatchRunner::run(
    const std::filesystem::path& template_config_path,
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& output_root,
    size_t limit) const {
    AblationBatchReport report{};
    report.template_config_path = template_config_path.string();
    report.dataset_root = dataset_root.string();
    report.output_root = output_root.string();
    report.requested_limit = limit;

    const AppConfig base = ConfigLoader::load(template_config_path.string());
    ImageBatchRunner helper;
    const std::vector<std::filesystem::path> images = helper.collect_images(dataset_root, limit);
    const std::vector<std::string> variants = default_variants();
    report.image_count = images.size();
    report.variant_count = variants.size();

    for (const std::filesystem::path& image_path : images) {
        for (const std::string& variant : variants) {
            AblationBatchRecord record{};
            record.label = helper.make_label(dataset_root, image_path);
            record.input_path = image_path.string();
            record.variant = variant;

            const AppConfig cfg = make_variant_config(base, dataset_root, image_path, output_root, variant);
            record.output_dir = cfg.output.out_dir;
            record.output_artifact = (std::filesystem::path(cfg.output.out_dir) / "images" / (image_path.stem().string() + "_liquid_glass.png")).string();
            record.benchmark_artifact = (std::filesystem::path(cfg.output.out_dir) / "benchmarks" / (image_path.stem().string() + "_performance.json")).string();

            try {
                Engine engine;
                if (!engine.initialize(cfg)) {
                    record.error = "engine initialization failed";
                } else if (engine.run() != 0) {
                    record.error = "engine run failed";
                } else {
                    record.success = true;
                }
            } catch (const std::exception& e) {
                record.error = e.what();
            }

            if (record.success) {
                ++report.success_count;
            } else {
                ++report.failure_count;
            }
            report.records.push_back(std::move(record));
        }
    }

    report.dataset_index_path = (output_root / "benchmarks" / "ablation_dataset_index.json").string();
    report.ablation_report_path = (output_root / "ablation" / "ablation_report.json").string();
    return report;
}

bool AblationBatchRunner::write_json(const std::filesystem::path& path, const AblationBatchReport& report) const {
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

    out << "{\n";
    out << "  \"template_config_path\": " << quote_json(report.template_config_path) << ",\n";
    out << "  \"dataset_root\": " << quote_json(report.dataset_root) << ",\n";
    out << "  \"output_root\": " << quote_json(report.output_root) << ",\n";
    out << "  \"requested_limit\": " << report.requested_limit << ",\n";
    out << "  \"image_count\": " << report.image_count << ",\n";
    out << "  \"variant_count\": " << report.variant_count << ",\n";
    out << "  \"success_count\": " << report.success_count << ",\n";
    out << "  \"failure_count\": " << report.failure_count << ",\n";
    out << "  \"dataset_index_path\": " << quote_json(report.dataset_index_path) << ",\n";
    out << "  \"ablation_report_path\": " << quote_json(report.ablation_report_path) << ",\n";
    out << "  \"records\": [\n";
    for (size_t i = 0; i < report.records.size(); ++i) {
        const AblationBatchRecord& record = report.records[i];
        out << "    {\n";
        out << "      \"label\": " << quote_json(record.label) << ",\n";
        out << "      \"input_path\": " << quote_json(record.input_path) << ",\n";
        out << "      \"variant\": " << quote_json(record.variant) << ",\n";
        out << "      \"output_dir\": " << quote_json(record.output_dir) << ",\n";
        out << "      \"output_artifact\": " << quote_json(record.output_artifact) << ",\n";
        out << "      \"benchmark_artifact\": " << quote_json(record.benchmark_artifact) << ",\n";
        out << "      \"success\": " << (record.success ? "true" : "false") << ",\n";
        out << "      \"error\": " << quote_json(record.error) << "\n";
        out << "    }" << (i + 1 == report.records.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

}
