#pragma once

#include <string>

namespace pugi {

struct xml_parse_result {
    bool status {false};
    const char* description_text {"parse_error"};
    explicit operator bool() const { return status; }
    const char* description() const { return description_text; }
};

// 轻量兼容层：仅提供 load_string 与原始文本访问，满足当前项目 XMLTV 解析入口。
class xml_document {
public:
    xml_parse_result load_string(const char* text) {
        xml_parse_result r;
        if (text == nullptr) {
            r.status = false;
            r.description_text = "null_input";
            return r;
        }
        raw_ = text;
        // 最小校验：包含 <tv 与 </tv>。
        if (raw_.find("<tv") != std::string::npos && raw_.find("</tv>") != std::string::npos) {
            r.status = true;
            r.description_text = "ok";
        } else {
            r.status = false;
            r.description_text = "invalid_xmltv";
        }
        return r;
    }

    const std::string& raw_text() const { return raw_; }

private:
    std::string raw_;
};

} // namespace pugi
