#include "io/VideoEncoder.h"
#include <filesystem>

#if defined(LG_WITH_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace lg {

VideoEncoder::~VideoEncoder() {
    close();
}

#if defined(LG_WITH_FFMPEG)

namespace {

AVRational fps_rational(double fps) {
    AVRational rate = av_d2q(fps > 0.0 ? fps : 30.0, 1000);
    if (rate.num <= 0 || rate.den <= 0) {
        rate = AVRational{30, 1};
    }
    return rate;
}

bool drain_encoder(AVCodecContext* codec_ctx, AVStream* stream, AVFormatContext* format_ctx, AVPacket* packet) {
    while (true) {
        const int receive_status = avcodec_receive_packet(codec_ctx, packet);
        if (receive_status == AVERROR(EAGAIN) || receive_status == AVERROR_EOF) {
            return true;
        }
        if (receive_status < 0) {
            return false;
        }
        av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
        packet->stream_index = stream->index;
        if (av_interleaved_write_frame(format_ctx, packet) < 0) {
            av_packet_unref(packet);
            return false;
        }
        av_packet_unref(packet);
    }
}

}  // namespace

bool VideoEncoder::open(const std::string& path, int width, int height, double fps) {
    close();
    if (path.empty() || width <= 0 || height <= 0) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    width_ = width;
    height_ = height;
    fps_ = fps > 0.0 ? fps : 30.0;
    next_pts_ = 0;
    path_ = path;

    if (avformat_alloc_output_context2(&format_ctx_, nullptr, nullptr, path.c_str()) < 0 || !format_ctx_) {
        close();
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!codec) {
        close();
        return false;
    }

    stream_ = avformat_new_stream(format_ctx_, nullptr);
    codec_ctx_ = avcodec_alloc_context3(codec);
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!stream_ || !codec_ctx_ || !packet_ || !frame_) {
        close();
        return false;
    }

    const AVRational framerate = fps_rational(fps_);
    codec_ctx_->codec_id = codec->id;
    codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx_->time_base = av_inv_q(framerate);
    codec_ctx_->framerate = framerate;
    codec_ctx_->gop_size = 12;
    codec_ctx_->max_b_frames = 0;
    codec_ctx_->bit_rate = 4'000'000;

    if (format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        close();
        return false;
    }

    stream_->time_base = codec_ctx_->time_base;
    if (avcodec_parameters_from_context(stream_->codecpar, codec_ctx_) < 0) {
        close();
        return false;
    }

    frame_->format = codec_ctx_->pix_fmt;
    frame_->width = width_;
    frame_->height = height_;
    if (av_frame_get_buffer(frame_, 32) < 0) {
        close();
        return false;
    }

    sws_ctx_ = sws_getContext(
        width_,
        height_,
        AV_PIX_FMT_RGBA,
        width_,
        height_,
        codec_ctx_->pix_fmt,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!sws_ctx_) {
        close();
        return false;
    }

    if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&format_ctx_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
            close();
            return false;
        }
    }

    if (avformat_write_header(format_ctx_, nullptr) < 0) {
        close();
        return false;
    }

    is_open_ = true;
    return true;
}

bool VideoEncoder::write(const CpuFrame& frame) {
    if (!is_open_ || !codec_ctx_ || !frame_ || !packet_ || !sws_ctx_) {
        return false;
    }
    if (frame.width != width_ || frame.height != height_ || frame.channels < 4) {
        return false;
    }

    if (av_frame_make_writable(frame_) < 0) {
        return false;
    }

    const uint8_t* src_slices[4]{frame.data.data(), nullptr, nullptr, nullptr};
    const int src_linesize[4]{frame.width * frame.channels, 0, 0, 0};
    sws_scale(
        sws_ctx_,
        src_slices,
        src_linesize,
        0,
        height_,
        frame_->data,
        frame_->linesize);

    frame_->pts = next_pts_++;
    if (avcodec_send_frame(codec_ctx_, frame_) < 0) {
        return false;
    }
    return drain_encoder(codec_ctx_, stream_, format_ctx_, packet_);
}

void VideoEncoder::close() {
    if (codec_ctx_ && packet_ && is_open_) {
        avcodec_send_frame(codec_ctx_, nullptr);
        drain_encoder(codec_ctx_, stream_, format_ctx_, packet_);
    }

    if (format_ctx_) {
        av_write_trailer(format_ctx_);
    }
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (frame_) {
        av_frame_free(&frame_);
    }
    if (packet_) {
        av_packet_free(&packet_);
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    if (format_ctx_) {
        if (!(format_ctx_->oformat->flags & AVFMT_NOFILE) && format_ctx_->pb) {
            avio_closep(&format_ctx_->pb);
        }
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
    }

    stream_ = nullptr;
    width_ = 0;
    height_ = 0;
    fps_ = 0.0;
    next_pts_ = 0;
    is_open_ = false;
    path_.clear();
}

#else

bool VideoEncoder::open(const std::string& path, int width, int height, double fps) {
    (void)width;
    (void)height;
    (void)fps;
    path_ = path;
    is_open_ = true;
    return true;
}

bool VideoEncoder::write(const CpuFrame& frame) {
    (void)frame;
    return is_open_;
}

void VideoEncoder::close() {
    path_.clear();
    width_ = 0;
    height_ = 0;
    fps_ = 0.0;
    next_pts_ = 0;
    is_open_ = false;
}

#endif

}
