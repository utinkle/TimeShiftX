#include "timeshiftx/catchup_engine.hpp"
#include "timeshiftx/network_service.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

namespace timeshiftx {

namespace {

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return s;
    }
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string trimTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

} // namespace

std::string CatchupEngine::buildUrl(const Channel& channel, const Programme& target_prog, const ServerCredentials& creds) {
    // 4.4: Basic boundary check (invalid time period directly falls back to live).
    if (target_prog.start_time <= 0 || target_prog.end_time <= 0 || target_prog.end_time <= target_prog.start_time) {
        return channel.live_url;
    }

    // 4.4: Directly fall back when exceeding catchup window (if catchup_days is not set, no window clipping).
    if (channel.catchup_days > 0) {
        const std::time_t now = std::time(nullptr);
        const std::time_t oldest = now - static_cast<std::time_t>(channel.catchup_days) * 24 * 60 * 60;
        if (target_prog.start_time < oldest) {
            return channel.live_url;
        }
    }

    if (channel.source_type == Channel::SourceType::M3U) {
        const std::string url = buildM3UCatchup(channel, target_prog);
        return url.empty() ? channel.live_url : url;
    }

    if (channel.source_type == Channel::SourceType::XTREAM_CODES) {
        const std::string url = buildXCCatchup(channel, target_prog, creds);
        return url.empty() ? channel.live_url : url;
    }

    return channel.live_url;
}

Error CatchupEngine::probeAvailability(const std::string& url,
                                       long timeout_seconds,
                                       int max_retries,
                                       INetworkPlugin* injected_plugin) {
    if (url.empty()) {
        return {ErrorCode::ERR_INVALID_ARGUMENT, "Catchup URL is empty"};
    }

    const Error rc = NetworkService::head(url, timeout_seconds, max_retries, injected_plugin);
    if (rc.ok()) {
        return rc;
    }

    // 5.3: Expose more semantic error codes externally for UI prompts.
    if (rc.code == ErrorCode::ERR_CATCHUP_EXPIRED) {
        return {ErrorCode::ERR_CATCHUP_EXPIRED, "Catchup resource has expired or been cleaned up"};
    }

    return {ErrorCode::ERR_CATCHUP_UNAVAILABLE, "Catchup address unavailable: " + rc.message};
}

std::string CatchupEngine::buildM3UCatchup(const Channel& channel, const Programme& prog) {
    const long duration_seconds = std::max<long>(1, static_cast<long>(prog.end_time - prog.start_time));
    
    const auto replaceTemplateTokens = [&](std::string tpl) -> std::string {
        // 辅助函数：安全地用迭代器遍历并替换
        auto safe_replace = [](const std::string& input,
                               const std::regex& pattern,
                               std::function<std::string(const std::smatch&)> replacer) -> std::string {
            std::string result;
            auto it = std::sregex_iterator(input.begin(), input.end(), pattern);
            auto end = std::sregex_iterator();
            std::string::const_iterator last = input.begin();
            for (; it != end; ++it) {
                // 追加匹配之前的文本
                result.append(last, it->prefix().second);
                // 追加替换后的内容
                result.append(replacer(*it));
                last = it->suffix().first;
            }
            // 追加剩余部分
            result.append(last, input.end());
            return result;
        };

        // 1. 处理 ${(b|e)...} 格式（支持 :utc 或 :timestamp）
        const std::regex extended_regex(R"(\$\{\((b|e)\)([^}:]+)(?::([^}]+))?\})");
        std::string result = safe_replace(tpl, extended_regex, [&](const std::smatch& m) -> std::string {
            const bool is_begin = m[1].str() == "b";
            const std::time_t time_val = is_begin ? prog.start_time : prog.end_time;
            const std::string fmt_str = m[2].str();
            const std::string suffix = m[3].matched ? m[3].str() : "";
            
            if (suffix == "timestamp" || fmt_str == "timestamp") {
                return std::to_string(static_cast<long long>(time_val));
            } else {
                return formatTimeWithTemplate(time_val, fmt_str, suffix == "utc");
            }
        });

        // 2. 处理 {utc:format} 简单格式
        const std::regex utc_simple_regex(R"(\{utc:([^}]+)\})");
        result = safe_replace(result, utc_simple_regex, [&](const std::smatch& m) -> std::string {
            const std::string format = m[1].str();
            return formatTimeWithTemplate(prog.start_time, format, true);
        });

        // 3. 处理 {utcend:format} 简单格式
        const std::regex utcend_simple_regex(R"(\{utcend:([^}]+)\})");
        result = safe_replace(result, utcend_simple_regex, [&](const std::smatch& m) -> std::string {
            const std::string format = m[1].str();
            return formatTimeWithTemplate(prog.end_time, format, true);
        });

        // 4. 处理 ${duration} 和 ${(duration)}
        result = replaceAll(result, "${duration}", std::to_string(duration_seconds));
        result = replaceAll(result, "${(duration)}", std::to_string(duration_seconds));

        // 5. 处理 ${offset}
        long offset_seconds = 0;
        if (channel.catchup_days > 0) {
            offset_seconds = -static_cast<long>(channel.catchup_days) * 24 * 60 * 60;
        }
        result = replaceAll(result, "${offset}", std::to_string(offset_seconds));

        // 6. 遗留支持：${(b)} 和 ${(e)} 作为 Unix 时间戳
        result = replaceAll(result, "${(b)}", std::to_string(static_cast<long long>(prog.start_time)));
        result = replaceAll(result, "${(e)}", std::to_string(static_cast<long long>(prog.end_time)));

        return result;
    };

    // 默认模板逻辑不变
    std::string tpl = channel.catchup_template;
    if (tpl.empty()) {
        if (channel.catchup_type == "shift") tpl = "?utc=${(b)}&lutc=${(e)}";
        else if (channel.catchup_type == "flussonic") tpl = "-${(b)}-${(duration)}";
        else tpl = "?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}";
    }

    tpl = replaceTemplateTokens(tpl);

    std::string type = channel.catchup_type;
    if (type.empty()) type = "append";

    if (type == "default") {
        if (tpl.rfind("http://", 0) == 0 || tpl.rfind("https://", 0) == 0) {
            return sanitizeUrl(tpl);
        }
        return {};
    }

    if (type == "flussonic") {
        const std::size_t dot = channel.live_url.rfind('.');
        if (dot != std::string::npos) {
            return sanitizeUrl(channel.live_url.substr(0, dot) + tpl + channel.live_url.substr(dot));
        }
        return sanitizeUrl(channel.live_url + tpl);
    }

    if (type == "shift") {
        if (tpl.rfind("http://", 0) == 0 || tpl.rfind("https://", 0) == 0) return sanitizeUrl(tpl);
        return sanitizeUrl(channel.live_url + tpl);
    }

    // append 及其他未知类型
    if (tpl.rfind("http://", 0) == 0 || tpl.rfind("https://", 0) == 0) return sanitizeUrl(tpl);
    return sanitizeUrl(channel.live_url + tpl);
}

std::string CatchupEngine::buildXCCatchup(const Channel& channel, const Programme& prog, const ServerCredentials& creds) {
    // 4.2: Missing parameters fall back to live address.
    if (creds.server_url.empty() || creds.username.empty() || creds.password.empty() || channel.xc_stream_id.empty()) {
        return {};
    }

    long duration_minutes = static_cast<long>((prog.end_time - prog.start_time) / 60);
    if (duration_minutes <= 0) {
        duration_minutes = 1;
    }

    // 4.4: Duration upper limit protection to avoid abnormally long requests from program schedules.
    const long max_minutes = (channel.catchup_days > 0) ? static_cast<long>(channel.catchup_days) * 24L * 60L : 7L * 24L * 60L;
    if (duration_minutes > max_minutes) {
        duration_minutes = max_minutes;
    }

    const std::string server = trimTrailingSlash(creds.server_url);
    const std::string start = formatTimeWithTemplate(prog.start_time, "yyyy-MM-dd:HH-mm");

    const std::string raw = server + "/timeshift/" + creds.username + "/" + creds.password + "/" +
                            std::to_string(duration_minutes) + "/" + start + "/" + channel.xc_stream_id + ".ts";

    return sanitizeUrl(raw);
}

std::string CatchupEngine::formatTimeWithTemplate(std::time_t ts, const std::string& java_like_fmt, bool use_utc) {
    // 获取时间结构
    std::tm time_tm {};
    if (use_utc) {
        // Use UTC time
#if defined(_WIN32)
        gmtime_s(&time_tm, &ts);
#else
        gmtime_r(&ts, &time_tm);
#endif
    } else {
        // Use local time
#if defined(_WIN32)
        localtime_s(&time_tm, &ts);
#else
        localtime_r(&ts, &time_tm);
#endif
    }
    
    // 准备时间分量值
    int year = time_tm.tm_year + 1900;
    int month = time_tm.tm_mon + 1;
    int day = time_tm.tm_mday;
    int hour = time_tm.tm_hour;
    int minute = time_tm.tm_min;
    int second = time_tm.tm_sec;
    
    // 格式化为两位数的字符串
    char buf[256] = {};
    snprintf(buf, sizeof(buf), "%04d", year);
    std::string year_4 = buf;
    
    snprintf(buf, sizeof(buf), "%02d", month);
    std::string month_2 = buf;
    
    snprintf(buf, sizeof(buf), "%02d", day);
    std::string day_2 = buf;
    
    snprintf(buf, sizeof(buf), "%02d", hour);
    std::string hour_2 = buf;
    
    snprintf(buf, sizeof(buf), "%02d", minute);
    std::string minute_2 = buf;
    
    snprintf(buf, sizeof(buf), "%02d", second);
    std::string second_2 = buf;
    
    // 直接替换格式字符串
    std::string result = java_like_fmt;
    
    // 先替换长格式（确保不会被短格式误匹配）
    result = replaceAll(result, "yyyy", year_4);
    result = replaceAll(result, "MM", month_2);
    result = replaceAll(result, "dd", day_2);
    result = replaceAll(result, "HH", hour_2);
    result = replaceAll(result, "mm", minute_2);
    result = replaceAll(result, "ss", second_2);
    
    // 再替换短格式
    result = replaceAll(result, "Y", year_4);
    result = replaceAll(result, "m", month_2);
    result = replaceAll(result, "d", day_2);
    result = replaceAll(result, "H", hour_2);
    result = replaceAll(result, "M", minute_2);
    result = replaceAll(result, "S", second_2);
    
    return result;
}

std::string CatchupEngine::sanitizeUrl(const std::string& raw_url) {
    // 4.3: Lightweight URL sanitization.
    std::string s = raw_url;

    // Encode whitespace uniformly (most common illegal characters).
    s = replaceAll(s, " ", "%20");

    // Clean up duplicate separators.
    while (s.find("??") != std::string::npos) s = replaceAll(s, "??", "?");
    while (s.find("&&") != std::string::npos) s = replaceAll(s, "&&", "&");
    s = replaceAll(s, "?&", "?");
    s = replaceAll(s, "&?", "&");

    // Remove trailing invalid separators.
    while (!s.empty() && (s.back() == '?' || s.back() == '&')) {
        s.pop_back();
    }

    return s;
}

} // namespace timeshiftx
