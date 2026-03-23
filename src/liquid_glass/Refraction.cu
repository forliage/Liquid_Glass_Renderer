#include "liquid_glass/GlassPipeline.h"
#include <cuda_runtime.h>
#include <cmath>

namespace lg {

namespace {

__device__ float edge_weight_device(float sdf, const GlassParams& p) {
    const float sigma = fmaxf(1e-3f, p.edge_sigma);
    return expf(-(sdf * sdf) / (2.0f * sigma * sigma));
}

__device__ float4 fetch_bg_rgba(const unsigned char* bg, size_t bg_pitch, int x, int y) {
    const unsigned char* src = reinterpret_cast<const unsigned char*>(reinterpret_cast<const char*>(bg) + y * bg_pitch) + x * 4;
    return make_float4(src[0] / 255.0f, src[1] / 255.0f, src[2] / 255.0f, src[3] / 255.0f);
}

__device__ float4 sample_bg_bilinear(const unsigned char* bg, size_t bg_pitch, int width, int height, float x, float y) {
    x = fminf(fmaxf(x, 0.0f), width - 1.0f);
    y = fminf(fmaxf(y, 0.0f), height - 1.0f);
    const int x0 = static_cast<int>(floorf(x));
    const int y0 = static_cast<int>(floorf(y));
    const int x1 = min(width - 1, x0 + 1);
    const int y1 = min(height - 1, y0 + 1);
    const float tx = x - x0;
    const float ty = y - y0;

    const float4 c00 = fetch_bg_rgba(bg, bg_pitch, x0, y0);
    const float4 c10 = fetch_bg_rgba(bg, bg_pitch, x1, y0);
    const float4 c01 = fetch_bg_rgba(bg, bg_pitch, x0, y1);
    const float4 c11 = fetch_bg_rgba(bg, bg_pitch, x1, y1);

    const float4 top = make_float4(
        c00.x + (c10.x - c00.x) * tx,
        c00.y + (c10.y - c00.y) * tx,
        c00.z + (c10.z - c00.z) * tx,
        c00.w + (c10.w - c00.w) * tx);
    const float4 bottom = make_float4(
        c01.x + (c11.x - c01.x) * tx,
        c01.y + (c11.y - c01.y) * tx,
        c01.z + (c11.z - c01.z) * tx,
        c01.w + (c11.w - c01.w) * tx);
    return make_float4(
        top.x + (bottom.x - top.x) * ty,
        top.y + (bottom.y - top.y) * ty,
        top.z + (bottom.z - top.z) * ty,
        top.w + (bottom.w - top.w) * ty);
}

__device__ float4 sample_bg_blur(const unsigned char* bg, size_t bg_pitch, int width, int height, float x, float y, float radius) {
    const float offsets[] = {-1.0f, 0.0f, 1.0f};
    const float weights[] = {1.0f, 2.0f, 1.0f};
    float4 accum = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    float total = 0.0f;
    for (int oy = 0; oy < 3; ++oy) {
        for (int ox = 0; ox < 3; ++ox) {
            const float weight = weights[ox] * weights[oy];
            const float4 sample = sample_bg_bilinear(bg, bg_pitch, width, height, x + offsets[ox] * radius, y + offsets[oy] * radius);
            accum.x += sample.x * weight;
            accum.y += sample.y * weight;
            accum.z += sample.z * weight;
            accum.w += sample.w * weight;
            total += weight;
        }
    }
    const float inv_total = total > 0.0f ? 1.0f / total : 1.0f;
    return make_float4(accum.x * inv_total, accum.y * inv_total, accum.z * inv_total, accum.w * inv_total);
}

}  // namespace

__global__ void refraction_field_kernel(
    GlassParams p,
    const float* sdf,
    size_t sdf_pitch,
    const float* thickness,
    size_t thickness_pitch,
    const float* normal,
    size_t normal_pitch,
    float* disp,
    size_t disp_pitch) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    if (!p.refraction_enabled) {
        float* out = reinterpret_cast<float*>(reinterpret_cast<char*>(disp) + y * disp_pitch) + x * 4;
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }

    const float signed_distance = reinterpret_cast<const float*>(reinterpret_cast<const char*>(sdf) + y * sdf_pitch)[x];
    const float height = reinterpret_cast<const float*>(reinterpret_cast<const char*>(thickness) + y * thickness_pitch)[x];
    const float* n = reinterpret_cast<const float*>(reinterpret_cast<const char*>(normal) + y * normal_pitch) + x * 4;
    const float edge = edge_weight_device(signed_distance, p);
    float dx = p.refraction_strength * edge * n[0] + p.center_strength * height * n[0];
    float dy = p.refraction_strength * edge * n[1] + p.center_strength * height * n[1];

    const float limit = fmaxf(1e-3f, p.displacement_limit);
    const float guard = fminf(1.0f, fmaxf(0.0f, p.jacobian_guard));
    const float gradient_risk = fminf(1.0f, fmaxf(0.0f, n[3]));
    const float raw_magnitude = sqrtf(dx * dx + dy * dy);
    const float normalized = raw_magnitude / limit;
    if (normalized > guard) {
        const float t = (normalized - guard) / fmaxf(1e-3f, 1.0f - guard);
        const float atten = 1.0f - 0.75f * fminf(1.0f, t);
        dx *= atten;
        dy *= atten;
    }

    const float magnitude = sqrtf(dx * dx + dy * dy);
    if (magnitude > limit) {
        const float scale = limit / fmaxf(1e-3f, magnitude);
        dx *= scale;
        dy *= scale;
    }
    const float risk = fminf(1.0f, fmaxf(normalized, 0.35f * gradient_risk + 0.65f * edge * normalized));

    float* out = reinterpret_cast<float*>(reinterpret_cast<char*>(disp) + y * disp_pitch) + x * 4;
    out[0] = dx;
    out[1] = dy;
    out[2] = risk;
    out[3] = 1.0f;
}

__global__ void refraction_sample_kernel(
    GlassParams p,
    const unsigned char* bg,
    size_t bg_pitch,
    const float* thickness,
    size_t thickness_pitch,
    const float* disp,
    size_t disp_pitch,
    float* refracted,
    size_t refracted_pitch) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    float* out = reinterpret_cast<float*>(reinterpret_cast<char*>(refracted) + y * refracted_pitch) + x * 4;

    if (p.blur_background_only) {
        const float blur_radius = fmaxf(1.0f, 0.35f * p.h0);
        const float4 blurred = sample_bg_blur(bg, bg_pitch, p.width, p.height, static_cast<float>(x), static_cast<float>(y), blur_radius);
        out[0] = blurred.x;
        out[1] = blurred.y;
        out[2] = blurred.z;
        out[3] = 1.0f;
        return;
    }

    if (!p.refraction_enabled) {
        const float4 sampled = sample_bg_bilinear(bg, bg_pitch, p.width, p.height, static_cast<float>(x), static_cast<float>(y));
        out[0] = sampled.x;
        out[1] = sampled.y;
        out[2] = sampled.z;
        out[3] = 1.0f;
        return;
    }

    const float height = reinterpret_cast<const float*>(reinterpret_cast<const char*>(thickness) + y * thickness_pitch)[x];
    const float* d = reinterpret_cast<const float*>(reinterpret_cast<const char*>(disp) + y * disp_pitch) + x * 4;
    const float4 sampled = sample_bg_bilinear(bg, bg_pitch, p.width, p.height, x + d[0], y + d[1]);
    out[0] = sampled.x * expf(-p.absorb[0] * height);
    out[1] = sampled.y * expf(-p.absorb[1] * height);
    out[2] = sampled.z * expf(-p.absorb[2] * height);
    out[3] = 1.0f;
}

void launch_refraction(const GlassParams& p, const GpuImage& sdf, const GpuImage& thickness, const GpuImage& normal, GpuImage& disp, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    refraction_field_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const float*>(sdf.ptr),
        sdf.pitch,
        reinterpret_cast<const float*>(thickness.ptr),
        thickness.pitch,
        reinterpret_cast<const float*>(normal.ptr),
        normal.pitch,
        reinterpret_cast<float*>(disp.ptr),
        disp.pitch);
}

void launch_sample_refracted(const GlassParams& p, const GpuImage& bg, const GpuImage& thickness, const GpuImage& disp, GpuImage& refracted, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    refraction_sample_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const unsigned char*>(bg.ptr),
        bg.pitch,
        reinterpret_cast<const float*>(thickness.ptr),
        thickness.pitch,
        reinterpret_cast<const float*>(disp.ptr),
        disp.pitch,
        reinterpret_cast<float*>(refracted.ptr),
        refracted.pitch);
}

}
