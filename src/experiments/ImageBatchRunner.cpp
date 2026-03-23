#include "experiments/ImageBatchRunner.h"
#include "core/Engine.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace lg {

namespace {

bool is_supported_image_file(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    const std::string name = path.filename().string();
    if (name.find(":Zone.Identifier") != std::string::npos) {
        return false;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".ppm" || ext == ".pgm" || ext == ".pnm";
}

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

std::string sanitize_label_component(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out;
}

}  // namespace

std::vector<std::filesystem::path> ImageBatchRunner::collect_images(const std::filesystem::path& root, size_t limit) const {
    std::vector<std::filesystem::path> images;
    if (!std::filesystem::exists(root)) {
        return images;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !is_supported_image_file(entry.path())) {
            continue;
        }
        images.push_back(entry.path());
    }
    std::sort(images.begin(), images.end());
    if (limit > 0 && images.size() > limit) {
        images.resize(limit);
    }
    return images;
}

std::string ImageBatchRunner::make_label(const std::filesystem::path& dataset_root, const std::filesystem::path& image_path) const {
    std::filesystem::path relative = image_path.filename();
    std::error_code ec;
    const std::filesystem::path candidate = std::filesystem::relative(image_path, dataset_root, ec);
    if (!ec && !candidate.empty()) {
        relative = candidate;
    }

    std::string label;
    for (const auto& part : relative) {
        std::string piece = part.string();
        if (piece == ".") {
            continue;
        }
        if (!label.empty()) {
            label += "__";
        }
        if (part == relative.filename()) {
            label += sanitize_label_component(part.stem().string());
        } else {
            label += sanitize_label_component(piece);
        }
    }
    return label.empty() ? sanitize_label_component(image_path.stem().string()) : label;
}

AppConfig ImageBatchRunner::make_config(
    const AppConfig& base,
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& image_path,
    const std::filesystem::path& output_root) const {
    AppConfig cfg = base;
    cfg.input.mode = "image";
    cfg.input.path = image_path.string();
    cfg.input.loop = true;
    cfg.output.save_frames = true;
    cfg.output.save_debug_buffers = true;
    cfg.output.headless = true;
    cfg.performance.opengl_interop = false;
    cfg.output.out_dir = (output_root / make_label(dataset_root, image_path)).string();
    return cfg;
}

BatchValidationReport ImageBatchRunner::run(
    const std::filesystem::path& template_config_path,
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& output_root,
    size_t limit) const {
    BatchValidationReport report{};
    report.template_config_path = template_config_path.string();
    report.dataset_root = dataset_root.string();
    report.output_root = output_root.string();
    report.requested_limit = limit;

    const AppConfig base = ConfigLoader::load(template_config_path.string());
    const std::vector<std::filesystem::path> images = collect_images(dataset_root, limit);
    report.image_count = images.size();

    for (const std::filesystem::path& image_path : images) {
        BatchValidationRecord record{};
        record.label = make_label(dataset_root, image_path);
        record.input_path = image_path.string();

        const AppConfig cfg = make_config(base, dataset_root, image_path, output_root);
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

    return report;
}

bool ImageBatchRunner::write_json(const std::filesystem::path& path, const BatchValidationReport& report) const {
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
    out << "  \"success_count\": " << report.success_count << ",\n";
    out << "  \"failure_count\": " << report.failure_count << ",\n";
    out << "  \"records\": [\n";
    for (size_t i = 0; i < report.records.size(); ++i) {
        const BatchValidationRecord& record = report.records[i];
        out << "    {\n";
        out << "      \"label\": " << quote_json(record.label) << ",\n";
        out << "      \"input_path\": " << quote_json(record.input_path) << ",\n";
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
