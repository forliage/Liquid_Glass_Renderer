#include "io/VideoDecoder.h"
#include <algorithm>
#include <vector>

#if defined(LG_WITH_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}
#endif

namespace lg {

VideoDecoder::~VideoDecoder() {
    close();
}

#if defined(LG_WITH_FFMPEG)

namespace {

double rational_to_double(int num, int den) {
    return den != 0 ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
}

}  // namespace

std::string webcam_device_path(int device_index) {
    return "/dev/video" + std::to_string(device_index);
}

void VideoDecoder::close() {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (packet_) {
        av_packet_free(&packet_);
    }
    if (decoded_frame_) {
        av_frame_free(&decoded_frame_);
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
    }

    stream_index_ = -1;
    decoded_frames_ = 0;
    time_base_num_ = 0;
    time_base_den_ = 1;
    draining_ = false;
    flush_sent_ = false;
    width_ = 0;
    height_ = 0;
    fps_ = 0.0;
    frame_count_ = 0;
    duration_seconds_ = 0.0;
}

bool VideoDecoder::open(const char* path) {
    close();
    if (!path || !*path) {
        return false;
    }

    if (avformat_open_input(&format_ctx_, path, nullptr, nullptr) < 0) {
        close();
        return false;
    }
    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        close();
        return false;
    }

    stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
        close();
        return false;
    }

    return initialize_stream_decoder();
}

bool VideoDecoder::open_webcam(int device_index) {
    close();
    if (device_index < 0) {
        return false;
    }

    const std::string path = webcam_device_path(device_index);
    const AVInputFormat* input_format = av_find_input_format("video4linux2");
    if (!input_format) {
        return false;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "30", 0);
    if (avformat_open_input(&format_ctx_, path.c_str(), input_format, &options) < 0) {
        av_dict_free(&options);
        close();
        return false;
    }
    av_dict_free(&options);
    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        close();
        return false;
    }

    stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
        close();
        return false;
    }

    return initialize_stream_decoder();
}

bool VideoDecoder::initialize_stream_decoder() {
    AVStream* stream = format_ctx_->streams[stream_index_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        close();
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        close();
        return false;
    }
    if (avcodec_parameters_to_context(codec_ctx_, stream->codecpar) < 0) {
        close();
        return false;
    }
    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        close();
        return false;
    }

    decoded_frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!decoded_frame_ || !packet_) {
        close();
        return false;
    }

    width_ = codec_ctx_->width;
    height_ = codec_ctx_->height;
    const AVRational fps_q = av_guess_frame_rate(format_ctx_, stream, nullptr);
    fps_ = av_q2d(fps_q);
    if (!(fps_ > 0.0)) {
        fps_ = av_q2d(stream->avg_frame_rate);
    }
    if (!(fps_ > 0.0)) {
        fps_ = 30.0;
    }
    frame_count_ = stream->nb_frames > 0 ? stream->nb_frames : 0;
    time_base_num_ = stream->time_base.num;
    time_base_den_ = stream->time_base.den;

    if (stream->duration > 0) {
        duration_seconds_ = stream->duration * rational_to_double(time_base_num_, time_base_den_);
    } else if (format_ctx_->duration > 0) {
        duration_seconds_ = static_cast<double>(format_ctx_->duration) / static_cast<double>(AV_TIME_BASE);
    } else if (frame_count_ > 0 && fps_ > 0.0) {
        duration_seconds_ = static_cast<double>(frame_count_) / fps_;
    }

    sws_ctx_ = sws_getContext(
        width_,
        height_,
        codec_ctx_->pix_fmt,
        width_,
        height_,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!sws_ctx_) {
        close();
        return false;
    }

    return true;
}

bool VideoDecoder::next(CpuFrame& frame) {
    if (!format_ctx_ || !codec_ctx_ || !decoded_frame_ || !packet_ || !sws_ctx_) {
        return false;
    }

    while (true) {
        const int receive_status = avcodec_receive_frame(codec_ctx_, decoded_frame_);
        if (receive_status == 0) {
            frame.width = width_;
            frame.height = height_;
            frame.channels = 4;
            frame.data.assign(static_cast<size_t>(width_ * height_ * 4), 0);

            uint8_t* dst_data[4]{};
            int dst_linesize[4]{};
            if (av_image_fill_arrays(dst_data, dst_linesize, frame.data.data(), AV_PIX_FMT_RGBA, width_, height_, 1) < 0) {
                av_frame_unref(decoded_frame_);
                return false;
            }

            sws_scale(
                sws_ctx_,
                decoded_frame_->data,
                decoded_frame_->linesize,
                0,
                height_,
                dst_data,
                dst_linesize);

            int64_t pts = decoded_frame_->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) {
                pts = decoded_frame_->pts;
            }
            frame.pts = pts == AV_NOPTS_VALUE
                ? static_cast<double>(decoded_frames_) / std::max(fps_, 1.0)
                : pts * rational_to_double(time_base_num_, time_base_den_);

            ++decoded_frames_;
            av_frame_unref(decoded_frame_);
            return true;
        }
        if (receive_status == AVERROR_EOF) {
            return false;
        }
        if (receive_status != AVERROR(EAGAIN)) {
            return false;
        }

        if (!draining_) {
            int read_status = av_read_frame(format_ctx_, packet_);
            if (read_status >= 0) {
                if (packet_->stream_index == stream_index_) {
                    const int send_status = avcodec_send_packet(codec_ctx_, packet_);
                    av_packet_unref(packet_);
                    if (send_status < 0 && send_status != AVERROR(EAGAIN)) {
                        return false;
                    }
                } else {
                    av_packet_unref(packet_);
                }
                continue;
            }
            draining_ = true;
        }

        if (draining_ && !flush_sent_) {
            const int flush_status = avcodec_send_packet(codec_ctx_, nullptr);
            flush_sent_ = true;
            if (flush_status < 0 && flush_status != AVERROR_EOF && flush_status != AVERROR(EAGAIN)) {
                return false;
            }
            continue;
        }

        return false;
    }
}

void VideoDecoder::reset() {
    if (!format_ctx_ || stream_index_ < 0 || !codec_ctx_) {
        return;
    }
    av_seek_frame(format_ctx_, stream_index_, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec_ctx_);
    draining_ = false;
    flush_sent_ = false;
    decoded_frames_ = 0;
    if (packet_) {
        av_packet_unref(packet_);
    }
    if (decoded_frame_) {
        av_frame_unref(decoded_frame_);
    }
}

bool VideoDecoder::seek_seconds(double seconds) {
    if (!format_ctx_ || stream_index_ < 0 || !codec_ctx_) {
        return false;
    }
    const double clamped = std::clamp(seconds, 0.0, std::max(duration_seconds_, 0.0));
    const AVRational time_base{time_base_num_, time_base_den_};
    const int64_t timestamp = av_rescale_q(
        static_cast<int64_t>(clamped * static_cast<double>(AV_TIME_BASE)),
        AVRational{1, AV_TIME_BASE},
        time_base);
    if (av_seek_frame(format_ctx_, stream_index_, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }
    avcodec_flush_buffers(codec_ctx_);
    draining_ = false;
    flush_sent_ = false;
    decoded_frames_ = 0;
    if (packet_) {
        av_packet_unref(packet_);
    }
    if (decoded_frame_) {
        av_frame_unref(decoded_frame_);
    }
    return true;
}

#else

void VideoDecoder::close() {
    cursor_ = 0;
    dummy_ = {};
    width_ = 0;
    height_ = 0;
    fps_ = 0.0;
    frame_count_ = 0;
    duration_seconds_ = 0.0;
}

bool VideoDecoder::open(const char* path) {
    (void)path;
    cursor_ = 0;
    width_ = 1280;
    height_ = 720;
    fps_ = 60.0;
    frame_count_ = 601;
    duration_seconds_ = frame_count_ / fps_;
    dummy_.width = width_;
    dummy_.height = height_;
    dummy_.channels = 4;
    dummy_.data.assign(static_cast<size_t>(width_ * height_ * 4), 16);
    return true;
}

bool VideoDecoder::open_webcam(int device_index) {
    if (device_index < 0) {
        return false;
    }
    return open(webcam_device_path(device_index).c_str());
}

bool VideoDecoder::next(CpuFrame& frame) {
    if (cursor_++ > 600) return false;
    frame = dummy_;
    frame.pts = cursor_ / fps_;
    return true;
}

void VideoDecoder::reset() {
    cursor_ = 0;
}

bool VideoDecoder::seek_seconds(double seconds) {
    if (fps_ <= 0.0) {
        return false;
    }
    const double clamped = std::max(0.0, seconds);
    cursor_ = static_cast<int>(clamped * fps_);
    return true;
}

#endif

}
