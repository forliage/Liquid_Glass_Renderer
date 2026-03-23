#include "utils/Logger.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
namespace lg {

namespace {

std::mutex g_log_mutex;
std::function<void(const std::string&)> g_log_sink;

const char* level_tag(LogLevel level) {
    const char* tag = "INFO";
    switch (level) {
        case LogLevel::Trace: tag = "TRACE"; break;
        case LogLevel::Info: tag = "INFO"; break;
        case LogLevel::Warn: tag = "WARN"; break;
        case LogLevel::Error: tag = "ERROR"; break;
    }
    return tag;
}

}  // namespace

void init_logger() {}

std::string format_log_message(LogLevel level, const std::string& msg) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
    localtime_r(&timestamp, &local_tm);

    std::ostringstream oss;
    oss << "[" << level_tag(level) << "] "
        << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
        << " " << msg;
    return oss.str();
}

void set_log_sink(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_sink = std::move(sink);
}

void reset_log_sink() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_sink = {};
}

void log(LogLevel level, const std::string& msg) {
    const std::string formatted = format_log_message(level, msg);
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_sink) {
        g_log_sink(formatted);
        return;
    }
    std::cerr << formatted << std::endl;
}
}
