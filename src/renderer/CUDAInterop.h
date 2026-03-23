#pragma once
#include "liquid_glass/GlassParams.h"
#include <string>

namespace lg {

struct CUDAInteropStatus {
    bool initialized = false;
    bool registered_with_cuda = false;
    bool last_upload_succeeded = false;
    int width = 0;
    int height = 0;
    size_t upload_count = 0;
    std::string mode = "cpu_readback";
    std::string last_error{};
};

struct InteropFrameRequest {
    bool supported = false;
    bool overlay_visible = true;
    bool final_view = false;
    bool outputs_pending = true;
};

inline bool should_use_cuda_interop_for_frame(const InteropFrameRequest& request) {
    return request.supported && !request.overlay_visible && request.final_view && !request.outputs_pending;
}

inline bool resolve_cuda_interop_support_after_attempt(
    bool currently_supported,
    bool ever_succeeded,
    bool last_attempt_succeeded) {
    if (!currently_supported) {
        return false;
    }
    if (last_attempt_succeeded) {
        return true;
    }
    return ever_succeeded;
}

class CUDAInterop {
public:
    CUDAInterop() = default;
    ~CUDAInterop();

    bool initialize(int width, int height);
    bool resize(int width, int height);
    bool upload_from_device(const GpuImage& rgba);
    void render(int fb_width, int fb_height) const;
    void shutdown();

    const CUDAInteropStatus& status() const { return status_; }
    bool active() const { return status_.initialized && status_.registered_with_cuda; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    CUDAInteropStatus status_{};
};

}
