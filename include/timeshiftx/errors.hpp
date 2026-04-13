#pragma once

#include <string>

namespace timeshiftx {

// 统一错误码：供核心中间件与 UI/播放层进行明确协作。
enum class ErrorCode {
    OK = 0,

    // 通用错误
    ERR_INVALID_ARGUMENT,
    ERR_INTERNAL,

    // 解析相关
    ERR_PARSE_M3U_FAILED,
    ERR_PARSE_XC_JSON_FAILED,
    ERR_PARSE_XMLTV_FAILED,

    // EPG 相关
    ERR_EPG_NOT_FOUND,
    ERR_EPG_TIMELINE_EMPTY,

    // 回看相关
    ERR_CATCHUP_NOT_SUPPORTED,
    ERR_CATCHUP_TEMPLATE_INVALID,
    ERR_CATCHUP_BUILD_FAILED,
    ERR_CATCHUP_UNAVAILABLE,
    ERR_CATCHUP_EXPIRED,

    // 网络相关
    ERR_NETWORK_TIMEOUT,
    ERR_NETWORK_HTTP_STATUS,
};

// 错误对象：用于函数返回值和日志记录。
struct Error {
    ErrorCode code {ErrorCode::OK};
    std::string message;

    // 便捷函数：判断是否成功。
    bool ok() const {
        return code == ErrorCode::OK;
    }
};

} // namespace timeshiftx
