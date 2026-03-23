#include "core/Engine.h"
#include "core/Timer.h"
#include "experiments/Benchmark.h"
#include "io/ImageIO.h"
#include "io/VideoEncoder.h"
#include "utils/Logger.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace lg {

namespace {

BenchmarkConfig make_benchmark_config(const AppConfig& cfg, const std::string& label) {
    BenchmarkConfig benchmark{};
    benchmark.label = label;
    benchmark.input_mode = cfg.input.mode;
    benchmark.input_path = cfg.input.path;
    benchmark.performance_mode = cfg.performance.mode;
    benchmark.ablation_variant = cfg.ablation.variant;
    benchmark.width = cfg.window.width;
    benchmark.height = cfg.window.height;
    benchmark.half_res_specular = cfg.performance.half_res_specular;
    benchmark.cuda_graph = cfg.performance.cuda_graph;
    benchmark.ablation_refraction = cfg.ablation.refraction;
    benchmark.ablation_reflection = cfg.ablation.reflection;
    benchmark.ablation_specular = cfg.ablation.specular;
    benchmark.ablation_legibility = cfg.ablation.legibility;
    benchmark.ablation_temporal = cfg.ablation.temporal;
    benchmark.ablation_blur_only = cfg.ablation.blur_only;
    benchmark.specular_downsample = cfg.performance.specular_downsample;
    benchmark.legibility_downsample = cfg.performance.legibility_downsample;
    benchmark.warmup_frames = cfg.performance.warmup_frames;
    benchmark.benchmark_frames = cfg.performance.benchmark_frames;
    return benchmark;
}

}  // namespace

Engine::~Engine() {
    stop_realtime_decoder();
}

bool Engine::initialize(const AppConfig& cfg){
    cfg_=cfg;
    ConfigLoader::ensure_output_directories(cfg_);
    log(LogLevel::Info, "startup config: " + ConfigLoader::describe(cfg_));
    log(LogLevel::Info, "prepared output directories under " + cfg_.output.out_dir);

    if(!renderer_.initialize(cfg)) return false;

    if(cfg.input.mode=="image") {
        source_ = std::make_shared<ImageSource>();
        if(!source_ || !source_->open(cfg.input.path.c_str())) {
            log(LogLevel::Error, "failed to open image input: " + cfg.input.path);
            return false;
        }
        if (!cfg.output.headless && !display_.initialize(cfg.window.width,cfg.window.height,"Liquid Glass Engine", cfg.window.vsync)) {
            log(LogLevel::Error, "failed to initialize GL display for image mode");
            return false;
        }
        renderer_.set_source(source_);
        return true;
    }

    if (cfg.input.mode == "offline_video") {
        source_ = std::make_shared<VideoDecoder>();
        if(!source_ || !source_->open(cfg.input.path.c_str())) {
            log(LogLevel::Error, "failed to open offline video input: " + cfg.input.path);
            return false;
        }
        return true;
    }

    realtime_decoder_ = std::make_shared<VideoDecoder>();
    const bool opened = realtime_decoder_ && (cfg.input.realtime_source == "webcam"
        ? realtime_decoder_->open_webcam(cfg.input.device_index)
        : realtime_decoder_->open(cfg.input.path.c_str()));
    if (!opened) {
        const std::string source = cfg.input.realtime_source == "webcam"
            ? webcam_device_path(cfg.input.device_index)
            : cfg.input.path;
        log(LogLevel::Error, "failed to open realtime video input: " + source);
        return false;
    }
    if(!display_.initialize(cfg.window.width,cfg.window.height,"Liquid Glass Engine", cfg.window.vsync)) {
        log(LogLevel::Error, "failed to initialize GL display for realtime video mode");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(realtime_mutex_);
        realtime_queue_.clear();
        realtime_stats_ = {};
        realtime_stop_ = false;
        realtime_pause_ = false;
        realtime_loop_ = cfg.input.loop;
        realtime_seek_pending_ = false;
        realtime_seek_target_seconds_ = 0.0;
        realtime_eof_ = false;
        realtime_presented_pts_ = 0.0;
        realtime_buffered_pts_ = 0.0;
        realtime_dropped_frames_ = 0;
        realtime_last_frame_ = make_realtime_placeholder_frame();
    }
    start_realtime_decoder();
    return true;
}

int Engine::run(){
    if (cfg_.input.mode == "offline_video") {
        return run_offline_video();
    }
    if (cfg_.input.mode == "realtime_video") {
        return run_realtime_video();
    }
    if (cfg_.output.headless) {
        return run_image_headless();
    }
    return run_interactive();
}

int Engine::run_image_headless() {
    const int max_frames = std::max(1, cfg_.performance.warmup_frames + cfg_.performance.benchmark_frames + 2);
    for (int frame = 0; frame < max_frames; ++frame) {
        if (!renderer_.tick(FrameInput{})) {
            log(LogLevel::Error, "headless image mode failed to render frame " + std::to_string(frame));
            return 1;
        }
        if (renderer_.outputs_saved()) {
            log(LogLevel::Info, "headless image validation complete under " + cfg_.output.out_dir);
            return 0;
        }
    }
    log(LogLevel::Error, "headless image mode exceeded validation frame budget without producing outputs");
    return 1;
}

int Engine::run_interactive() {
    while(!display_.should_close()){
        display_.begin_frame();
        const FrameInput input = capture_input_from_window(display_.window());
        if(renderer_.tick(input)){
            bool drawn = false;
            if (renderer_.can_direct_present()) {
                const GpuImage* device_frame = renderer_.direct_present_buffer();
                drawn = device_frame && display_.draw_device_rgba(*device_frame);
                renderer_.mark_direct_present_result(drawn);
            }
            if (!drawn) {
                const CpuFrame& frame = renderer_.presented_frame();
                display_.draw_rgba(frame.data.data(), frame.width, frame.height);
            }
        }
        display_.present();
    }
    display_.shutdown();
    return 0;
}

int Engine::run_realtime_video() {
    while (!display_.should_close()) {
        display_.begin_frame();
        const FrameInput input = capture_input_from_window(display_.window());

        CpuFrame queued_frame;
        size_t dropped = 0;
        if (realtime_queue_.pop_latest(queued_frame, &dropped)) {
            std::lock_guard<std::mutex> lock(realtime_mutex_);
            realtime_last_frame_ = std::move(queued_frame);
            realtime_presented_pts_ = realtime_last_frame_.pts;
            realtime_dropped_frames_ += dropped;
            realtime_stats_.dropped_frames += dropped;
        }

        PlaybackUiState playback_ui = build_playback_ui_state();
        PlaybackCommand playback_command{};
        if (renderer_.render_frame(realtime_last_frame_, input, -1, &playback_ui, &playback_command)) {
            {
                std::lock_guard<std::mutex> lock(realtime_mutex_);
                ++realtime_stats_.rendered_frames;
                realtime_stats_.last_presented_pts = realtime_presented_pts_;
                realtime_stats_.buffered_pts = realtime_buffered_pts_;
                realtime_stats_.paused = realtime_pause_;
                realtime_stats_.eof = realtime_eof_;
            }
            bool drawn = false;
            if (renderer_.can_direct_present()) {
                const GpuImage* device_frame = renderer_.direct_present_buffer();
                drawn = device_frame && display_.draw_device_rgba(*device_frame);
                renderer_.mark_direct_present_result(drawn);
            }
            if (!drawn) {
                const CpuFrame& frame = renderer_.presented_frame();
                display_.draw_rgba(frame.data.data(), frame.width, frame.height);
            }
        }

        apply_playback_command(playback_command);
        display_.present();
    }

    stop_realtime_decoder();
    RealtimeSessionStats summary{};
    {
        std::lock_guard<std::mutex> lock(realtime_mutex_);
        summary = realtime_stats_;
        summary.last_presented_pts = realtime_presented_pts_;
        summary.buffered_pts = realtime_buffered_pts_;
        summary.paused = realtime_pause_;
        summary.eof = realtime_eof_;
    }
    log(realtime_session_healthy(summary) ? LogLevel::Info : LogLevel::Warn, summarize_realtime_session(summary));
    display_.shutdown();
    return 0;
}

int Engine::run_offline_video() {
    auto decoder = std::dynamic_pointer_cast<VideoDecoder>(source_);
    if (!decoder) {
        log(LogLevel::Error, "offline video mode requires VideoDecoder");
        return 1;
    }

    const std::filesystem::path stem_path = std::filesystem::path(cfg_.input.path).stem();
    const std::string stem = stem_path.empty() ? std::string("video") : stem_path.string();
    const std::filesystem::path output_video_path = std::filesystem::path(cfg_.output.out_dir) / "videos" / (stem + "_liquid_glass.mp4");
    const std::filesystem::path metrics_path = std::filesystem::path(cfg_.output.out_dir) / "videos" / (stem + "_metrics.json");

    VideoEncoder encoder;
    const double input_fps = decoder->fps() > 0.0 ? decoder->fps() : 30.0;
    if (!encoder.open(output_video_path.string(), cfg_.window.width, cfg_.window.height, input_fps)) {
        log(LogLevel::Error, "failed to open video encoder for " + output_video_path.string());
        return 1;
    }

    CpuFrame frame;
    CpuTimer timer;
    CpuTimer measured_timer;
    std::vector<BenchmarkSample> samples;
    int frame_index = 0;
    timer.tic("offline_total");

    while (decoder->next(frame)) {
        if (frame_index == cfg_.performance.warmup_frames) {
            measured_timer.tic("offline_measured");
        }
        if (!renderer_.render_frame(frame, FrameInput{}, frame_index, nullptr, nullptr)) {
            encoder.close();
            log(LogLevel::Error, "offline renderer failed at frame " + std::to_string(frame_index));
            return 1;
        }
        if (!encoder.write(renderer_.final_frame())) {
            encoder.close();
            log(LogLevel::Error, "video encoder write failed at frame " + std::to_string(frame_index));
            return 1;
        }

        samples.push_back({frame_index, frame.pts, renderer_.last_timing(), frame_index < cfg_.performance.warmup_frames});
        ++frame_index;
    }

    encoder.close();

    const double wall_ms = timer.toc_ms("offline_total");
    if (samples.empty()) {
        log(LogLevel::Error, "offline video mode decoded zero frames");
        return 1;
    }

    Benchmark benchmark;
    const BenchmarkResult result = benchmark.build(
        make_benchmark_config(cfg_, stem),
        samples,
        wall_ms,
        samples.size() > static_cast<size_t>(cfg_.performance.warmup_frames) ? measured_timer.toc_ms("offline_measured") : wall_ms,
        input_fps,
        output_video_path.string());
    if (!benchmark.write_json(metrics_path, result)) {
        log(LogLevel::Error, "failed to write offline metrics to " + metrics_path.string());
        return 1;
    }

    std::ostringstream summary;
    summary << "offline video complete: frames=" << result.summary.frame_count
            << ", avg_cpu_ms=" << std::fixed << std::setprecision(3) << result.summary.frame_ms
            << ", avg_gpu_ms=" << result.summary.gpu_frame_ms
            << ", avg_fps=" << result.summary.fps
            << ", video=" << output_video_path.string()
            << ", metrics=" << metrics_path.string();
    log(LogLevel::Info, summary.str());
    return 0;
}

void Engine::start_realtime_decoder() {
    stop_realtime_decoder();
    {
        std::lock_guard<std::mutex> lock(realtime_mutex_);
        realtime_stop_ = false;
    }
    decode_thread_ = std::thread(&Engine::realtime_decoder_loop, this);
}

void Engine::stop_realtime_decoder() {
    {
        std::lock_guard<std::mutex> lock(realtime_mutex_);
        realtime_stop_ = true;
    }
    realtime_cv_.notify_all();
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }
}

void Engine::realtime_decoder_loop() {
    constexpr size_t kQueueCapacity = 4;
    realtime_queue_.set_capacity(kQueueCapacity);

    while (true) {
        double seek_target = -1.0;
        bool paused = false;
        bool loop = false;
        {
            std::unique_lock<std::mutex> lock(realtime_mutex_);
            realtime_cv_.wait_for(lock, std::chrono::milliseconds(4), [&] {
                if (realtime_stop_ || realtime_seek_pending_) {
                    return true;
                }
                if (realtime_eof_) {
                    return realtime_loop_;
                }
                return !realtime_pause_ && realtime_queue_.size() < kQueueCapacity;
            });

            if (realtime_stop_) {
                return;
            }

            if (realtime_seek_pending_) {
                seek_target = realtime_seek_target_seconds_;
                realtime_seek_pending_ = false;
                realtime_eof_ = false;
            }
            paused = realtime_pause_;
            loop = realtime_loop_;

            if (paused && seek_target < 0.0) {
                continue;
            }
            if (realtime_queue_.size() >= kQueueCapacity && seek_target < 0.0) {
                continue;
            }
        }

        if (seek_target >= 0.0) {
            realtime_decoder_->seek_seconds(seek_target);
            realtime_queue_.clear();
        }

        CpuFrame frame;
        if (realtime_decoder_->next(frame)) {
            realtime_queue_.push(frame);
            bool first_decoded = false;
            {
                std::lock_guard<std::mutex> lock(realtime_mutex_);
                first_decoded = realtime_stats_.decoded_frames == 0;
                ++realtime_stats_.decoded_frames;
                realtime_stats_.buffered_pts = frame.pts;
                realtime_buffered_pts_ = frame.pts;
                realtime_eof_ = false;
                realtime_stats_.eof = false;
            }
            if (first_decoded) {
                std::ostringstream oss;
                oss << "realtime decoder primed at pts=" << std::fixed << std::setprecision(3) << frame.pts;
                log(LogLevel::Info, oss.str());
            }
            if (paused) {
                continue;
            }
            realtime_cv_.notify_all();
            continue;
        }

        if (loop) {
            realtime_decoder_->seek_seconds(0.0);
            realtime_queue_.clear();
            {
                std::lock_guard<std::mutex> lock(realtime_mutex_);
                realtime_buffered_pts_ = 0.0;
                realtime_eof_ = false;
                realtime_stats_.buffered_pts = 0.0;
                realtime_stats_.eof = false;
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(realtime_mutex_);
            realtime_eof_ = true;
            realtime_stats_.eof = true;
        }
    }
}

void Engine::apply_playback_command(const PlaybackCommand& command) {
    if (!realtime_decoder_) {
        return;
    }

    bool notify = false;
    bool interaction = false;
    std::lock_guard<std::mutex> lock(realtime_mutex_);
    if (command.toggle_pause) {
        realtime_pause_ = !realtime_pause_;
        realtime_stats_.paused = realtime_pause_;
        notify = true;
        interaction = true;
    }
    if (command.toggle_loop) {
        realtime_loop_ = !realtime_loop_;
        notify = true;
        interaction = true;
    }
    if (command.restart) {
        realtime_seek_pending_ = true;
        realtime_seek_target_seconds_ = 0.0;
        realtime_eof_ = false;
        realtime_stats_.eof = false;
        notify = true;
        interaction = true;
    } else if (command.seek_to_ratio && realtime_decoder_->duration_seconds() > 0.0) {
        realtime_seek_pending_ = true;
        realtime_seek_target_seconds_ = std::clamp(command.seek_ratio, 0.0f, 1.0f) * realtime_decoder_->duration_seconds();
        realtime_eof_ = false;
        realtime_stats_.eof = false;
        notify = true;
        interaction = true;
    } else if (command.seek_direction != 0) {
        realtime_seek_pending_ = true;
        const double delta = command.seek_direction * 5.0;
        realtime_seek_target_seconds_ = std::clamp(realtime_presented_pts_ + delta, 0.0, std::max(realtime_decoder_->duration_seconds(), 0.0));
        realtime_eof_ = false;
        realtime_stats_.eof = false;
        notify = true;
        interaction = true;
    }

    if (command.seek_to_ratio || command.seek_direction != 0 || command.restart) {
        realtime_buffered_pts_ = realtime_seek_target_seconds_;
        realtime_stats_.buffered_pts = realtime_seek_target_seconds_;
    }
    if (interaction) {
        ++realtime_stats_.interaction_commands;
    }

    if (notify) {
        realtime_cv_.notify_all();
    }
}

PlaybackUiState Engine::build_playback_ui_state() const {
    PlaybackUiState state{};
    if (!realtime_decoder_) {
        return state;
    }

    std::lock_guard<std::mutex> lock(realtime_mutex_);
    const double duration = std::max(realtime_decoder_->duration_seconds(), 0.0);
    state.available = true;
    state.paused = realtime_pause_;
    state.loop = realtime_loop_;
    if (duration > 0.0) {
        state.progress = static_cast<float>(std::clamp(realtime_presented_pts_ / duration, 0.0, 1.0));
        state.buffered_progress = static_cast<float>(std::clamp(std::max(realtime_presented_pts_, realtime_buffered_pts_) / duration, 0.0, 1.0));
    }
    return state;
}

CpuFrame Engine::make_realtime_placeholder_frame() const {
    CpuFrame frame;
    frame.width = cfg_.window.width;
    frame.height = cfg_.window.height;
    frame.channels = 4;
    frame.data.assign(static_cast<size_t>(frame.width * frame.height * 4), 0);
    for (int i = 0; i < frame.width * frame.height; ++i) {
        frame.data[static_cast<size_t>(i) * 4 + 3] = 255;
    }
    return frame;
}

}
