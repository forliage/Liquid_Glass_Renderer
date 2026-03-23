#include "core/Timer.h"
#include <iomanip>
#include <sstream>
namespace lg {
void CpuTimer::tic(const std::string& name) { starts_[name]=std::chrono::high_resolution_clock::now(); }
double CpuTimer::toc_ms(const std::string& name) {
    auto end=std::chrono::high_resolution_clock::now();
    auto it=starts_.find(name); if(it==starts_.end()) return 0.0;
    return std::chrono::duration<double,std::milli>(end-it->second).count();
}
void CpuTimer::reset() { starts_.clear(); }

std::string format_frame_timing_summary(const FrameTiming& timing) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3)
        << "cpu_ms=" << timing.cpu_ms
        << ", gpu_ms=" << timing.gpu_ms;
    for (const PassTiming& pass : timing.passes) {
        oss << ", " << pass.name << "(cpu=" << pass.cpu_ms << ", gpu=" << pass.gpu_ms << ")";
    }
    return oss.str();
}
}
