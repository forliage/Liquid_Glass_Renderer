#pragma once
#include "core/RealtimePlayback.h"
#include "renderer/GLDisplay.h"
#include "renderer/Renderer.h"
#include "core/Config.h"
#include "io/FrameSource.h"
#include "io/VideoDecoder.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace lg {
class Engine {
public:
    ~Engine();
    bool initialize(const AppConfig& cfg);
    int run();
private:
    int run_image_headless();
    int run_interactive();
    int run_offline_video();
    int run_realtime_video();
    void start_realtime_decoder();
    void stop_realtime_decoder();
    void realtime_decoder_loop();
    void apply_playback_command(const PlaybackCommand& command);
    PlaybackUiState build_playback_ui_state() const;
    CpuFrame make_realtime_placeholder_frame() const;

    GLDisplay display_{};
    Renderer renderer_{};
    AppConfig cfg_{};
    std::shared_ptr<IFrameSource> source_{};
    std::shared_ptr<VideoDecoder> realtime_decoder_{};
    RealtimeFrameQueue realtime_queue_{4};
    std::thread decode_thread_{};
    mutable std::mutex realtime_mutex_{};
    std::condition_variable realtime_cv_{};
    RealtimeSessionStats realtime_stats_{};
    bool realtime_stop_ = false;
    bool realtime_pause_ = false;
    bool realtime_loop_ = true;
    bool realtime_seek_pending_ = false;
    double realtime_seek_target_seconds_ = 0.0;
    bool realtime_eof_ = false;
    double realtime_presented_pts_ = 0.0;
    double realtime_buffered_pts_ = 0.0;
    size_t realtime_dropped_frames_ = 0;
    CpuFrame realtime_last_frame_{};
};
}
