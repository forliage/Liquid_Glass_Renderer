#pragma once
#include "io/FrameSource.h"
#include <string>
namespace lg {
struct AspectFitRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

bool load_image_file_rgba(const std::string& path, CpuFrame& frame, std::string* error = nullptr);
bool save_image_file(const std::string& path, const CpuFrame& frame, std::string* error = nullptr);
AspectFitRect compute_aspect_fit(int src_width, int src_height, int dst_width, int dst_height);
CpuFrame resize_with_letterbox(const CpuFrame& source, int dst_width, int dst_height);

class ImageSource : public IFrameSource {
public:
    bool open(const char* path) override;
    bool next(CpuFrame& frame) override;
    void reset() override;
private:
    CpuFrame frame_{}; bool emitted_=false;
};
}
