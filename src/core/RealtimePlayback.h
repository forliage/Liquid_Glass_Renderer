#pragma once
#include "io/FrameSource.h"
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace lg {

struct RealtimeSessionStats {
    size_t decoded_frames = 0;
    size_t rendered_frames = 0;
    size_t dropped_frames = 0;
    size_t interaction_commands = 0;
    double last_presented_pts = 0.0;
    double buffered_pts = 0.0;
    bool paused = false;
    bool eof = false;
};

class RealtimeFrameQueue {
public:
    explicit RealtimeFrameQueue(size_t capacity = 4);

    void set_capacity(size_t capacity);
    void push(CpuFrame frame);
    bool pop_latest(CpuFrame& frame, size_t* dropped_older = nullptr);
    void clear();
    size_t size() const;
    bool empty() const;

private:
    mutable std::mutex mutex_{};
    std::deque<CpuFrame> frames_{};
    size_t capacity_ = 4;
};

bool realtime_session_healthy(const RealtimeSessionStats& stats);
std::string summarize_realtime_session(const RealtimeSessionStats& stats);

}
