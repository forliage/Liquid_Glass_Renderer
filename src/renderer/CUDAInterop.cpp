#include "renderer/CUDAInterop.h"
#include "utils/Logger.h"
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <cuda_runtime.h>
#if defined(LG_ENABLE_OPENGL_INTEROP)
#include <cuda_gl_interop.h>
#endif
#include <sstream>

namespace lg {

struct CUDAInterop::Impl {
    GLuint pbo = 0;
    GLuint texture = 0;
#if defined(LG_ENABLE_OPENGL_INTEROP)
    cudaGraphicsResource* resource = nullptr;
#endif
};

namespace {

#if defined(LG_ENABLE_OPENGL_INTEROP)
std::string gl_error_string(GLenum error) {
    std::ostringstream oss;
    oss << "OpenGL error 0x" << std::hex << error;
    return oss.str();
}
#endif

bool has_current_gl_context() {
    return glGetString(GL_VERSION) != nullptr;
}

}  // namespace

CUDAInterop::~CUDAInterop() {
    shutdown();
}

bool CUDAInterop::initialize(int width, int height) {
    shutdown();
    status_ = {};
    status_.width = width;
    status_.height = height;

    if (width <= 0 || height <= 0) {
        status_.last_error = "invalid interop surface size";
        return false;
    }
    if (!has_current_gl_context()) {
        status_.last_error = "no current OpenGL context for CUDA interop";
        return false;
    }
#if !defined(LG_ENABLE_OPENGL_INTEROP)
    status_.last_error = "LG_ENABLE_OPENGL_INTEROP is disabled";
    return false;
#else
    impl_ = new Impl();

    glGenBuffers(1, &impl_->pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, impl_->pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(width) * height * 4, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glGenTextures(1, &impl_->texture);
    glBindTexture(GL_TEXTURE_2D, impl_->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    const GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        status_.last_error = gl_error_string(gl_error);
        shutdown();
        return false;
    }

    const cudaError_t register_status = cudaGraphicsGLRegisterBuffer(&impl_->resource, impl_->pbo, cudaGraphicsRegisterFlagsWriteDiscard);
    if (register_status != cudaSuccess) {
        status_.last_error = std::string("cudaGraphicsGLRegisterBuffer: ") + cudaGetErrorString(register_status);
        shutdown();
        return false;
    }

    status_.initialized = true;
    status_.registered_with_cuda = true;
    status_.mode = "cuda_gl_pbo";
    status_.last_error.clear();
    return true;
#endif
}

bool CUDAInterop::resize(int width, int height) {
    if (active() && status_.width == width && status_.height == height) {
        return true;
    }
    return initialize(width, height);
}

bool CUDAInterop::upload_from_device(const GpuImage& rgba) {
    status_.last_upload_succeeded = false;
    if (!active()) {
        status_.last_error = "interop is not active";
        return false;
    }
    if (rgba.format != GpuPixelFormat::U8 || rgba.channels < 4) {
        status_.last_error = "interop expects an RGBA8 device buffer";
        return false;
    }
    if (rgba.width != status_.width || rgba.height != status_.height) {
        if (!resize(rgba.width, rgba.height)) {
            return false;
        }
    }

#if !defined(LG_ENABLE_OPENGL_INTEROP)
    status_.last_error = "LG_ENABLE_OPENGL_INTEROP is disabled";
    return false;
#else
    cudaError_t cuda_status = cudaSuccess;
    cuda_status = cudaGraphicsMapResources(1, &impl_->resource);
    if (cuda_status != cudaSuccess) {
        status_.last_error = std::string("cudaGraphicsMapResources: ") + cudaGetErrorString(cuda_status);
        return false;
    }

    void* mapped_ptr = nullptr;
    size_t mapped_size = 0;
    cuda_status = cudaGraphicsResourceGetMappedPointer(&mapped_ptr, &mapped_size, impl_->resource);
    if (cuda_status == cudaSuccess) {
        cuda_status = cudaMemcpy2D(
            mapped_ptr,
            static_cast<size_t>(status_.width) * 4,
            rgba.ptr,
            rgba.pitch,
            static_cast<size_t>(status_.width) * 4,
            status_.height,
            cudaMemcpyDeviceToDevice);
    }
    const cudaError_t unmap_status = cudaGraphicsUnmapResources(1, &impl_->resource);
    if (cuda_status != cudaSuccess) {
        status_.last_error = std::string("cuda interop upload: ") + cudaGetErrorString(cuda_status);
        return false;
    }
    if (unmap_status != cudaSuccess) {
        status_.last_error = std::string("cudaGraphicsUnmapResources: ") + cudaGetErrorString(unmap_status);
        return false;
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, impl_->pbo);
    glBindTexture(GL_TEXTURE_2D, impl_->texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, status_.width, status_.height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    const GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        status_.last_error = gl_error_string(gl_error);
        return false;
    }

    status_.last_upload_succeeded = true;
    ++status_.upload_count;
    status_.last_error.clear();
    return true;
#endif
}

void CUDAInterop::render(int fb_width, int fb_height) const {
    if (!active()) {
        return;
    }

    glViewport(0, 0, fb_width, fb_height);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, impl_->texture);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(1.f, 1.f, 1.f, 1.f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 0.f); glVertex2f(-1.f,  1.f);
    glTexCoord2f(1.f, 0.f); glVertex2f( 1.f,  1.f);
    glTexCoord2f(1.f, 1.f); glVertex2f( 1.f, -1.f);
    glTexCoord2f(0.f, 1.f); glVertex2f(-1.f, -1.f);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void CUDAInterop::shutdown() {
    if (!impl_) {
        status_.initialized = false;
        status_.registered_with_cuda = false;
        return;
    }

#if defined(LG_ENABLE_OPENGL_INTEROP)
    if (impl_->resource) {
        cudaGraphicsUnregisterResource(impl_->resource);
        impl_->resource = nullptr;
    }
#endif
    if (impl_->texture != 0) {
        glDeleteTextures(1, &impl_->texture);
    }
    if (impl_->pbo != 0) {
        glDeleteBuffers(1, &impl_->pbo);
    }
    delete impl_;
    impl_ = nullptr;
    status_.initialized = false;
    status_.registered_with_cuda = false;
}

}
