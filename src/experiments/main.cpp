#include "experiments/Ablation.h"
#include "experiments/AblationBatchRunner.h"
#include "experiments/DatasetRunner.h"
#include "experiments/ImageBatchRunner.h"
#include "core/Config.h"
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <string>

namespace {

int usage() {
    std::cerr
        << "usage:\n"
        << "  liquid_glass_analysis benchmark-index <results_root> [output_json]\n"
        << "  liquid_glass_analysis ablation <results_root> [output_json]\n";
    std::cerr
        << "  liquid_glass_analysis image-batch <template_config> <dataset_root> <output_root> [limit]\n";
    std::cerr
        << "  liquid_glass_analysis image-ablation <template_config> <dataset_root> <output_root> [limit]\n";
    return 1;
}

}

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }

    const std::string command = argv[1];
    if (command == "benchmark-index") {
        const std::filesystem::path root = argv[2];
        lg::DatasetRunner runner;
        const std::vector<lg::ExperimentRecord> records = runner.collect(root);
        const std::filesystem::path out = argc > 3 ? std::filesystem::path(argv[3]) : (root / "benchmarks" / "phase8_dataset_index.json");
        if (!runner.write_json(out, records)) {
            std::cerr << "failed to write benchmark index: " << out << '\n';
            return 1;
        }
        std::cout << "wrote " << records.size() << " experiment records to " << out << '\n';
        return 0;
    }

    if (command == "ablation") {
        const std::filesystem::path root = argv[2];
        lg::DatasetRunner runner;
        const std::vector<lg::ExperimentRecord> records = runner.collect(root);
        lg::Ablation ablation;
        const lg::AblationReport report = ablation.build(records);
        const std::filesystem::path out = argc > 3 ? std::filesystem::path(argv[3]) : (root / "ablation" / "phase8_ablation.json");
        if (!ablation.write_json(out, report)) {
            std::cerr << "failed to write ablation report: " << out << '\n';
            return 1;
        }
        std::cout << "wrote ablation report with " << report.experiment_count << " experiments and "
                  << report.comparisons.size() << " comparisons to " << out << '\n';
        return 0;
    }

    if (command == "image-batch") {
        if (argc < 5) {
            return usage();
        }
        const std::filesystem::path template_config = argv[2];
        const std::filesystem::path dataset_root = argv[3];
        const std::filesystem::path output_root = argv[4];
        const size_t limit = argc > 5 ? static_cast<size_t>(std::strtoull(argv[5], nullptr, 10)) : 0;

        lg::ImageBatchRunner batch;
        const lg::BatchValidationReport report = batch.run(template_config, dataset_root, output_root, limit);
        const std::filesystem::path manifest = output_root / "batch_manifest.json";
        if (!batch.write_json(manifest, report)) {
            std::cerr << "failed to write batch manifest: " << manifest << '\n';
            return 1;
        }
        std::cout << "validated " << report.success_count << "/" << report.image_count
                  << " images; manifest=" << manifest << '\n';
        return report.failure_count == 0 ? 0 : 1;
    }

    if (command == "image-ablation") {
        if (argc < 5) {
            return usage();
        }
        const std::filesystem::path template_config = argv[2];
        const std::filesystem::path dataset_root = argv[3];
        const std::filesystem::path output_root = argv[4];
        const size_t limit = argc > 5 ? static_cast<size_t>(std::strtoull(argv[5], nullptr, 10)) : 0;

        lg::AblationBatchRunner batch;
        const lg::AblationBatchReport report = batch.run(template_config, dataset_root, output_root, limit);
        const std::filesystem::path manifest = output_root / "batch_manifest.json";
        if (!batch.write_json(manifest, report)) {
            std::cerr << "failed to write ablation batch manifest: " << manifest << '\n';
            return 1;
        }

        lg::DatasetRunner runner;
        const std::vector<lg::ExperimentRecord> records = runner.collect(output_root);
        if (!runner.write_json(report.dataset_index_path, records)) {
            std::cerr << "failed to write ablation dataset index: " << report.dataset_index_path << '\n';
            return 1;
        }

        lg::Ablation ablation;
        const lg::AblationReport ablation_report = ablation.build(records);
        if (!ablation.write_json(report.ablation_report_path, ablation_report)) {
            std::cerr << "failed to write ablation report: " << report.ablation_report_path << '\n';
            return 1;
        }

        std::cout << "ran " << report.success_count << "/" << report.records.size()
                  << " ablation jobs; manifest=" << manifest
                  << ", dataset_index=" << report.dataset_index_path
                  << ", report=" << report.ablation_report_path << '\n';
        return report.failure_count == 0 ? 0 : 1;
    }

    return usage();
}
