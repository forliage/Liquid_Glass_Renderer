#pragma once
#include "liquid_glass/GlassParams.h"
#include "renderer/CUDAInterop.h"
#include <vector>
struct GLFWwindow;
namespace lg {

struct GLContextRequest {
    int major = 0;
    int minor = 0;
    int profile = 0;
    bool profile_hint = false;
};

std::vector<GLContextRequest> default_gl_context_requests();

class GLDisplay {
public:
    bool initialize(int width, int height, const char* title, bool vsync);
    void shutdown();
    bool should_close() const;
    void begin_frame();
    void draw_rgba(const unsigned char* rgba, int width, int height);
    bool draw_device_rgba(const GpuImage& rgba);
    void present();
    GLFWwindow* window() const { return window_; }
    const CUDAInteropStatus& interop_status() const { return interop_.status(); }
private:
    GLFWwindow* window_ = nullptr;
    CUDAInterop interop_{};
    bool interop_logged_success_ = false;
};
}
