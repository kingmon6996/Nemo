#ifndef NEMO_LOGGING_LOGGER_HPP
#define NEMO_LOGGING_LOGGER_HPP

#include <string_view>
#include <mutex>
#include <iostream>

namespace nemo::logging {

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

// A minimal, thread-safe logging abstraction.
class Logger {
public:
    static Logger& get_instance() {
        static Logger instance;
        return instance;
    }

    void set_level(LogLevel level) noexcept {
        m_level = level;
    }

    bool is_enabled(LogLevel level) const noexcept {
        return level >= m_level;
    }

    void log(LogLevel level, std::string_view component, std::string_view message);

private:
    Logger() = default;

    LogLevel m_level{LogLevel::INFO};
    std::mutex m_mutex;
};

// Macros to avoid expensive message construction if the level is disabled
#define NEMO_LOG_TRACE(component, message) \
    do { \
        if (nemo::logging::Logger::get_instance().is_enabled(nemo::logging::LogLevel::TRACE)) { \
            nemo::logging::Logger::get_instance().log(nemo::logging::LogLevel::TRACE, component, message); \
        } \
    } while(0)

#define NEMO_LOG_DEBUG(component, message) \
    do { \
        if (nemo::logging::Logger::get_instance().is_enabled(nemo::logging::LogLevel::DEBUG)) { \
            nemo::logging::Logger::get_instance().log(nemo::logging::LogLevel::DEBUG, component, message); \
        } \
    } while(0)

#define NEMO_LOG_INFO(component, message) \
    do { \
        if (nemo::logging::Logger::get_instance().is_enabled(nemo::logging::LogLevel::INFO)) { \
            nemo::logging::Logger::get_instance().log(nemo::logging::LogLevel::INFO, component, message); \
        } \
    } while(0)

#define NEMO_LOG_WARN(component, message) \
    do { \
        if (nemo::logging::Logger::get_instance().is_enabled(nemo::logging::LogLevel::WARN)) { \
            nemo::logging::Logger::get_instance().log(nemo::logging::LogLevel::WARN, component, message); \
        } \
    } while(0)

#define NEMO_LOG_ERROR(component, message) \
    do { \
        if (nemo::logging::Logger::get_instance().is_enabled(nemo::logging::LogLevel::ERROR)) { \
            nemo::logging::Logger::get_instance().log(nemo::logging::LogLevel::ERROR, component, message); \
        } \
    } while(0)

#define NEMO_LOG_FATAL(component, message) \
    do { \
        if (nemo::logging::Logger::get_instance().is_enabled(nemo::logging::LogLevel::FATAL)) { \
            nemo::logging::Logger::get_instance().log(nemo::logging::LogLevel::FATAL, component, message); \
        } \
    } while(0)

} // namespace nemo::logging

#endif // NEMO_LOGGING_LOGGER_HPP
