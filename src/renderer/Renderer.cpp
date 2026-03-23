#include "renderer/Renderer.h"
#include "renderer/CUDAInterop.h"
#include <cuda_runtime.h>
#include "experiments/Benchmark.h"
#include "io/ImageIO.h"
#include "liquid_glass/ShapeSDF.h"
#include "utils/Logger.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
namespace lg {
namespace {

std::string debug_filename_for(const std::string& stem, GlassBufferId id, int channels) {
    std::string name = stem;
    switch (id) {
        case GlassBufferId::Mask: name += "_mask"; break;
        case GlassBufferId::Sdf: name += "_sdf"; break;
        case GlassBufferId::Thickness: name += "_thickness"; break;
        case GlassBufferId::Normal: name += "_normal"; break;
        case GlassBufferId::Disp: name += "_disp"; break;
        case GlassBufferId::Refracted: name += "_refracted"; break;
        case GlassBufferId::Reflection: name += "_reflection"; break;
        case GlassBufferId::Specular: name += "_specular"; break;
        case GlassBufferId::Legibility: name += "_legibility"; break;
        case GlassBufferId::Final: name += "_final"; break;
        default: name += "_buffer"; break;
    }
    (void)channels;
    name += ".png";
    return name;
}

CpuFrame to_rgba_frame(const CpuFrame& frame) {
    if (frame.channels == 4) {
        return frame;
    }

    CpuFrame out;
    out.width = frame.width;
    out.height = frame.height;
    out.channels = 4;
    out.data.assign(static_cast<size_t>(frame.width * frame.height * 4), 0);

    for (int i = 0; i < frame.width * frame.height; ++i) {
        const size_t dst = static_cast<size_t>(i) * 4;
        if (frame.channels == 1) {
            const uint8_t v = frame.data[static_cast<size_t>(i)];
            out.data[dst + 0] = v;
            out.data[dst + 1] = v;
            out.data[dst + 2] = v;
        } else if (frame.channels == 3) {
            out.data[dst + 0] = frame.data[static_cast<size_t>(i) * 3 + 0];
            out.data[dst + 1] = frame.data[static_cast<size_t>(i) * 3 + 1];
            out.data[dst + 2] = frame.data[static_cast<size_t>(i) * 3 + 2];
        }
        out.data[dst + 3] = 255;
    }
    return out;
}

void blend_rect(CpuFrame& frame, const RectI& rect, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    const int x0 = std::max(0, rect.x);
    const int y0 = std::max(0, rect.y);
    const int x1 = std::min(frame.width, rect.x + rect.width);
    const int y1 = std::min(frame.height, rect.y + rect.height);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const size_t idx = static_cast<size_t>((y * frame.width + x) * 4);
            frame.data[idx + 0] = static_cast<uint8_t>((frame.data[idx + 0] * (255 - alpha) + r * alpha) / 255);
            frame.data[idx + 1] = static_cast<uint8_t>((frame.data[idx + 1] * (255 - alpha) + g * alpha) / 255);
            frame.data[idx + 2] = static_cast<uint8_t>((frame.data[idx + 2] * (255 - alpha) + b * alpha) / 255);
            frame.data[idx + 3] = 255;
        }
    }
}

void stroke_rect(CpuFrame& frame, const RectI& rect, uint8_t r, uint8_t g, uint8_t b) {
    blend_rect(frame, {rect.x, rect.y, rect.width, 2}, r, g, b, 255);
    blend_rect(frame, {rect.x, rect.y + rect.height - 2, rect.width, 2}, r, g, b, 255);
    blend_rect(frame, {rect.x, rect.y, 2, rect.height}, r, g, b, 255);
    blend_rect(frame, {rect.x + rect.width - 2, rect.y, 2, rect.height}, r, g, b, 255);
}

void draw_slider_bar(CpuFrame& frame, const DebugSliderView& slider, uint8_t r, uint8_t g, uint8_t b) {
    blend_rect(frame, slider.bounds, 24, 24, 24, 180);
    const float range = std::max(1e-5f, slider.max_value - slider.min_value);
    const float t = std::clamp((slider.value - slider.min_value) / range, 0.0f, 1.0f);
    const int fill_width = std::max(1, static_cast<int>(slider.bounds.width * t));
    blend_rect(frame, {slider.bounds.x, slider.bounds.y, fill_width, slider.bounds.height}, r, g, b, slider.active ? 220 : 180);
    stroke_rect(frame, slider.bounds, slider.active ? 255 : 180, slider.active ? 255 : 180, slider.active ? 255 : 180);
}

void draw_playback_timeline(CpuFrame& frame, const PlaybackTimelineView& playback) {
    if (!playback.available) {
        return;
    }

    blend_rect(frame, playback.bounds, 20, 20, 20, 200);
    const int buffered_width = std::max(1, static_cast<int>(playback.bounds.width * std::clamp(playback.buffered_progress, 0.0f, 1.0f)));
    blend_rect(frame, {playback.bounds.x, playback.bounds.y, buffered_width, playback.bounds.height}, 70, 70, 70, 210);

    const uint8_t progress_r = playback.paused ? 255 : 56;
    const uint8_t progress_g = playback.paused ? 193 : 187;
    const uint8_t progress_b = playback.paused ? 7 : 84;
    const int progress_width = std::max(1, static_cast<int>(playback.bounds.width * std::clamp(playback.progress, 0.0f, 1.0f)));
    blend_rect(frame, {playback.bounds.x, playback.bounds.y, progress_width, playback.bounds.height}, progress_r, progress_g, progress_b, playback.active ? 255 : 220);
    stroke_rect(frame, playback.bounds, 245, 245, 245);

    const int handle_x = playback.bounds.x + progress_width - 3;
    blend_rect(frame, {handle_x, playback.bounds.y - 3, 6, playback.bounds.height + 6}, 255, 255, 255, 255);

    if (playback.loop) {
        blend_rect(frame, {playback.bounds.x + playback.bounds.width - 24, playback.bounds.y - 10, 20, 8}, 33, 150, 243, 220);
    }
}

ThicknessProfileKind runtime_profile_kind(const std::string& value) {
    return thickness_profile_kind_from_string(value);
}

std::string frame_index_string(int frame_index) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(6) << frame_index;
    return oss.str();
}

BenchmarkConfig make_benchmark_config(const AppConfig& config, const std::string& label) {
    BenchmarkConfig benchmark{};
    benchmark.label = label;
    benchmark.input_mode = config.input.mode;
    benchmark.input_path = config.input.path;
    benchmark.performance_mode = config.performance.mode;
    benchmark.ablation_variant = config.ablation.variant;
    benchmark.width = config.window.width;
    benchmark.height = config.window.height;
    benchmark.half_res_specular = config.performance.half_res_specular;
    benchmark.cuda_graph = config.performance.cuda_graph;
    benchmark.ablation_refraction = config.ablation.refraction;
    benchmark.ablation_reflection = config.ablation.reflection;
    benchmark.ablation_specular = config.ablation.specular;
    benchmark.ablation_legibility = config.ablation.legibility;
    benchmark.ablation_temporal = config.ablation.temporal;
    benchmark.ablation_blur_only = config.ablation.blur_only;
    benchmark.specular_downsample = config.performance.specular_downsample;
    benchmark.legibility_downsample = config.performance.legibility_downsample;
    benchmark.warmup_frames = config.performance.warmup_frames;
    benchmark.benchmark_frames = config.performance.benchmark_frames;
    return benchmark;
}

}  // namespace

bool Renderer::initialize(const AppConfig& config){
    config_=config;
    tabbar_=std::make_unique<TabBar>(config.tabbar);
    interaction_.initialize(config.window.width, config.window.height);
    direct_present_supported_ = config.performance.opengl_interop;
    direct_present_confirmed_ = false;
    if (!pipeline_.initialize(config.window.width, config.window.height)) {
        log(LogLevel::Error, "failed to initialize glass pipeline");
        return false;
    }
    std::string controls = "controls: drag the tab bar with the mouse; overlay sliders adjust width/height/radius/thickness/refraction; keys 1-5 switch final/mask/sdf/normal/disp; Q/E cycles thickness profiles; Tab toggles the overlay";
    if (config.input.mode == "realtime_video") {
        controls += "; Space pauses/resumes; Left/Right seeks 5s; R restarts; L toggles loop; drag the bottom timeline to scrub";
    }
    log(LogLevel::Info, controls);
    log(LogLevel::Info, "performance mode " + config.performance.mode
        + ": spec_downsample=" + std::to_string(config.performance.specular_downsample)
        + ", legibility_downsample=" + std::to_string(config.performance.legibility_downsample)
        + ", warmup_frames=" + std::to_string(config.performance.warmup_frames)
        + ", benchmark_frames=" + std::to_string(config.performance.benchmark_frames)
        + ", half_res_specular=" + std::string(config.performance.half_res_specular ? "true" : "false")
        + ", cuda_graph=" + std::string(config.performance.cuda_graph ? "true" : "false")
        + ", opengl_interop=" + std::string(config.performance.opengl_interop ? "true" : "false")
        + ", ablation.variant=" + config.ablation.variant);
    return true;
}

void Renderer::mark_direct_present_result(bool success) {
    direct_present_supported_ = resolve_cuda_interop_support_after_attempt(
        direct_present_supported_,
        direct_present_confirmed_,
        success);
    direct_present_confirmed_ = direct_present_confirmed_ || success;
}
void Renderer::set_source(std::shared_ptr<IFrameSource> source){ source_=std::move(source); }
GlassParams Renderer::build_params() const {
    GlassParams p{};
    p.width = prepared_.width;
    p.height = prepared_.height;
    p.cx=tabbar_->config().cx;
    p.cy=tabbar_->config().cy;
    p.tab_w=tabbar_->config().width;
    p.tab_h=tabbar_->config().height;
    p.radius=tabbar_->config().corner_radius;
    p.h0=config_.glass.h0;
    p.refraction_strength=config_.glass.refraction_strength;
    p.center_strength=config_.glass.center_strength;
    p.edge_sigma=config_.glass.edge_sigma;
    p.fresnel_strength=config_.glass.fresnel_strength;
    p.specular_strength=config_.glass.specular_strength;
    p.temporal_alpha=config_.glass.temporal_alpha;
    p.edge_boost=config_.glass.edge_boost;
    p.edge_glow_strength=config_.glass.edge_glow_strength;
    p.displacement_limit=config_.glass.displacement_limit;
    p.jacobian_guard=config_.glass.jacobian_guard;
    p.foreground_protect=config_.glass.foreground_protect;
    p.legibility_boost=config_.glass.legibility_boost;
    p.specular_power=config_.glass.specular_power;
    p.profile_p=config_.glass.profile_p;
    p.profile_q=config_.glass.profile_q;
    p.thickness_profile=static_cast<int>(runtime_profile_kind(config_.glass.thickness_profile));
    p.target_contrast=config_.legibility.target_contrast;
    p.tile_size=config_.legibility.tile_size;
    p.legibility_enabled=(config_.legibility.enabled && config_.ablation.legibility) ? 1 : 0;
    p.refraction_enabled=config_.ablation.refraction ? 1 : 0;
    p.reflection_enabled=config_.ablation.reflection ? 1 : 0;
    p.specular_enabled=config_.ablation.specular ? 1 : 0;
    p.temporal_enabled=config_.ablation.temporal ? 1 : 0;
    p.blur_background_only=config_.ablation.blur_only ? 1 : 0;
    p.specular_step = std::max(1, config_.performance.specular_downsample);
    if (config_.performance.half_res_specular) {
        p.specular_step = std::max(2, p.specular_step);
    }
    p.legibility_step = std::max(1, config_.performance.legibility_downsample);
    if (config_.performance.mode == "fast") {
        p.specular_step = std::max(2, p.specular_step);
        p.legibility_step = std::max(2, p.legibility_step);
    }
    p.use_cuda_graph = config_.performance.cuda_graph ? 1 : 0;
    if (tabbar_->hovered()) {
        p.specular_strength *= 1.1f;
        p.fresnel_strength *= 1.05f;
        p.edge_glow_strength *= 1.15f;
    }
    if (tabbar_->pressed()) {
        p.refraction_strength *= 0.92f;
        p.h0 = std::max(0.0f, p.h0 - 1.5f);
        p.displacement_limit *= 0.95f;
    }
    if (tabbar_->selected()) {
        p.h0 += 2.0f;
        p.specular_strength *= 1.08f;
        p.foreground_protect = std::min(1.0f, p.foreground_protect + 0.1f);
    }
    for(int i=0;i<3;++i){
        p.absorb[i]=config_.glass.absorb[i];
        p.tint[i]=config_.glass.tint[i];
    }
    return p;
}

bool Renderer::save_image_outputs_once() {
    if (outputs_saved_) {
        return true;
    }

    if (!image_capture_started_) {
        image_capture_timer_.reset();
        image_capture_timer_.tic("image_total");
        if (config_.performance.warmup_frames == 0) {
            image_capture_timer_.tic("image_measured");
            image_measurement_started_ = true;
        }
        image_capture_started_ = true;
    }

    const bool warmup = image_frames_seen_ < config_.performance.warmup_frames;
    image_samples_.push_back({image_frames_seen_, current_.pts, last_timing_, warmup});
    ++image_frames_seen_;

    if (!image_measurement_started_ && image_frames_seen_ >= config_.performance.warmup_frames) {
        image_capture_timer_.tic("image_measured");
        image_measurement_started_ = true;
    }

    int measured_frames = 0;
    for (const BenchmarkSample& sample : image_samples_) {
        if (!sample.warmup) {
            ++measured_frames;
        }
    }
    if (measured_frames < config_.performance.benchmark_frames) {
        return true;
    }

    const std::filesystem::path stem_path = std::filesystem::path(config_.input.path).stem();
    const std::string stem = stem_path.empty() ? std::string("image") : stem_path.string();

    if (config_.output.save_frames) {
        std::string error;
        const std::filesystem::path final_path = std::filesystem::path(config_.output.out_dir) / "images" / (stem + "_liquid_glass.png");
        if (!save_image_file(final_path.string(), final_frame_, &error)) {
            log(LogLevel::Warn, "failed to save final image: " + error);
            return false;
        }
        log(LogLevel::Info, "saved image output to " + final_path.string());
    }

    if (config_.output.save_debug_buffers) {
        const GlassBufferId buffers[] = {
            GlassBufferId::Mask,
            GlassBufferId::Sdf,
            GlassBufferId::Thickness,
            GlassBufferId::Normal,
            GlassBufferId::Disp,
            GlassBufferId::Refracted,
            GlassBufferId::Reflection,
            GlassBufferId::Specular,
            GlassBufferId::Legibility,
            GlassBufferId::Final
        };
        for (GlassBufferId buffer : buffers) {
            CpuFrame debug = pipeline_.download_buffer(buffer);
            std::string error;
            const std::filesystem::path path = std::filesystem::path(config_.output.out_dir) / "debug" / "buffers" / debug_filename_for(stem, buffer, debug.channels);
            if (!save_image_file(path.string(), debug, &error)) {
                log(LogLevel::Warn, "failed to save debug buffer: " + error);
                return false;
            }
        }
        log(LogLevel::Info, "saved debug buffers under " + (std::filesystem::path(config_.output.out_dir) / "debug" / "buffers").string());
    }

    const std::filesystem::path benchmark_path = std::filesystem::path(config_.output.out_dir) / "benchmarks" / (stem + "_performance.json");
    Benchmark benchmark;
    const BenchmarkResult result = benchmark.build(
        make_benchmark_config(config_, stem),
        image_samples_,
        image_capture_timer_.toc_ms("image_total"),
        image_measurement_started_ ? image_capture_timer_.toc_ms("image_measured") : image_capture_timer_.toc_ms("image_total"),
        0.0,
        (std::filesystem::path(config_.output.out_dir) / "images" / (stem + "_liquid_glass.png")).string());
    if (!benchmark.write_json(benchmark_path, result)) {
        log(LogLevel::Warn, "failed to write benchmark json: " + benchmark_path.string());
        return false;
    }
    log(LogLevel::Info, "saved performance summary to " + benchmark_path.string());
    log(LogLevel::Info,
        "image benchmark complete: warmup_frames=" + std::to_string(result.warmup_frame_count)
        + ", measured_frames=" + std::to_string(result.measured_frame_count)
        + ", cold_start_cpu_ms=" + [&] {
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(3) << result.cold_start.cpu_ms;
              return oss.str();
          }()
        + ", steady_cpu_ms=" + [&] {
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(3) << result.summary.frame_ms;
              return oss.str();
          }()
        + ", steady_gpu_ms=" + [&] {
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(3) << result.summary.gpu_frame_ms;
              return oss.str();
          }());
    log(LogLevel::Info, "steady-state pass timing: " + format_frame_timing_summary(result.samples.back().timing));

    outputs_saved_ = true;
    return true;
}

bool Renderer::save_video_outputs_for_frame(int frame_index) {
    if (frame_index < 0 || (!config_.output.save_frames && !config_.output.save_debug_buffers)) {
        return true;
    }

    const std::filesystem::path stem_path = std::filesystem::path(config_.input.path).stem();
    const std::string stem = stem_path.empty() ? std::string("video") : stem_path.string();
    const std::string index = frame_index_string(frame_index);

    if (config_.output.save_frames) {
        std::string error;
        const std::filesystem::path dir = std::filesystem::path(config_.output.out_dir) / "videos" / "frames" / stem;
        std::filesystem::create_directories(dir);
        const std::filesystem::path final_path = dir / (stem + "_" + index + ".png");
        if (!save_image_file(final_path.string(), final_frame_, &error)) {
            log(LogLevel::Warn, "failed to save video frame dump: " + error);
            return false;
        }
    }

    if (config_.output.save_debug_buffers) {
        const GlassBufferId buffers[] = {
            GlassBufferId::Mask,
            GlassBufferId::Sdf,
            GlassBufferId::Thickness,
            GlassBufferId::Normal,
            GlassBufferId::Disp,
            GlassBufferId::Refracted,
            GlassBufferId::Reflection,
            GlassBufferId::Specular,
            GlassBufferId::Legibility,
            GlassBufferId::Final
        };
        const std::filesystem::path dir = std::filesystem::path(config_.output.out_dir) / "debug" / "buffers" / stem;
        std::filesystem::create_directories(dir);
        for (GlassBufferId buffer : buffers) {
            CpuFrame debug = pipeline_.download_buffer(buffer);
            std::string error;
            const std::filesystem::path path = dir / debug_filename_for(stem + "_" + index, buffer, debug.channels);
            if (!save_image_file(path.string(), debug, &error)) {
                log(LogLevel::Warn, "failed to save video debug buffer: " + error);
                return false;
            }
        }
    }

    return true;
}

CpuFrame Renderer::build_view_frame() {
    const GlassBufferId buffer = buffer_for_debug_view(interaction_.debug_view());
    if (buffer == GlassBufferId::Final) {
        return final_frame_;
    }
    return pipeline_.download_buffer(buffer);
}

void Renderer::build_display_overlay() {
    presented_ = to_rgba_frame(viewed_);
    if (!interaction_.overlay_visible()) {
        return;
    }

    static const uint8_t slider_colors[][3] = {
        {76, 175, 80},
        {3, 169, 244},
        {255, 193, 7},
        {255, 87, 34},
        {156, 39, 176}
    };

    const auto& sliders = interaction_.sliders();
    for (size_t i = 0; i < sliders.size(); ++i) {
        const uint8_t* color = slider_colors[i % (sizeof(slider_colors) / sizeof(slider_colors[0]))];
        draw_slider_bar(presented_, sliders[i], color[0], color[1], color[2]);
    }

    const auto& buttons = interaction_.debug_buttons();
    for (const DebugButtonView& button : buttons) {
        uint8_t r = 80;
        uint8_t g = 80;
        uint8_t b = 80;
        switch (button.mode) {
            case DebugViewMode::Final: r = 255; g = 255; b = 255; break;
            case DebugViewMode::Mask: r = 0; g = 200; b = 120; break;
            case DebugViewMode::Sdf: r = 33; g = 150; b = 243; break;
            case DebugViewMode::Normal: r = 255; g = 152; b = 0; break;
            case DebugViewMode::Disp: r = 233; g = 30; b = 99; break;
        }
        blend_rect(presented_, button.bounds, r, g, b, button.active ? 255 : 170);
        stroke_rect(presented_, button.bounds, 0, 0, 0);
    }

    const RectI tab_rect{
        static_cast<int>(tabbar_->config().cx - tabbar_->config().width * 0.5f),
        static_cast<int>(tabbar_->config().cy - tabbar_->config().height * 0.5f),
        static_cast<int>(tabbar_->config().width),
        static_cast<int>(tabbar_->config().height)
    };

    if (tabbar_->selected()) {
        stroke_rect(presented_, tab_rect, 80, 220, 120);
    } else if (tabbar_->pressed()) {
        stroke_rect(presented_, tab_rect, 255, 120, 80);
    } else if (tabbar_->hovered()) {
        stroke_rect(presented_, tab_rect, 255, 255, 255);
    }

    draw_playback_timeline(presented_, interaction_.playback_timeline());
}

bool Renderer::render_frame(
    const CpuFrame& source_frame,
    const FrameInput& input,
    int frame_index,
    const PlaybackUiState* playback,
    PlaybackCommand* playback_command) {
    current_ = source_frame;
    direct_present_ready_ = false;
    const auto cpu_start = std::chrono::high_resolution_clock::now();
    cudaEvent_t gpu_start = nullptr;
    cudaEvent_t gpu_stop = nullptr;
    cudaEventCreate(&gpu_start);
    cudaEventCreate(&gpu_stop);
    cudaEventRecord(gpu_start);

    std::vector<InteractiveControl*> controls{tabbar_.get()};
    interaction_.update(input, controls, tabbar_->mutable_config(), config_.glass, playback, playback_command);
    prepared_ = resize_with_letterbox(current_, config_.window.width, config_.window.height);
    if (prepared_.width != config_.window.width || prepared_.height != config_.window.height) {
        log(LogLevel::Warn, "prepared frame size does not match window size");
        cudaEventDestroy(gpu_start);
        cudaEventDestroy(gpu_stop);
        return false;
    }
    pipeline_.upload_background(prepared_.data.data(), prepared_.width, prepared_.height, prepared_.channels);
    pipeline_.render(build_params());
    final_frame_ = pipeline_.download_buffer(GlassBufferId::Final);
    cudaEventRecord(gpu_stop);
    cudaEventSynchronize(gpu_stop);
    float gpu_ms = 0.0f;
    cudaEventElapsedTime(&gpu_ms, gpu_start, gpu_stop);
    cudaEventDestroy(gpu_start);
    cudaEventDestroy(gpu_stop);
    last_gpu_ms_ = gpu_ms;

    viewed_ = build_view_frame();
    build_display_overlay();
    last_cpu_ms_ = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - cpu_start).count();
    last_timing_ = pipeline_.last_timing();
    last_timing_.cpu_ms = last_cpu_ms_;
    last_timing_.gpu_ms = last_gpu_ms_;

    const bool outputs_pending = (config_.input.mode == "image" && !outputs_saved_) || config_.input.mode == "offline_video";
    const InteropFrameRequest interop_request{
        direct_present_supported_ && config_.performance.opengl_interop,
        interaction_.overlay_visible(),
        interaction_.debug_view() == DebugViewMode::Final,
        outputs_pending
    };
    direct_present_ready_ = should_use_cuda_interop_for_frame(interop_request) && pipeline_.display_buffer() != nullptr;

    if (direct_present_ready_ && direct_present_confirmed_) {
        return true;
    }

    if (config_.input.mode == "image") {
        if (!save_image_outputs_once()) {
            return false;
        }
    } else if (config_.input.mode == "offline_video") {
        if (!save_video_outputs_for_frame(frame_index)) {
            return false;
        }
    }
    return true;
}

bool Renderer::tick(const FrameInput& input){ if(!source_) return false; if(!source_->next(current_)) { source_->reset(); if(!source_->next(current_)) return false; }
    return render_frame(current_, input, -1, nullptr, nullptr);
}
}
