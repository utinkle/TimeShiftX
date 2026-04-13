#pragma once

#include <string>
#include <vector>

#include "timeshiftx/errors.hpp"
#include "timeshiftx/types.hpp"

namespace timeshiftx {

// 解析策略接口：M3U/XC 等不同来源需实现统一接口，保证上层无感切换。
class ISourceParser {
public:
    virtual ~ISourceParser() = default;

    // 输入原始数据（文本或 JSON 字符串）并执行解析。
    // 返回 Error，便于上层准确识别失败原因。
    virtual Error parse(const std::string& raw_data) = 0;

    // 获取已解析的频道列表。
    virtual std::vector<Channel> getChannels() const = 0;
};

} // namespace timeshiftx
