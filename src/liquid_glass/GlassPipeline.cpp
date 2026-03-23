#include "liquid_glass/GlassPipeline.h"
#include "core/Timer.h"
#include "liquid_glass/ExecutionPlan.h"
#include <cuda_runtime.h>
#include <algorithm>
#include <cstring>
#include "utils/Logger.h"
#include <vector>
namespace lg {
struct GlassPipeline::Impl {
    GpuImage bg, mask, sdf, thickness, normal, disp_raw, disp, refracted, reflection, specular_raw, specular, legibility_raw, legibility, final_raw, final, present;
    std::vector<uint8_t> host;
    GlassParams last_params{};
    FrameTiming last_timing{};
    cudaStream_t stream = nullptr;
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t graph_exec = nullptr;
    uint64_t graph_signature = 0;
    bool graph_valid = false;
    bool graph_disabled = false;
};

static bool check_cuda(cudaError_t status, const char* context) {
    if (status == cudaSuccess) {
        return true;
    }
    log(LogLevel::Error, std::string(context) + ": " + cudaGetErrorString(status));
    return false;
}

static GpuImage alloc_u8(int w,int h,int c){
    GpuImage img;
    img.width=w;
    img.height=h;
    img.channels=c;
    img.format=GpuPixelFormat::U8;
    if (!check_cuda(cudaMallocPitch(&img.ptr,&img.pitch,w*c,h), "cudaMallocPitch(u8)")) {
        img.ptr = nullptr;
        return img;
    }
    if (!check_cuda(cudaMemset2D(img.ptr,img.pitch,0,w*c,h), "cudaMemset2D(u8)")) {
        cudaFree(img.ptr);
        img.ptr = nullptr;
    }
    return img;
}

static GpuImage alloc_f32(int w,int h,int c){
    GpuImage img;
    img.width=w;
    img.height=h;
    img.channels=c;
    img.format=GpuPixelFormat::F32;
    if (!check_cuda(cudaMallocPitch(&img.ptr,&img.pitch,w*c*sizeof(float),h), "cudaMallocPitch(f32)")) {
        img.ptr = nullptr;
        return img;
    }
    if (!check_cuda(cudaMemset2D(img.ptr,img.pitch,0,w*c*sizeof(float),h), "cudaMemset2D(f32)")) {
        cudaFree(img.ptr);
        img.ptr = nullptr;
    }
    return img;
}

static void free_img(GpuImage& img){ if(img.ptr) cudaFree(img.ptr); img={}; }

static void destroy_graph(GlassPipeline::Impl& impl) {
    if (impl.graph_exec) {
        cudaGraphExecDestroy(impl.graph_exec);
        impl.graph_exec = nullptr;
    }
    if (impl.graph) {
        cudaGraphDestroy(impl.graph);
        impl.graph = nullptr;
    }
    impl.graph_signature = 0;
    impl.graph_valid = false;
}

GlassPipeline::~GlassPipeline(){
    if(!impl_) return;
    destroy_graph(*impl_);
    if (impl_->stream) {
        cudaStreamDestroy(impl_->stream);
    }
    free_img(impl_->bg); free_img(impl_->mask); free_img(impl_->sdf); free_img(impl_->thickness); free_img(impl_->normal); free_img(impl_->disp_raw); free_img(impl_->disp); free_img(impl_->refracted); free_img(impl_->reflection); free_img(impl_->specular_raw); free_img(impl_->specular); free_img(impl_->legibility_raw); free_img(impl_->legibility); free_img(impl_->final_raw); free_img(impl_->final); free_img(impl_->present); delete impl_;
}
bool GlassPipeline::initialize(int width, int height){
    int device_count = 0;
    if (!check_cuda(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount") || device_count <= 0) {
        return false;
    }
    if (!check_cuda(cudaSetDevice(0), "cudaSetDevice")) {
        return false;
    }
    impl_=new Impl();
    if (!check_cuda(cudaStreamCreate(&impl_->stream), "cudaStreamCreate")) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    resize(width,height);
    return impl_->bg.ptr && impl_->final.ptr;
}
void GlassPipeline::resize(int width, int height){
    if(!impl_) return;
    destroy_graph(*impl_);
    for (GpuImage* p: {&impl_->bg,&impl_->mask,&impl_->sdf,&impl_->thickness,&impl_->normal,&impl_->disp_raw,&impl_->disp,&impl_->refracted,&impl_->reflection,&impl_->specular_raw,&impl_->specular,&impl_->legibility_raw,&impl_->legibility,&impl_->final_raw,&impl_->final,&impl_->present}) free_img(*p);
    impl_->bg=alloc_u8(width,height,4);
    impl_->mask=alloc_u8(width,height,1);
    impl_->sdf=alloc_f32(width,height,1);
    impl_->thickness=alloc_f32(width,height,1);
    impl_->normal=alloc_f32(width,height,4);
    impl_->disp_raw=alloc_f32(width,height,4);
    impl_->disp=alloc_f32(width,height,4);
    impl_->refracted=alloc_f32(width,height,4);
    impl_->reflection=alloc_f32(width,height,4);
    impl_->specular_raw=alloc_f32(width,height,4);
    impl_->specular=alloc_f32(width,height,4);
    impl_->legibility_raw=alloc_f32(width,height,4);
    impl_->legibility=alloc_f32(width,height,4);
    impl_->final_raw=alloc_f32(width,height,4);
    impl_->final=alloc_f32(width,height,4);
    impl_->present=alloc_u8(width,height,4);
    impl_->host.assign(width*height*4,0);
}
void GlassPipeline::upload_background(const uint8_t* rgba,int width,int height,int channels){ if(!impl_||channels<4) return; check_cuda(cudaMemcpy2D(impl_->bg.ptr,impl_->bg.pitch,rgba,width*4,width*4,height,cudaMemcpyHostToDevice), "cudaMemcpy2D(background)"); }

template <typename Fn>
static void profile_cuda_pass(FrameTiming& frame_timing, const std::string& name, cudaStream_t stream, Fn&& fn) {
    CpuTimer cpu_timer;
    cpu_timer.tic("pass");

    cudaEvent_t start = nullptr;
    cudaEvent_t stop = nullptr;
    float gpu_ms = 0.0f;

    const bool has_start = cudaEventCreate(&start) == cudaSuccess;
    const bool has_stop = cudaEventCreate(&stop) == cudaSuccess;
    if (has_start && has_stop) {
        cudaGetLastError();
        cudaEventRecord(start, stream);
    }

    fn();

    if (has_start && has_stop) {
        cudaEventRecord(stop, stream);
        cudaEventSynchronize(stop);
        cudaEventElapsedTime(&gpu_ms, start, stop);
        cudaEventDestroy(start);
        cudaEventDestroy(stop);
    } else {
        cudaStreamSynchronize(stream);
    }

    const std::string context = "GlassPipeline::render/" + name;
    check_cuda(cudaGetLastError(), context.c_str());
    frame_timing.passes.push_back({name, cpu_timer.toc_ms("pass"), gpu_ms});
}

static void launch_pipeline_sequence(
    const GlassParams& params,
    const GlassExecutionPlan& plan,
    GlassPipeline::Impl& impl,
    cudaStream_t stream) {
    launch_build_thickness(params, impl.mask, impl.sdf, impl.thickness, stream);
    launch_build_normal(params, impl.thickness, impl.normal, stream);
    launch_refraction(params, impl.sdf, impl.thickness, impl.normal, impl.disp_raw, stream);
    launch_temporal(params, impl.disp_raw, impl.disp, impl.disp, stream);
    launch_sample_refracted(params, impl.bg, impl.thickness, impl.disp, impl.refracted, stream);
    if (plan.fuse_reflection_specular) {
        launch_reflection_specular_fused(params, impl.sdf, impl.normal, impl.reflection, impl.specular_raw, stream);
    } else {
        launch_reflection(params, impl.sdf, impl.normal, impl.reflection, stream);
        launch_specular(params, impl.sdf, impl.normal, impl.specular_raw, stream);
    }
    launch_temporal(params, impl.specular_raw, impl.specular, impl.specular, stream);
    launch_legibility(params, impl.refracted, impl.legibility_raw, stream);
    launch_temporal(params, impl.legibility_raw, impl.legibility, impl.legibility, stream);
    launch_compose(params, impl.bg, impl.mask, impl.refracted, impl.reflection, impl.specular, impl.legibility, impl.final_raw, stream);
    launch_temporal(params, impl.final_raw, impl.final, impl.final, stream);
    launch_pack_rgba8(impl.final, impl.present, stream);
}

static bool build_cuda_graph(
    GlassPipeline::Impl& impl,
    const GlassParams& params,
    const GlassExecutionPlan& plan) {
    destroy_graph(impl);
    if (!check_cuda(cudaStreamBeginCapture(impl.stream, cudaStreamCaptureModeGlobal), "cudaStreamBeginCapture")) {
        return false;
    }
    launch_pipeline_sequence(params, plan, impl, impl.stream);
    if (!check_cuda(cudaStreamEndCapture(impl.stream, &impl.graph), "cudaStreamEndCapture")) {
        destroy_graph(impl);
        return false;
    }
    if (!check_cuda(cudaGraphInstantiate(&impl.graph_exec, impl.graph, nullptr, nullptr, 0), "cudaGraphInstantiate")) {
        destroy_graph(impl);
        return false;
    }
    impl.graph_signature = glass_execution_signature(params, plan);
    impl.graph_valid = true;
    log(LogLevel::Info, "captured CUDA graph for steady-state glass pipeline");
    return true;
}

void GlassPipeline::render(const GlassParams& params){
    if(!impl_) return;
    impl_->last_params = params;
    impl_->last_timing = {};
    const GlassExecutionPlan plan = resolve_glass_execution_plan(params);
    if (plan.use_cuda_graph && !impl_->graph_disabled) {
        if (should_rebuild_cuda_graph(impl_->graph_signature, params, plan, impl_->graph_valid) &&
            !build_cuda_graph(*impl_, params, plan)) {
            impl_->graph_disabled = true;
            log(LogLevel::Warn, "CUDA graph path disabled after capture failure; falling back to stream launches");
        }
        if (impl_->graph_valid) {
            CpuTimer cpu_timer;
            cpu_timer.tic("graph");
            cudaEvent_t start = nullptr;
            cudaEvent_t stop = nullptr;
            float gpu_ms = 0.0f;
            const bool has_start = cudaEventCreate(&start) == cudaSuccess;
            const bool has_stop = cudaEventCreate(&stop) == cudaSuccess;
            if (has_start && has_stop) {
                cudaEventRecord(start, impl_->stream);
            }
            cudaGraphLaunch(impl_->graph_exec, impl_->stream);
            if (has_start && has_stop) {
                cudaEventRecord(stop, impl_->stream);
                cudaEventSynchronize(stop);
                cudaEventElapsedTime(&gpu_ms, start, stop);
                cudaEventDestroy(start);
                cudaEventDestroy(stop);
            } else {
                cudaStreamSynchronize(impl_->stream);
            }
            impl_->last_timing.cpu_ms = cpu_timer.toc_ms("graph");
            impl_->last_timing.gpu_ms = gpu_ms;
            impl_->last_timing.passes.push_back({"graph_launch", impl_->last_timing.cpu_ms, impl_->last_timing.gpu_ms});
            check_cuda(cudaGetLastError(), "GlassPipeline::render/graph_launch");
            return;
        }
    }

    profile_cuda_pass(impl_->last_timing, "thickness", impl_->stream, [&] { launch_build_thickness(params, impl_->mask, impl_->sdf, impl_->thickness, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "normal", impl_->stream, [&] { launch_build_normal(params, impl_->thickness, impl_->normal, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "refraction_field", impl_->stream, [&] { launch_refraction(params, impl_->sdf, impl_->thickness, impl_->normal, impl_->disp_raw, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "temporal_disp", impl_->stream, [&] { launch_temporal(params, impl_->disp_raw, impl_->disp, impl_->disp, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "refracted", impl_->stream, [&] { launch_sample_refracted(params, impl_->bg, impl_->thickness, impl_->disp, impl_->refracted, impl_->stream); });
    if (plan.fuse_reflection_specular) {
        profile_cuda_pass(impl_->last_timing, "reflection_specular_fused", impl_->stream, [&] {
            launch_reflection_specular_fused(params, impl_->sdf, impl_->normal, impl_->reflection, impl_->specular_raw, impl_->stream);
        });
    } else {
        profile_cuda_pass(impl_->last_timing, "reflection", impl_->stream, [&] { launch_reflection(params, impl_->sdf, impl_->normal, impl_->reflection, impl_->stream); });
        profile_cuda_pass(impl_->last_timing, "specular", impl_->stream, [&] { launch_specular(params, impl_->sdf, impl_->normal, impl_->specular_raw, impl_->stream); });
    }
    profile_cuda_pass(impl_->last_timing, "temporal_specular", impl_->stream, [&] { launch_temporal(params, impl_->specular_raw, impl_->specular, impl_->specular, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "legibility", impl_->stream, [&] { launch_legibility(params, impl_->refracted, impl_->legibility_raw, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "temporal_legibility", impl_->stream, [&] { launch_temporal(params, impl_->legibility_raw, impl_->legibility, impl_->legibility, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "compose", impl_->stream, [&] { launch_compose(params, impl_->bg, impl_->mask, impl_->refracted, impl_->reflection, impl_->specular, impl_->legibility, impl_->final_raw, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "temporal_final", impl_->stream, [&] { launch_temporal(params, impl_->final_raw, impl_->final, impl_->final, impl_->stream); });
    profile_cuda_pass(impl_->last_timing, "present_pack", impl_->stream, [&] { launch_pack_rgba8(impl_->final, impl_->present, impl_->stream); });

    impl_->last_timing.gpu_ms = 0.0;
    for (const PassTiming& pass : impl_->last_timing.passes) {
        impl_->last_timing.cpu_ms += pass.cpu_ms;
        impl_->last_timing.gpu_ms += pass.gpu_ms;
    }
    check_cuda(cudaGetLastError(), "GlassPipeline::render");
}
CpuFrame GlassPipeline::download_buffer(GlassBufferId buffer){
    CpuFrame frame;
    if(!impl_) return frame;
    GpuImage* image = nullptr;
    switch (buffer) {
        case GlassBufferId::Background: image = &impl_->bg; break;
        case GlassBufferId::Mask: image = &impl_->mask; break;
        case GlassBufferId::Sdf: image = &impl_->sdf; break;
        case GlassBufferId::Thickness: image = &impl_->thickness; break;
        case GlassBufferId::Normal: image = &impl_->normal; break;
        case GlassBufferId::Disp: image = &impl_->disp; break;
        case GlassBufferId::Refracted: image = &impl_->refracted; break;
        case GlassBufferId::Reflection: image = &impl_->reflection; break;
        case GlassBufferId::Specular: image = &impl_->specular; break;
        case GlassBufferId::Legibility: image = &impl_->legibility; break;
        case GlassBufferId::History: image = &impl_->final; break;
        case GlassBufferId::Final: image = &impl_->present; break;
    }
    if(!image || !image->ptr) return frame;
    frame.width = image->width;
    frame.height = image->height;
    frame.channels = image->channels;
    if (image->format == GpuPixelFormat::U8) {
        frame.data.assign(static_cast<size_t>(image->width * image->height * image->channels), 0);
        check_cuda(cudaMemcpy2D(frame.data.data(), image->width * image->channels, image->ptr, image->pitch, image->width * image->channels, image->height, cudaMemcpyDeviceToHost), "cudaMemcpy2D(download u8)");
        return frame;
    }

    std::vector<float> scratch(static_cast<size_t>(image->width * image->height * image->channels), 0.0f);
    check_cuda(cudaMemcpy2D(scratch.data(), image->width * image->channels * sizeof(float), image->ptr, image->pitch, image->width * image->channels * sizeof(float), image->height, cudaMemcpyDeviceToHost), "cudaMemcpy2D(download f32)");
    frame.data.assign(static_cast<size_t>(image->width * image->height * image->channels), 0);
    const float safe_h0 = std::max(1.0f, impl_->last_params.h0 * (1.0f + impl_->last_params.edge_boost));
    const float safe_limit = std::max(1.0f, impl_->last_params.displacement_limit);
    for (int i = 0; i < image->width * image->height; ++i) {
        if (image->channels == 1) {
            float value = scratch[static_cast<size_t>(i)];
            if (buffer == GlassBufferId::Sdf) {
                value = 0.5f + 0.5f * value / std::max(impl_->last_params.radius, 1.0f);
            } else if (buffer == GlassBufferId::Thickness) {
                value = value / safe_h0;
            }
            frame.data[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            continue;
        }

        const size_t base = static_cast<size_t>(i) * image->channels;
        float r = scratch[base + 0];
        float g = scratch[base + 1];
        float b = scratch[base + 2];
        float a = image->channels > 3 ? scratch[base + 3] : 1.0f;
        if (buffer == GlassBufferId::Normal) {
            r = 0.5f + 0.5f * r;
            g = 0.5f + 0.5f * g;
            b = 0.5f + 0.5f * b;
        } else if (buffer == GlassBufferId::Disp) {
            r = 0.5f + 0.5f * r / safe_limit;
            g = 0.5f + 0.5f * g / safe_limit;
            b = std::clamp(b, 0.0f, 1.0f);
        } else if (buffer == GlassBufferId::Legibility) {
            r = std::clamp(r * 0.5f, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b, 0.0f, 1.0f);
        }
        frame.data[base + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        frame.data[base + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        frame.data[base + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        frame.data[base + 3] = static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    }
    return frame;
}
uint8_t* GlassPipeline::download_final(){ if(!impl_) return nullptr; CpuFrame frame = download_buffer(GlassBufferId::Final); impl_->host = std::move(frame.data); return impl_->host.data(); }
const FrameTiming& GlassPipeline::last_timing() const {
    static const FrameTiming empty{};
    return impl_ ? impl_->last_timing : empty;
}
const GpuImage* GlassPipeline::display_buffer() const {
    return (impl_ && impl_->present.ptr) ? &impl_->present : nullptr;
}
}
