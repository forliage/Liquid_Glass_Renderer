#include "experiments/Metrics.h"
#include "io/ImageIO.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace lg {

namespace {

double luminance_at(const CpuFrame& frame, int index) {
    if (frame.channels == 1) {
        return static_cast<double>(frame.data[static_cast<size_t>(index)]) / 255.0;
    }
    const size_t base = static_cast<size_t>(index * frame.channels);
    const double r = static_cast<double>(frame.data[base + 0]) / 255.0;
    const double g = static_cast<double>(frame.data[base + 1]) / 255.0;
    const double b = static_cast<double>(frame.data[base + 2]) / 255.0;
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

bool mask_allows(const CpuFrame* mask, int index) {
    if (!mask || mask->data.empty()) {
        return true;
    }
    if (mask->channels == 1) {
        return mask->data[static_cast<size_t>(index)] > 0;
    }
    const size_t base = static_cast<size_t>(index * mask->channels);
    return mask->data[base + 0] > 0 || mask->data[base + 1] > 0 || mask->data[base + 2] > 0;
}

double contrast_energy(const CpuFrame& frame, const CpuFrame* mask) {
    if (frame.width < 2 || frame.height < 2 || frame.data.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    size_t count = 0;
    for (int y = 0; y < frame.height - 1; ++y) {
        for (int x = 0; x < frame.width - 1; ++x) {
            const int index = y * frame.width + x;
            if (!mask_allows(mask, index)) {
                continue;
            }
            const double center = luminance_at(frame, index);
            const double right = luminance_at(frame, index + 1);
            const double down = luminance_at(frame, index + frame.width);
            sum += 0.5 * (std::abs(center - right) + std::abs(center - down));
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

bool load_optional_frame(const std::filesystem::path& path, CpuFrame& frame) {
    std::string error;
    return !path.empty() && std::filesystem::exists(path) && load_image_file_rgba(path.string(), frame, &error);
}

}  // namespace

MetricsSummary summarize_metrics(const std::vector<FrameTiming>& frames, double wall_ms) {
    MetricsSummary summary{};
    summary.wall_ms = wall_ms;
    summary.frame_count = frames.size();
    if (frames.empty()) {
        return summary;
    }

    double cpu_sum = 0.0;
    double gpu_sum = 0.0;
    std::unordered_map<std::string, size_t> pass_indices;
    for (const FrameTiming& frame : frames) {
        cpu_sum += frame.cpu_ms;
        gpu_sum += frame.gpu_ms;
        for (const PassTiming& pass : frame.passes) {
            auto it = pass_indices.find(pass.name);
            if (it == pass_indices.end()) {
                pass_indices.emplace(pass.name, summary.passes.size());
                summary.passes.push_back({pass.name, pass.cpu_ms, pass.gpu_ms});
            } else {
                PassMetricsSummary& aggregate = summary.passes[it->second];
                aggregate.avg_cpu_ms += pass.cpu_ms;
                aggregate.avg_gpu_ms += pass.gpu_ms;
            }
        }
    }

    const double frame_count = static_cast<double>(frames.size());
    summary.frame_ms = cpu_sum / frame_count;
    summary.gpu_frame_ms = gpu_sum / frame_count;
    summary.fps = wall_ms > 0.0 ? frame_count * 1000.0 / wall_ms : 0.0;
    for (PassMetricsSummary& pass : summary.passes) {
        pass.avg_cpu_ms /= frame_count;
        pass.avg_gpu_ms /= frame_count;
    }
    return summary;
}

double compute_fold_over_mean(const CpuFrame& disp, const CpuFrame* mask) {
    if (disp.channels < 3 || disp.data.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    size_t count = 0;
    for (int i = 0; i < disp.width * disp.height; ++i) {
        if (!mask_allows(mask, i)) {
            continue;
        }
        const size_t base = static_cast<size_t>(i * disp.channels);
        sum += static_cast<double>(disp.data[base + 2]) / 255.0;
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

double compute_fold_over_peak(const CpuFrame& disp, const CpuFrame* mask) {
    if (disp.channels < 3 || disp.data.empty()) {
        return 0.0;
    }

    double peak = 0.0;
    for (int i = 0; i < disp.width * disp.height; ++i) {
        if (!mask_allows(mask, i)) {
            continue;
        }
        const size_t base = static_cast<size_t>(i * disp.channels);
        peak = std::max(peak, static_cast<double>(disp.data[base + 2]) / 255.0);
    }
    return peak;
}

double compute_local_contrast_preservation(const CpuFrame& input, const CpuFrame& output, const CpuFrame* mask) {
    CpuFrame resized_input = input;
    if (input.width != output.width || input.height != output.height) {
        resized_input = resize_with_letterbox(input, output.width, output.height);
    }
    const double input_contrast = contrast_energy(resized_input, mask);
    const double output_contrast = contrast_energy(output, mask);
    if (input_contrast <= 1e-6) {
        return 0.0;
    }
    return output_contrast / input_contrast;
}

QualityMetricsSummary analyze_quality_metrics(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const std::filesystem::path& disp_path,
    const std::filesystem::path& mask_path) {
    QualityMetricsSummary summary{};

    CpuFrame mask;
    CpuFrame* mask_ptr = nullptr;
    if (load_optional_frame(mask_path, mask)) {
        mask_ptr = &mask;
    }

    CpuFrame disp;
    if (load_optional_frame(disp_path, disp)) {
        summary.fold_over_available = true;
        summary.fold_over_mean = compute_fold_over_mean(disp, mask_ptr);
        summary.fold_over_peak = compute_fold_over_peak(disp, mask_ptr);
    }

    CpuFrame input;
    CpuFrame output;
    if (load_optional_frame(input_path, input) && load_optional_frame(output_path, output)) {
        summary.contrast_available = true;
        summary.contrast_preservation = compute_local_contrast_preservation(input, output, mask_ptr);
    }

    return summary;
}

}
