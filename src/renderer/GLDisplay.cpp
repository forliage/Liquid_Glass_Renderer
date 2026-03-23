#include "renderer/GLDisplay.h"
#include "utils/Logger.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <sstream>

namespace lg {

namespace {

std::string last_glfw_error_message() {
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    std::ostringstream oss;
    oss << "GLFW error " << code;
    if (description && *description) {
        oss << ": " << description;
    }
    return oss.str();
}

const char* profile_name(const GLContextRequest& request) {
    if (!request.profile_hint) {
        return "legacy";
    }
    switch (request.profile) {
        case GLFW_OPENGL_COMPAT_PROFILE: return "compat";
        case GLFW_OPENGL_CORE_PROFILE: return "core";
        default: return "any";
    }
}

std::string describe_request(const GLContextRequest& request) {
    std::ostringstream oss;
    oss << request.major << "." << request.minor << " " << profile_name(request);
    return oss.str();
}

}  // namespace

std::vector<GLContextRequest> default_gl_context_requests() {
    return {
        {4, 3, GLFW_OPENGL_COMPAT_PROFILE, true},
        {3, 3, GLFW_OPENGL_COMPAT_PROFILE, true},
        {2, 1, 0, false}
    };
}

bool GLDisplay::initialize(int width, int height, const char* title, bool vsync){
    if (!glfwInit()) {
        log(LogLevel::Error, "failed to initialize GLFW");
        return false;
    }

    std::string failures;
    for (const GLContextRequest& request : default_gl_context_requests()) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, request.major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, request.minor);
        if (request.profile_hint) {
            glfwWindowHint(GLFW_OPENGL_PROFILE, request.profile);
        }

        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window_) {
            if (!failures.empty()) {
                failures += "; ";
            }
            failures += describe_request(request) + " -> " + last_glfw_error_message();
            continue;
        }

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(vsync ? 1 : 0);
        log(LogLevel::Info, "GL display initialized with OpenGL context " + describe_request(request));
        return true;
    }

    log(LogLevel::Error, "failed to create OpenGL window; attempts: " + failures);
    glfwTerminate();
    return false;
}

void GLDisplay::shutdown(){ interop_.shutdown(); if(window_){ glfwDestroyWindow(window_); window_=nullptr; } glfwTerminate(); }
bool GLDisplay::should_close() const { return !window_ || glfwWindowShouldClose(window_); }
void GLDisplay::begin_frame(){ glfwPollEvents(); }
void GLDisplay::draw_rgba(const unsigned char* rgba, int width, int height){
    if(!window_ || !rgba || width <= 0 || height <= 0) return;
    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glRasterPos2f(-1.f, 1.f);
    glPixelZoom(static_cast<float>(fb_width) / static_cast<float>(width), -static_cast<float>(fb_height) / static_cast<float>(height));
    glDrawPixels(width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelZoom(1.f, 1.f);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}
bool GLDisplay::draw_device_rgba(const GpuImage& rgba) {
    if (!window_) {
        return false;
    }

    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    if (!interop_.active() && !interop_.initialize(rgba.width, rgba.height)) {
        log(LogLevel::Warn, "CUDA-OpenGL interop initialization failed, falling back to CPU display: " + interop_.status().last_error);
        return false;
    }
    if (!interop_.upload_from_device(rgba)) {
        log(LogLevel::Warn, "CUDA-OpenGL interop upload failed, falling back to CPU display: " + interop_.status().last_error);
        return false;
    }
    if (!interop_logged_success_) {
        log(LogLevel::Info, "present path: cuda_gl_pbo");
        interop_logged_success_ = true;
    }
    interop_.render(fb_width, fb_height);
    return true;
}
void GLDisplay::present(){ glfwSwapBuffers(window_); }
}
