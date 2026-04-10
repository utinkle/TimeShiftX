#pragma once

#include <iostream>
#include <string>

namespace chronosstream {

// 日志级别：Phase 1 使用轻量实现，后续可替换为 spdlog 等组件。
enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
};

// 简单日志器：当前输出到标准输出/错误输出。
class Logger {
public:
    // 设置全局最低日志等级。
    static void setLevel(LogLevel level) {
        level_ = level;
    }

    // 按等级输出日志。
    static void log(LogLevel level, const std::string& msg) {
        if (level < level_) {
            return;
        }

        std::ostream& os = (level == LogLevel::ERROR) ? std::cerr : std::cout;
        os << "[ChronosStream] " << levelToString(level) << " " << msg << '\n';
    }

private:
    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "[DEBUG]";
            case LogLevel::INFO: return "[INFO ]";
            case LogLevel::WARN: return "[WARN ]";
            case LogLevel::ERROR: return "[ERROR]";
            default: return "[UNKWN]";
        }
    }

    // 默认 INFO：避免调试日志过多干扰终端。
    inline static LogLevel level_ = LogLevel::INFO;
};

} // namespace chronosstream
