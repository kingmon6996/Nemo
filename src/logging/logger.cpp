#include "nemo/logging/logger.hpp"
#include <chrono>
#include <iomanip>

namespace nemo::logging {

namespace {

std::string_view level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

} // namespace

void Logger::log(LogLevel level, std::string_view component, std::string_view message) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    // We lock the mutex to prevent interleaved log output from multiple threads
    std::lock_guard<std::mutex> lock(m_mutex);

    // Basic timestamp formatting (YYYY-MM-DD HH:MM:SS)
    // Note: std::put_time expects a std::tm. 
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::cerr << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "] "
              << "[" << level_to_string(level) << "] "
              << "[" << component << "] "
              << message << "\n";
}

} // namespace nemo::logging
