#include "chronosstream/catchup/catchup_engine.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

#include "chronosstream/net/http_client.hpp"

namespace chronosstream {

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
    if (!channel.supports_catchup) {
        return channel.live_url;
    }

    // 4.4: 基础边界校验（非法时间段直接回退直播）。
    if (target_prog.start_time <= 0 || target_prog.end_time <= 0 || target_prog.end_time <= target_prog.start_time) {
        return channel.live_url;
    }

    // 4.4: 超出回看窗口时直接回退（若 catchup_days 未设置则不做窗口裁剪）。
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

Error CatchupEngine::probeAvailability(const std::string& url, long timeout_seconds, int max_retries) {
    if (url.empty()) {
        return {ErrorCode::ERR_INVALID_ARGUMENT, "回看 URL 为空"};
    }

    const Error rc = HttpClient::head(url, timeout_seconds, max_retries);
    if (rc.ok()) {
        return rc;
    }

    // 5.3: 对外暴露更语义化错误码，便于 UI 提示。
    if (rc.code == ErrorCode::ERR_CATCHUP_EXPIRED) {
        return {ErrorCode::ERR_CATCHUP_EXPIRED, "回看资源已过期或被清理"};
    }

    return {ErrorCode::ERR_CATCHUP_UNAVAILABLE, "回看地址不可用: " + rc.message};
}

std::string CatchupEngine::buildM3UCatchup(const Channel& channel, const Programme& prog) {
    const auto replaceTemplateTokens = [&](std::string tpl) {
        const std::regex token_regex(R"(\$\{\((b|e)\)([^}]+)\})");
        std::string out;
        std::size_t last = 0;
        for (std::sregex_iterator it(tpl.begin(), tpl.end(), token_regex), end; it != end; ++it) {
            const auto& m = *it;
            out.append(tpl.substr(last, m.position() - last));
            const bool is_begin = m[1].str() == "b";
            out.append(formatTimeWithTemplate(is_begin ? prog.start_time : prog.end_time, m[2].str()));
            last = static_cast<std::size_t>(m.position() + m.length());
        }
        out.append(tpl.substr(last));
        return out;
    };

    const long duration_seconds = std::max<long>(1, static_cast<long>(prog.end_time - prog.start_time));
    const std::string begin_unix = std::to_string(static_cast<long long>(prog.start_time));
    const std::string end_unix = std::to_string(static_cast<long long>(prog.end_time));

    // 默认模板为空时，提供业内常见兜底规则。
    std::string tpl = channel.catchup_template;
    if (tpl.empty()) {
        if (channel.catchup_type == "shift") tpl = "?utc=${(b)}&lutc=${(e)}";
        else if (channel.catchup_type == "flussonic") tpl = "-${(b)}-${(duration)}";
        else tpl = "?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}";
    }

    tpl = replaceTemplateTokens(tpl);
    tpl = replaceAll(tpl, "${(b)}", begin_unix);
    tpl = replaceAll(tpl, "${(e)}", end_unix);
    tpl = replaceAll(tpl, "${(duration)}", std::to_string(duration_seconds));

    std::string type = channel.catchup_type;
    if (type.empty()) type = "append";

    if (type == "flussonic") {
        // flussonic: 在最后一个扩展名前插入片段，如 index.m3u8 -> index-<b>-<duration>.m3u8
        const std::size_t dot = channel.live_url.rfind('.');
        if (dot != std::string::npos) {
            return sanitizeUrl(channel.live_url.substr(0, dot) + tpl + channel.live_url.substr(dot));
        }
        return sanitizeUrl(channel.live_url + tpl);
    }

    if (type == "shift") {
        // shift: 常见 ?utc=<begin>&lutc=<end>
        if (tpl.rfind("http://", 0) == 0 || tpl.rfind("https://", 0) == 0) return sanitizeUrl(tpl);
        return sanitizeUrl(channel.live_url + tpl);
    }

    // append（以及未知类型默认回退到 append）
    if (tpl.rfind("http://", 0) == 0 || tpl.rfind("https://", 0) == 0) return sanitizeUrl(tpl);
    return sanitizeUrl(channel.live_url + tpl);
}

std::string CatchupEngine::buildXCCatchup(const Channel& channel, const Programme& prog, const ServerCredentials& creds) {
    // 4.2: 参数缺失回退到直播地址。
    if (creds.server_url.empty() || creds.username.empty() || creds.password.empty() || channel.xc_stream_id.empty()) {
        return {};
    }

    long duration_minutes = static_cast<long>((prog.end_time - prog.start_time) / 60);
    if (duration_minutes <= 0) {
        duration_minutes = 1;
    }

    // 4.4: 时长上限保护，避免异常节目单导致超长请求。
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

std::string CatchupEngine::formatTimeWithTemplate(std::time_t ts, const std::string& java_like_fmt) {
    // 将常见 Java 时间模板映射为 strftime 模板。
    std::string fmt = java_like_fmt;
    fmt = replaceAll(fmt, "yyyy", "%Y");
    fmt = replaceAll(fmt, "MM", "%m");
    fmt = replaceAll(fmt, "dd", "%d");
    fmt = replaceAll(fmt, "HH", "%H");
    fmt = replaceAll(fmt, "mm", "%M");
    fmt = replaceAll(fmt, "ss", "%S");

    std::tm utc_tm {};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &ts);
#else
    gmtime_r(&ts, &utc_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, fmt.c_str());
    return oss.str();
}

std::string CatchupEngine::sanitizeUrl(const std::string& raw_url) {
    // 4.3: 轻量 URL 清洗。
    std::string s = raw_url;

    // 空白字符统一编码（最常见非法字符）。
    s = replaceAll(s, " ", "%20");

    // 清理重复分隔符。
    while (s.find("??") != std::string::npos) s = replaceAll(s, "??", "?");
    while (s.find("&&") != std::string::npos) s = replaceAll(s, "&&", "&");
    s = replaceAll(s, "?&", "?");
    s = replaceAll(s, "&?", "&");

    // 移除尾部无效分隔符。
    while (!s.empty() && (s.back() == '?' || s.back() == '&')) {
        s.pop_back();
    }

    return s;
}

} // namespace chronosstream
