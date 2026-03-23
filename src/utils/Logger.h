#pragma once
#include <functional>
#include <string>
namespace lg {
enum class LogLevel { Trace, Info, Warn, Error };
void init_logger();
std::string format_log_message(LogLevel level, const std::string& msg);
void set_log_sink(std::function<void(const std::string&)> sink);
void reset_log_sink();
void log(LogLevel level, const std::string& msg);
}
