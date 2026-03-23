#include "liquid_glass/GlassPipeline.h"
#include <cuda_fp16.h>
#include <cuda_runtime.h>

namespace lg {

namespace {

__device__ float clamp01(float value) {
    return fminf(1.0f, fmaxf(0.0f, value));
}

__device__ float maybe_half(float value, bool enabled) {
    return enabled ? __half2float(__float2half_rn(value)) : value;
}

}  // namespace

__global__ void compose_kernel(
    GlassParams p,
    const unsigned char* bg,
    size_t bg_pitch,
    const unsigned char* mask,
    size_t mask_pitch,
    const float* refracted,
    size_t refracted_pitch,
    const float* reflection,
    size_t reflection_pitch,
    const float* specular,
    size_t specular_pitch,
    const float* legibility,
    size_t legibility_pitch,
    float* out,
    size_t out_pitch) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    const unsigned char* bgp = reinterpret_cast<const unsigned char*>(reinterpret_cast<const char*>(bg) + y * bg_pitch) + x * 4;
    const unsigned char inside = reinterpret_cast<const unsigned char*>(reinterpret_cast<const char*>(mask) + y * mask_pitch)[x];
    float* dst = reinterpret_cast<float*>(reinterpret_cast<char*>(out) + y * out_pitch) + x * 4;

    if (inside == 0) {
        dst[0] = bgp[0] / 255.0f;
        dst[1] = bgp[1] / 255.0f;
        dst[2] = bgp[2] / 255.0f;
        dst[3] = 1.0f;
        return;
    }

    const float* refr = reinterpret_cast<const float*>(reinterpret_cast<const char*>(refracted) + y * refracted_pitch) + x * 4;
    const float* refl = reinterpret_cast<const float*>(reinterpret_cast<const char*>(reflection) + y * reflection_pitch) + x * 4;
    const float* spec = reinterpret_cast<const float*>(reinterpret_cast<const char*>(specular) + y * specular_pitch) + x * 4;
    const float* leg = reinterpret_cast<const float*>(reinterpret_cast<const char*>(legibility) + y * legibility_pitch) + x * 4;
    const bool mixed_precision = p.specular_step > 1 || p.legibility_step > 1;

    const float adapt = maybe_half(p.legibility_enabled ? clamp01(leg[0]) : 1.0f, mixed_precision);
    const float tint_mix = maybe_half(p.legibility_enabled ? clamp01(leg[1]) : 0.0f, mixed_precision);
    const float protect = maybe_half(p.legibility_enabled ? clamp01(leg[2]) : 0.0f, mixed_precision);

    const float tint_weight = maybe_half(tint_mix * (1.0f - 0.65f * protect), mixed_precision);
    const float reflection_weight = maybe_half(0.30f + 0.35f * (1.0f - protect), mixed_precision);
    const float specular_weight = maybe_half(0.12f + 0.38f * (1.0f - protect), mixed_precision);

    for (int c = 0; c < 3; ++c) {
        const float base = maybe_half(clamp01(refr[c] * adapt), mixed_precision);
        const float tinted = maybe_half(base * (1.0f - tint_weight) + p.tint[c] * tint_weight, mixed_precision);
        const float layered = maybe_half(tinted + refl[c] * reflection_weight + spec[c] * specular_weight, mixed_precision);
        dst[c] = clamp01(maybe_half(layered * (1.0f - 0.2f * protect) + base * 0.2f * protect, mixed_precision));
    }
    dst[3] = 1.0f;
}

void launch_compose(
    const GlassParams& p,
    const GpuImage& bg,
    const GpuImage& mask,
    const GpuImage& refracted,
    const GpuImage& reflection,
    const GpuImage& specular,
    const GpuImage& legibility,
    GpuImage& out,
    cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    compose_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const unsigned char*>(bg.ptr),
        bg.pitch,
        reinterpret_cast<const unsigned char*>(mask.ptr),
        mask.pitch,
        reinterpret_cast<const float*>(refracted.ptr),
        refracted.pitch,
        reinterpret_cast<const float*>(reflection.ptr),
        reflection.pitch,
        reinterpret_cast<const float*>(specular.ptr),
        specular.pitch,
        reinterpret_cast<const float*>(legibility.ptr),
        legibility.pitch,
        reinterpret_cast<float*>(out.ptr),
        out.pitch);
}

__global__ void pack_rgba8_kernel(
    const float* src,
    size_t src_pitch,
    unsigned char* dst,
    size_t dst_pitch,
    int width,
    int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float* pixel = reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + y * src_pitch) + x * 4;
    unsigned char* out = reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(dst) + y * dst_pitch) + x * 4;
    out[0] = static_cast<unsigned char>(clamp01(pixel[0]) * 255.0f);
    out[1] = static_cast<unsigned char>(clamp01(pixel[1]) * 255.0f);
    out[2] = static_cast<unsigned char>(clamp01(pixel[2]) * 255.0f);
    out[3] = static_cast<unsigned char>(clamp01(pixel[3]) * 255.0f);
}

void launch_pack_rgba8(const GpuImage& src, GpuImage& dst, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((src.width + 15) / 16, (src.height + 15) / 16);
    pack_rgba8_kernel<<<gs, bs, 0, stream>>>(
        reinterpret_cast<const float*>(src.ptr),
        src.pitch,
        reinterpret_cast<unsigned char*>(dst.ptr),
        dst.pitch,
        src.width,
        src.height);
}

}
