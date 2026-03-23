#pragma once
#include "core/Timer.h"
#include "liquid_glass/GlassParams.h"
#include "io/FrameSource.h"
#include <cstdint>

struct CUstream_st;
using cudaStream_t = CUstream_st*;
namespace lg {
enum class GlassBufferId {
    Background,
    Mask,
    Sdf,
    Thickness,
    Normal,
    Disp,
    Refracted,
    Reflection,
    Specular,
    Legibility,
    History,
    Final
};

class GlassPipeline {
public:
    struct Impl;
    GlassPipeline() = default;
    ~GlassPipeline();
    bool initialize(int width, int height);
    void resize(int width, int height);
    void upload_background(const uint8_t* rgba, int width, int height, int channels);
    void render(const GlassParams& params);
    CpuFrame download_buffer(GlassBufferId buffer);
    uint8_t* download_final();
    const FrameTiming& last_timing() const;
    const GpuImage* display_buffer() const;
private:
    Impl* impl_=nullptr;
};

void launch_build_thickness(const GlassParams&, const GpuImage& mask, GpuImage& sdf, GpuImage& thickness, cudaStream_t stream = nullptr);
void launch_build_normal(const GlassParams&, const GpuImage& thickness, GpuImage& normal, cudaStream_t stream = nullptr);
void launch_refraction(const GlassParams&, const GpuImage& sdf, const GpuImage& thickness, const GpuImage& normal, GpuImage& disp, cudaStream_t stream = nullptr);
void launch_sample_refracted(const GlassParams&, const GpuImage& bg, const GpuImage& thickness, const GpuImage& disp, GpuImage& refracted, cudaStream_t stream = nullptr);
void launch_reflection(const GlassParams&, const GpuImage& sdf, const GpuImage& normal, GpuImage& reflection, cudaStream_t stream = nullptr);
void launch_specular(const GlassParams&, const GpuImage& sdf, const GpuImage& normal, GpuImage& specular, cudaStream_t stream = nullptr);
void launch_reflection_specular_fused(const GlassParams&, const GpuImage& sdf, const GpuImage& normal, GpuImage& reflection, GpuImage& specular, cudaStream_t stream = nullptr);
void launch_legibility(const GlassParams&, const GpuImage& refracted, GpuImage& legibility, cudaStream_t stream = nullptr);
void launch_temporal(const GlassParams&, const GpuImage& current, GpuImage& history, GpuImage& out, cudaStream_t stream = nullptr);
void launch_compose(const GlassParams&, const GpuImage& bg, const GpuImage& mask, const GpuImage& refracted, const GpuImage& reflection, const GpuImage& specular, const GpuImage& legibility, GpuImage& out, cudaStream_t stream = nullptr);
void launch_pack_rgba8(const GpuImage& src, GpuImage& dst, cudaStream_t stream = nullptr);
}
