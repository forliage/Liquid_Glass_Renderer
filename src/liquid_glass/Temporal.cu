#include "liquid_glass/GlassPipeline.h"
#include <cuda_runtime.h>

namespace lg {

__global__ void temporal_kernel(
    GlassParams p,
    const float* current,
    size_t current_pitch,
    float* history,
    size_t history_pitch,
    float* out,
    size_t out_pitch,
    int channels) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    const float* cur = reinterpret_cast<const float*>(reinterpret_cast<const char*>(current) + y * current_pitch) + x * channels;
    float* hist = reinterpret_cast<float*>(reinterpret_cast<char*>(history) + y * history_pitch) + x * channels;
    float* dst = reinterpret_cast<float*>(reinterpret_cast<char*>(out) + y * out_pitch) + x * channels;

    if (!p.temporal_enabled) {
        for (int c = 0; c < channels; ++c) {
            dst[c] = cur[c];
            hist[c] = cur[c];
        }
        if (channels >= 4) {
            dst[3] = 1.0f;
            hist[3] = 1.0f;
        }
        return;
    }

    const bool bootstrapped = channels >= 4 ? hist[3] > 0.0f : true;
    const float alpha = bootstrapped ? p.temporal_alpha : 1.0f;
    for (int c = 0; c < channels; ++c) {
        const float value = bootstrapped ? ((1.0f - alpha) * hist[c] + alpha * cur[c]) : cur[c];
        dst[c] = value;
        hist[c] = value;
    }
    if (channels >= 4) {
        dst[3] = 1.0f;
        hist[3] = 1.0f;
    }
}

void launch_temporal(const GlassParams& p, const GpuImage& current, GpuImage& history, GpuImage& out, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    temporal_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const float*>(current.ptr),
        current.pitch,
        reinterpret_cast<float*>(history.ptr),
        history.pitch,
        reinterpret_cast<float*>(out.ptr),
        out.pitch,
        current.channels);
}

}
