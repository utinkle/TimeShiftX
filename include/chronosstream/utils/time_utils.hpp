#pragma once

#include <ctime>
#include <string>

namespace chronosstream {
namespace time_utils {

// 将 time_t（UTC）格式化为可读字符串，默认格式：YYYY-MM-DD HH:MM:SS。
std::string formatUtc(std::time_t ts, const std::string& fmt = "%Y-%m-%d %H:%M:%S");

// 判断节目是否已经结束：用于点击回看前的快速逻辑判断。
bool isProgrammeEnded(std::time_t programme_end_time, std::time_t now_utc);

} // namespace time_utils
} // namespace chronosstream
