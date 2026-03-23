#pragma once
#include <chrono>
#include <map>
#include <string>
#include <vector>
namespace lg {

struct PassTiming {
    std::string name;
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
};

struct FrameTiming {
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
    std::vector<PassTiming> passes;
};

class CpuTimer {
public:
    void tic(const std::string& name);
    double toc_ms(const std::string& name);
    void reset();
private:
    std::map<std::string,std::chrono::high_resolution_clock::time_point> starts_;
};

std::string format_frame_timing_summary(const FrameTiming& timing);
}
