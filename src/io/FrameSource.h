#pragma once
#include <cstdint>
#include <vector>
namespace lg {
struct CpuFrame { int width=0, height=0, channels=4; std::vector<uint8_t> data; double pts=0.0; };
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual bool open(const char* path)=0;
    virtual bool next(CpuFrame& frame)=0;
    virtual void reset()=0;
};
}
