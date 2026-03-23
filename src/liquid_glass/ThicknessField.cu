#include "liquid_glass/GlassPipeline.h"
#include <cuda_runtime.h>
#include <cmath>

namespace lg {

namespace {

__device__ float round_rect_sdf_device(float x, float y, const GlassParams& p) {
    const float px = fabsf(x - p.cx) - (p.tab_w * 0.5f - p.radius);
    const float py = fabsf(y - p.cy) - (p.tab_h * 0.5f - p.radius);
    const float qx = fmaxf(px, 0.0f);
    const float qy = fmaxf(py, 0.0f);
    return sqrtf(qx * qx + qy * qy) + fminf(fmaxf(px, py), 0.0f) - p.radius;
}

__device__ float capsule_skeleton_distance_device(float x, float y, const GlassParams& p) {
    const float half_width = fmaxf(p.tab_w * 0.5f - p.radius, 0.0f);
    const float clamped = fminf(fmaxf(x - p.cx, -half_width), half_width);
    const float dx = x - (p.cx + clamped);
    const float dy = y - p.cy;
    return sqrtf(dx * dx + dy * dy);
}

__device__ float profile_device(float normalized_radius, const GlassParams& p) {
    const float r = fminf(fmaxf(normalized_radius, 0.0f), 1.6f);
    const float parabola = fmaxf(0.0f, 1.0f - r * r);
    const float super_quad = powf(fmaxf(0.0f, 1.0f - powf(r, p.profile_p)), p.profile_q);
    const float edge_roll = parabola + p.edge_boost * expf(-((1.0f - r) * (1.0f - r)) / 0.02f);

    switch (p.thickness_profile) {
        case 0: return parabola;
        case 1: return super_quad;
        case 2: return fmaxf(0.0f, edge_roll);
        default: return parabola;
    }
}

__device__ float edge_weight_device(float sdf, const GlassParams& p) {
    const float sigma = fmaxf(1e-3f, p.edge_sigma);
    return expf(-(sdf * sdf) / (2.0f * sigma * sigma));
}

}  // namespace

__global__ void thickness_kernel(
    GlassParams p,
    unsigned char* mask,
    size_t mask_pitch,
    float* sdf,
    size_t sdf_pitch,
    float* thickness,
    size_t thickness_pitch) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    const float fx = static_cast<float>(x) + 0.5f;
    const float fy = static_cast<float>(y) + 0.5f;
    const float signed_distance = round_rect_sdf_device(fx, fy, p);
    const float skeleton_distance = capsule_skeleton_distance_device(fx, fy, p);
    const float local_radius = fmaxf(0.5f * p.tab_h, 1.0f);
    const float normalized_radius = skeleton_distance / local_radius;
    const float profile = profile_device(normalized_radius, p);
    const float edge_weight = edge_weight_device(signed_distance, p);
    const float height = fmaxf(0.0f, p.h0 * profile + p.h0 * p.edge_boost * edge_weight * 0.35f);

    reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(mask) + y * mask_pitch)[x] = signed_distance <= 0.0f ? 255 : 0;
    reinterpret_cast<float*>(reinterpret_cast<char*>(sdf) + y * sdf_pitch)[x] = signed_distance;
    reinterpret_cast<float*>(reinterpret_cast<char*>(thickness) + y * thickness_pitch)[x] = signed_distance <= 0.0f ? height : 0.0f;
}

void launch_build_thickness(const GlassParams& p, const GpuImage& mask, GpuImage& sdf, GpuImage& thickness, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    thickness_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<unsigned char*>(mask.ptr),
        mask.pitch,
        reinterpret_cast<float*>(sdf.ptr),
        sdf.pitch,
        reinterpret_cast<float*>(thickness.ptr),
        thickness.pitch);
}

}
