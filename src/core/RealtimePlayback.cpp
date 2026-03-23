#include "core/RealtimePlayback.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace lg {

RealtimeFrameQueue::RealtimeFrameQueue(size_t capacity) : capacity_(std::max<size_t>(1, capacity)) {}

void RealtimeFrameQueue::set_capacity(size_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    capacity_ = std::max<size_t>(1, capacity);
    while (frames_.size() > capacity_) {
        frames_.pop_front();
    }
}

void RealtimeFrameQueue::push(CpuFrame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    while (frames_.size() >= capacity_) {
        frames_.pop_front();
    }
    frames_.push_back(std::move(frame));
}

bool RealtimeFrameQueue::pop_latest(CpuFrame& frame, size_t* dropped_older) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frames_.empty()) {
        if (dropped_older) {
            *dropped_older = 0;
        }
        return false;
    }
    if (dropped_older) {
        *dropped_older = frames_.size() > 0 ? frames_.size() - 1 : 0;
    }
    frame = std::move(frames_.back());
    frames_.clear();
    return true;
}

void RealtimeFrameQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
}

size_t RealtimeFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.size();
}

bool RealtimeFrameQueue::empty() const {
    return size() == 0;
}

bool realtime_session_healthy(const RealtimeSessionStats& stats) {
    if (stats.rendered_frames == 0 || stats.decoded_frames == 0) {
        return false;
    }
    return stats.last_presented_pts > 0.0 || stats.buffered_pts > 0.0 || stats.eof;
}

std::string summarize_realtime_session(const RealtimeSessionStats& stats) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3)
        << "realtime session: decoded_frames=" << stats.decoded_frames
        << ", rendered_frames=" << stats.rendered_frames
        << ", dropped_frames=" << stats.dropped_frames
        << ", interaction_commands=" << stats.interaction_commands
        << ", last_pts=" << stats.last_presented_pts
        << ", buffered_pts=" << stats.buffered_pts
        << ", paused=" << (stats.paused ? "true" : "false")
        << ", eof=" << (stats.eof ? "true" : "false");
    return oss.str();
}

}
