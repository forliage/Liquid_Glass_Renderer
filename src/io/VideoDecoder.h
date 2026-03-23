#pragma once
#include "io/FrameSource.h"
#include <cstdint>
#include <string>

#if defined(LG_WITH_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#endif

namespace lg {
std::string webcam_device_path(int device_index);

class VideoDecoder : public IFrameSource {
public:
    VideoDecoder() = default;
    ~VideoDecoder() override;
    bool open(const char* path) override;
    bool open_webcam(int device_index);
    bool next(CpuFrame& frame) override;
    void reset() override;
    bool seek_seconds(double seconds);
    int width() const { return width_; }
    int height() const { return height_; }
    double fps() const { return fps_; }
    int64_t frame_count() const { return frame_count_; }
    double duration_seconds() const { return duration_seconds_; }
private:
    void close();
    bool initialize_stream_decoder();

#if defined(LG_WITH_FFMPEG)
    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* decoded_frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    int stream_index_ = -1;
    int64_t decoded_frames_ = 0;
    int time_base_num_ = 0;
    int time_base_den_ = 1;
    bool draining_ = false;
    bool flush_sent_ = false;
#endif

    int cursor_=0;
    CpuFrame dummy_{};
    int width_=0;
    int height_=0;
    double fps_=0.0;
    int64_t frame_count_=0;
    double duration_seconds_=0.0;
};
}
