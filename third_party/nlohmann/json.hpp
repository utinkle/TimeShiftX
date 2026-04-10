#pragma once

#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace nlohmann {

class json {
public:
    enum class kind { array_t, object_t, value_t, null_t };

    static json parse(const std::string& text) {
        json root;
        std::size_t i = 0;
        skipWs(text, i);
        if (i >= text.size() || text[i] != '[') {
            throw std::runtime_error("json root is not array");
        }
        root.k_ = kind::array_t;
        ++i;
        skipWs(text, i);
        while (i < text.size() && text[i] != ']') {
            root.arr_.push_back(parseObject(text, i));
            skipWs(text, i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                skipWs(text, i);
            }
        }
        if (i >= text.size() || text[i] != ']') {
            throw std::runtime_error("array not closed");
        }
        return root;
    }

    bool is_array() const { return k_ == kind::array_t; }
    bool is_object() const { return k_ == kind::object_t; }
    bool is_null() const { return k_ == kind::null_t; }
    bool is_string() const { return k_ == kind::value_t && type_ == 's'; }
    bool is_number_integer() const { return k_ == kind::value_t && type_ == 'i'; }
    bool is_number_unsigned() const { return false; }
    bool is_number_float() const { return k_ == kind::value_t && type_ == 'f'; }
    bool is_boolean() const { return k_ == kind::value_t && type_ == 'b'; }

    template <typename T>
    T get() const;

    bool contains(const char* key) const {
        return obj_.find(key) != obj_.end();
    }

    const json& operator[](const char* key) const {
        auto it = obj_.find(key);
        if (it == obj_.end()) {
            static json nullv;
            return nullv;
        }
        return it->second;
    }

    std::vector<json>::const_iterator begin() const { return arr_.begin(); }
    std::vector<json>::const_iterator end() const { return arr_.end(); }

private:
    static void skipWs(const std::string& s, std::size_t& i) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) ++i;
    }

    static std::string parseString(const std::string& s, std::size_t& i) {
        if (s[i] != '"') throw std::runtime_error("expected string");
        ++i;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) ++i;
            out.push_back(s[i++]);
        }
        if (i >= s.size()) throw std::runtime_error("unterminated string");
        ++i;
        return out;
    }

    static json parseValue(const std::string& s, std::size_t& i) {
        skipWs(s, i);
        json v;
        v.k_ = kind::value_t;
        if (i >= s.size()) throw std::runtime_error("unexpected eof");

        if (s[i] == '"') {
            v.type_ = 's';
            v.str_ = parseString(s, i);
            return v;
        }

        if (s.compare(i, 4, "null") == 0) {
            v.k_ = kind::null_t;
            i += 4;
            return v;
        }
        if (s.compare(i, 4, "true") == 0) {
            v.type_ = 'b';
            v.str_ = "true";
            i += 4;
            return v;
        }
        if (s.compare(i, 5, "false") == 0) {
            v.type_ = 'b';
            v.str_ = "false";
            i += 5;
            return v;
        }

        std::size_t start = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '.')) ++i;
        v.str_ = s.substr(start, i - start);
        v.type_ = (v.str_.find('.') == std::string::npos) ? 'i' : 'f';
        return v;
    }

    static json parseObject(const std::string& s, std::size_t& i) {
        skipWs(s, i);
        if (i >= s.size() || s[i] != '{') throw std::runtime_error("expected object");
        ++i;
        json obj;
        obj.k_ = kind::object_t;
        skipWs(s, i);
        while (i < s.size() && s[i] != '}') {
            std::string key = parseString(s, i);
            skipWs(s, i);
            if (i >= s.size() || s[i] != ':') throw std::runtime_error("expected colon");
            ++i;
            obj.obj_[key] = parseValue(s, i);
            skipWs(s, i);
            if (i < s.size() && s[i] == ',') {
                ++i;
                skipWs(s, i);
            }
        }
        if (i >= s.size() || s[i] != '}') throw std::runtime_error("object not closed");
        ++i;
        return obj;
    }

private:
    kind k_ {kind::null_t};
    char type_ {'n'};
    std::string str_;
    std::unordered_map<std::string, json> obj_;
    std::vector<json> arr_;
};

template <>
inline std::string json::get<std::string>() const { return str_; }

template <>
inline long long json::get<long long>() const { return std::stoll(str_); }

template <>
inline unsigned long long json::get<unsigned long long>() const { return static_cast<unsigned long long>(std::stoull(str_)); }

template <>
inline double json::get<double>() const { return std::stod(str_); }

template <>
inline bool json::get<bool>() const { return str_ == "true"; }

} // namespace nlohmann
