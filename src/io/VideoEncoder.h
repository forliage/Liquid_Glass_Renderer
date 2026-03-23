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
class VideoEncoder {
public:
    VideoEncoder() = default;
    ~VideoEncoder();
    bool open(const std::string& path, int width, int height, double fps);
    bool write(const CpuFrame& frame);
    void close();
private:
    std::string path_;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 0.0;
    int64_t next_pts_ = 0;
    bool is_open_ = false;

#if defined(LG_WITH_FFMPEG)
    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
#endif
};
}
