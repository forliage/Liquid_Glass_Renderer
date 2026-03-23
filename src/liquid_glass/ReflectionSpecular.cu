#include "liquid_glass/GlassPipeline.h"
#include <cuda_runtime.h>
#include <cmath>

namespace lg {

namespace {

__device__ float cached_load(const float* ptr) {
    return __ldg(ptr);
}

__device__ float edge_weight_device(float sdf, const GlassParams& p) {
    const float sigma = fmaxf(1e-3f, p.edge_sigma);
    return expf(-(sdf * sdf) / (2.0f * sigma * sigma));
}

__device__ void reflection_terms(
    GlassParams p,
    const float* sdf,
    size_t sdf_pitch,
    const float* normal,
    size_t normal_pitch,
    int x,
    int y,
    float* fresnel_rgb,
    float* alpha) {
    const float* sdf_row = reinterpret_cast<const float*>(reinterpret_cast<const char*>(sdf) + y * sdf_pitch);
    const float signed_distance = cached_load(sdf_row + x);
    const float* n = reinterpret_cast<const float*>(reinterpret_cast<const char*>(normal) + y * normal_pitch) + x * 4;
    const float nz = cached_load(n + 2);
    const float edge = edge_weight_device(signed_distance, p);
    const float cos_theta = fmaxf(0.0f, nz);
    const float f0 = 0.04f;
    const float fresnel = (f0 + (1.0f - f0) * powf(1.0f - cos_theta, 5.0f)) * p.fresnel_strength;
    const float edge_glow = p.edge_glow_strength * edge * fresnel;
    fresnel_rgb[0] = fminf(1.0f, 0.18f * fresnel + edge_glow);
    fresnel_rgb[1] = fminf(1.0f, 0.20f * fresnel + edge_glow);
    fresnel_rgb[2] = fminf(1.0f, 0.24f * fresnel + edge_glow);
    *alpha = 1.0f;
}

__device__ float specular_term(
    GlassParams p,
    const float* sdf,
    size_t sdf_pitch,
    const float* normal,
    size_t normal_pitch,
    int x,
    int y) {
    const float* sdf_row = reinterpret_cast<const float*>(reinterpret_cast<const char*>(sdf) + y * sdf_pitch);
    const float signed_distance = cached_load(sdf_row + x);
    const float* n = reinterpret_cast<const float*>(reinterpret_cast<const char*>(normal) + y * normal_pitch) + x * 4;
    const float nx = cached_load(n + 0);
    const float ny = cached_load(n + 1);
    const float nz = cached_load(n + 2);
    const float edge = edge_weight_device(signed_distance, p);
    const float fresnel = (0.04f + (1.0f - 0.04f) * powf(1.0f - fmaxf(0.0f, nz), 5.0f)) * p.fresnel_strength;
    const float edge_glow = p.edge_glow_strength * edge * fresnel;

    const float3 light_dir = make_float3(-0.25f, -0.15f, 1.0f);
    const float light_norm = rsqrtf(light_dir.x * light_dir.x + light_dir.y * light_dir.y + light_dir.z * light_dir.z);
    const float3 l = make_float3(light_dir.x * light_norm, light_dir.y * light_norm, light_dir.z * light_norm);
    const float ndl = fmaxf(0.0f, nx * l.x + ny * l.y + nz * l.z);
    return fminf(1.0f, p.specular_strength * powf(ndl, p.specular_power) * (0.35f + 0.65f * edge) + edge_glow * 0.4f);
}

}  // namespace

__global__ void reflection_kernel(
    GlassParams p,
    const float* sdf,
    size_t sdf_pitch,
    const float* normal,
    size_t normal_pitch,
    float* reflection,
    size_t reflection_pitch) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.width || y >= p.height) return;

    if (!p.reflection_enabled) {
        float* refl = reinterpret_cast<float*>(reinterpret_cast<char*>(reflection) + y * reflection_pitch) + x * 4;
        refl[0] = 0.0f;
        refl[1] = 0.0f;
        refl[2] = 0.0f;
        refl[3] = 1.0f;
        return;
    }

    float rgb[3]{};
    float alpha = 1.0f;
    reflection_terms(p, sdf, sdf_pitch, normal, normal_pitch, x, y, rgb, &alpha);

    float* refl = reinterpret_cast<float*>(reinterpret_cast<char*>(reflection) + y * reflection_pitch) + x * 4;
    refl[0] = rgb[0];
    refl[1] = rgb[1];
    refl[2] = rgb[2];
    refl[3] = alpha;
}

__global__ void specular_kernel(
    GlassParams p,
    const float* sdf,
    size_t sdf_pitch,
    const float* normal,
    size_t normal_pitch,
    float* specular,
    size_t specular_pitch) {
    const int step = max(1, p.specular_step);
    const int reduced_x = blockIdx.x * blockDim.x + threadIdx.x;
    const int reduced_y = blockIdx.y * blockDim.y + threadIdx.y;
    const int x0 = reduced_x * step;
    const int y0 = reduced_y * step;
    if (x0 >= p.width || y0 >= p.height) return;

    if (!p.specular_enabled) {
        for (int yy = y0; yy < min(y0 + step, p.height); ++yy) {
            for (int xx = x0; xx < min(x0 + step, p.width); ++xx) {
                float* spec = reinterpret_cast<float*>(reinterpret_cast<char*>(specular) + yy * specular_pitch) + xx * 4;
                spec[0] = 0.0f;
                spec[1] = 0.0f;
                spec[2] = 0.0f;
                spec[3] = 1.0f;
            }
        }
        return;
    }

    const int sample_x = min(p.width - 1, x0 + step / 2);
    const int sample_y = min(p.height - 1, y0 + step / 2);
    const float highlight = specular_term(p, sdf, sdf_pitch, normal, normal_pitch, sample_x, sample_y);

    for (int yy = y0; yy < min(y0 + step, p.height); ++yy) {
        for (int xx = x0; xx < min(x0 + step, p.width); ++xx) {
            float* spec = reinterpret_cast<float*>(reinterpret_cast<char*>(specular) + yy * specular_pitch) + xx * 4;
            spec[0] = highlight;
            spec[1] = highlight;
            spec[2] = highlight;
            spec[3] = 1.0f;
        }
    }
}

__global__ void reflection_specular_fused_kernel(
    GlassParams p,
    const float* sdf,
    size_t sdf_pitch,
    const float* normal,
    size_t normal_pitch,
    float* reflection,
    size_t reflection_pitch,
    float* specular,
    size_t specular_pitch) {
    const int step = max(1, p.specular_step);
    const int reduced_x = blockIdx.x * blockDim.x + threadIdx.x;
    const int reduced_y = blockIdx.y * blockDim.y + threadIdx.y;
    const int x0 = reduced_x * step;
    const int y0 = reduced_y * step;
    if (x0 >= p.width || y0 >= p.height) return;

    float highlight = 0.0f;
    if (p.specular_enabled) {
        const int sample_x = min(p.width - 1, x0 + step / 2);
        const int sample_y = min(p.height - 1, y0 + step / 2);
        highlight = specular_term(p, sdf, sdf_pitch, normal, normal_pitch, sample_x, sample_y);
    }

    for (int yy = y0; yy < min(y0 + step, p.height); ++yy) {
        for (int xx = x0; xx < min(x0 + step, p.width); ++xx) {
            float* refl = reinterpret_cast<float*>(reinterpret_cast<char*>(reflection) + yy * reflection_pitch) + xx * 4;
            if (p.reflection_enabled) {
                float rgb[3]{};
                float alpha = 1.0f;
                reflection_terms(p, sdf, sdf_pitch, normal, normal_pitch, xx, yy, rgb, &alpha);
                refl[0] = rgb[0];
                refl[1] = rgb[1];
                refl[2] = rgb[2];
                refl[3] = alpha;
            } else {
                refl[0] = 0.0f;
                refl[1] = 0.0f;
                refl[2] = 0.0f;
                refl[3] = 1.0f;
            }

            float* spec = reinterpret_cast<float*>(reinterpret_cast<char*>(specular) + yy * specular_pitch) + xx * 4;
            spec[0] = highlight;
            spec[1] = highlight;
            spec[2] = highlight;
            spec[3] = 1.0f;
        }
    }
}

void launch_reflection(const GlassParams& p, const GpuImage& sdf, const GpuImage& normal, GpuImage& reflection, cudaStream_t stream) {
    dim3 bs(16, 16);
    dim3 gs((p.width + 15) / 16, (p.height + 15) / 16);
    reflection_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const float*>(sdf.ptr),
        sdf.pitch,
        reinterpret_cast<const float*>(normal.ptr),
        normal.pitch,
        reinterpret_cast<float*>(reflection.ptr),
        reflection.pitch);
}

void launch_specular(const GlassParams& p, const GpuImage& sdf, const GpuImage& normal, GpuImage& specular, cudaStream_t stream) {
    const int step = max(1, p.specular_step);
    const int reduced_width = (p.width + step - 1) / step;
    const int reduced_height = (p.height + step - 1) / step;
    dim3 bs(16, 16);
    dim3 gs((reduced_width + 15) / 16, (reduced_height + 15) / 16);
    specular_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const float*>(sdf.ptr),
        sdf.pitch,
        reinterpret_cast<const float*>(normal.ptr),
        normal.pitch,
        reinterpret_cast<float*>(specular.ptr),
        specular.pitch);
}

void launch_reflection_specular_fused(
    const GlassParams& p,
    const GpuImage& sdf,
    const GpuImage& normal,
    GpuImage& reflection,
    GpuImage& specular,
    cudaStream_t stream) {
    const int step = max(1, p.specular_step);
    const int reduced_width = (p.width + step - 1) / step;
    const int reduced_height = (p.height + step - 1) / step;
    dim3 bs(16, 16);
    dim3 gs((reduced_width + 15) / 16, (reduced_height + 15) / 16);
    reflection_specular_fused_kernel<<<gs, bs, 0, stream>>>(
        p,
        reinterpret_cast<const float*>(sdf.ptr),
        sdf.pitch,
        reinterpret_cast<const float*>(normal.ptr),
        normal.pitch,
        reinterpret_cast<float*>(reflection.ptr),
        reflection.pitch,
        reinterpret_cast<float*>(specular.ptr),
        specular.pitch);
}

}
