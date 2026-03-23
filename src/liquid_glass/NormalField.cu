#include "liquid_glass/GlassPipeline.h"
#include <cuda_runtime.h>
#include <cmath>

namespace lg {

namespace {

__device__ float sample_thickness(const float* thickness, size_t thickness_pitch, int width, int height, int x, int y) {
    x = max(0, min(width - 1, x));
    y = max(0, min(height - 1, y));
    return reinterpret_cast<const float*>(reinterpret_cast<const char*>(thickness) + y * thickness_pitch)[x];
}

}  // namespace

__global__ void normal_kernel(GlassParams p, const float* thickness, size_t thickness_pitch, float* normal, size_t normal_pitch) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    const float hL = sample_thickness(thickness, thickness_pitch, p.width, p.height, x - 1, y);
    const float hR = sample_thickness(thickness, thickness_pitch, p.width, p.height, x + 1, y);
    const float hD = sample_thickness(thickness, thickness_pitch, p.width, p.height, x, y - 1);
    const float hU = sample_thickness(thickness, thickness_pitch, p.width, p.height, x, y + 1);
    float nx = -(hR - hL);
    float ny = -(hU - hD);
    float nz = 1.0f;
    const float inv = rsqrtf(nx * nx + ny * ny + nz * nz);
    nx *= inv;
    ny *= inv;
    nz *= inv;

    float* out = reinterpret_cast<float*>(reinterpret_cast<char*>(normal) + y * normal_pitch) + x * 4;
    out[0] = nx;
    out[1] = ny;
    out[2] = nz;
    out[3] = fminf(1.0f, sqrtf((hR - hL) * (hR - hL) + (hU - hD) * (hU - hD)));
}

void launch_build_normal(const GlassParams& p, const GpuImage& thickness, GpuImage& normal, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    normal_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const float*>(thickness.ptr),
        thickness.pitch,
        reinterpret_cast<float*>(normal.ptr),
        normal.pitch);
}

}
