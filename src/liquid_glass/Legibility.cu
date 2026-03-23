#include "liquid_glass/GlassPipeline.h"
#include "liquid_glass/ShapeSDF.h"
#include <cuda_runtime.h>
#include <cmath>

namespace lg {

namespace {

__device__ float cached_load(const float* ptr) {
    return __ldg(ptr);
}

__device__ int clamp_tile_index(int value, int count) {
    return max(0, min(value, count - 1));
}

__device__ LegibilityResponse sample_legibility_tile(
    const GlassParams& p,
    const float* refracted,
    size_t refracted_pitch,
    int tile_ix,
    int tile_iy,
    int tile_size,
    int step,
    int tile_count_x,
    int tile_count_y) {
    const int clamped_ix = clamp_tile_index(tile_ix, tile_count_x);
    const int clamped_iy = clamp_tile_index(tile_iy, tile_count_y);
    const int tile_x = clamped_ix * tile_size;
    const int tile_y = clamped_iy * tile_size;
    const int x_end = min(p.width, tile_x + tile_size);
    const int y_end = min(p.height, tile_y + tile_size);

    float mean_luma = 0.0f;
    float mean_luma_sq = 0.0f;
    int count = 0;
    for (int sy = tile_y; sy < y_end; sy += step) {
        for (int sx = tile_x; sx < x_end; sx += step) {
            const float* pixel = reinterpret_cast<const float*>(reinterpret_cast<const char*>(refracted) + sy * refracted_pitch) + sx * 4;
            const float luma = 0.2126f * cached_load(pixel + 0) + 0.7152f * cached_load(pixel + 1) + 0.0722f * cached_load(pixel + 2);
            mean_luma += luma;
            mean_luma_sq += luma * luma;
            ++count;
        }
    }

    if (count <= 0) {
        return {};
    }

    mean_luma /= static_cast<float>(count);
    mean_luma_sq /= static_cast<float>(count);
    const float variance = fmaxf(0.0f, mean_luma_sq - mean_luma * mean_luma);
    return compute_legibility_response(mean_luma, variance, p);
}

}  // namespace

__global__ void legibility_kernel(GlassParams p, const float* refracted, size_t refracted_pitch, float* legibility, size_t legibility_pitch) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    float* out = reinterpret_cast<float*>(reinterpret_cast<char*>(legibility) + y * legibility_pitch) + x * 4;

    if (!p.legibility_enabled) {
        out[0] = 1.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = 0.0f;
        return;
    }

    const int step = max(1, p.legibility_step);
    const int tile_size = max(1, p.tile_size);
    const int tile_count_x = max(1, (p.width + tile_size - 1) / tile_size);
    const int tile_count_y = max(1, (p.height + tile_size - 1) / tile_size);

    const float tile_space_x = (static_cast<float>(x) + 0.5f) / static_cast<float>(tile_size) - 0.5f;
    const float tile_space_y = (static_cast<float>(y) + 0.5f) / static_cast<float>(tile_size) - 0.5f;
    const int tile_x0 = static_cast<int>(floorf(tile_space_x));
    const int tile_y0 = static_cast<int>(floorf(tile_space_y));
    const float tx = clamp_legibility_scalar(tile_space_x - floorf(tile_space_x), 0.0f, 1.0f);
    const float ty = clamp_legibility_scalar(tile_space_y - floorf(tile_space_y), 0.0f, 1.0f);

    const LegibilityResponse c00 = sample_legibility_tile(p, refracted, refracted_pitch, tile_x0, tile_y0, tile_size, step, tile_count_x, tile_count_y);
    const LegibilityResponse c10 = sample_legibility_tile(p, refracted, refracted_pitch, tile_x0 + 1, tile_y0, tile_size, step, tile_count_x, tile_count_y);
    const LegibilityResponse c01 = sample_legibility_tile(p, refracted, refracted_pitch, tile_x0, tile_y0 + 1, tile_size, step, tile_count_x, tile_count_y);
    const LegibilityResponse c11 = sample_legibility_tile(p, refracted, refracted_pitch, tile_x0 + 1, tile_y0 + 1, tile_size, step, tile_count_x, tile_count_y);
    const LegibilityResponse blended = bilerp_legibility_response(c00, c10, c01, c11, tx, ty);

    out[0] = blended.adapt_scale;
    out[1] = blended.tint_mix;
    out[2] = blended.protect;
    out[3] = blended.variance;
}

void launch_legibility(const GlassParams& p, const GpuImage& refracted, GpuImage& legibility, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    legibility_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const float*>(refracted.ptr),
        refracted.pitch,
        reinterpret_cast<float*>(legibility.ptr),
        legibility.pitch);
}

}
