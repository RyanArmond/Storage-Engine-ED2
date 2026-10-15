#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace storage {

namespace {

void skip_whitespace(const std::string& str, size_t& idx) {
    while (idx < str.size() && (std::isspace(static_cast<unsigned char>(str[idx])) != 0)) {
        idx++;
    }
}

bool parse_string(const std::string& str, size_t& idx, std::string& out) {
    if (idx >= str.size() || str[idx] != '"') return false;
    idx++; // Skip opening quote
    out.clear();

    while (idx < str.size()) {
        char c = str[idx++];
        if (c == '"') {
            return true; // Closing quote
        }
        if (c == '\\') {
            if (idx >= str.size()) return false;
            char esc = str[idx++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (idx + 4 > str.size()) return false;
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = str[idx++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                        else return false;
                    }
                    // Basic UTF-8 encoder for codepoint
                    if (cp <= 0x7F) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp <= 0x7FF) {
                        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default:
                    out.push_back(esc);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool parse_value(const std::string& str, size_t& idx, JsonValue& val);

bool parse_object(const std::string& str, size_t& idx, JsonValue& val) {
    if (idx >= str.size() || str[idx] != '{') return false;
    idx++; // Skip '{'
    val.type = JsonType::Object;
    val.obj_val.clear();

    skip_whitespace(str, idx);
    if (idx < str.size() && str[idx] == '}') {
        idx++;
        return true;
    }

    while (idx < str.size()) {
        skip_whitespace(str, idx);
        std::string key;
        if (!parse_string(str, idx, key)) return false;

        skip_whitespace(str, idx);
        if (idx >= str.size() || str[idx] != ':') return false;
        idx++; // Skip ':'

        skip_whitespace(str, idx);
        JsonValue sub_val;
        if (!parse_value(str, idx, sub_val)) return false;

        val.obj_val[key] = std::move(sub_val);

        skip_whitespace(str, idx);
        if (idx < str.size() && str[idx] == ',') {
            idx++; // Skip ','
            continue;
        }
        if (idx < str.size() && str[idx] == '}') {
            idx++; // Skip '}'
            return true;
        }
        return false;
    }
    return false;
}

bool parse_value(const std::string& str, size_t& idx, JsonValue& val) {
    skip_whitespace(str, idx);
    if (idx >= str.size()) return false;

    char c = str[idx];
    if (c == '"') {
        val.type = JsonType::String;
        return parse_string(str, idx, val.str_val);
    }
    if (c == '{') {
        return parse_object(str, idx, val);
    }
    if (c == 't' && str.substr(idx, 4) == "true") {
        idx += 4;
        val.type = JsonType::Boolean;
        val.bool_val = true;
        return true;
    }
    if (c == 'f' && str.substr(idx, 5) == "false") {
        idx += 5;
        val.type = JsonType::Boolean;
        val.bool_val = false;
        return true;
    }
    if (c == 'n' && str.substr(idx, 4) == "null") {
        idx += 4;
        val.type = JsonType::Null;
        return true;
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        val.type = JsonType::Number;
        size_t start = idx;
        if (str[idx] == '-') idx++;
        while (idx < str.size() && std::isdigit(static_cast<unsigned char>(str[idx]))) {
            idx++;
        }
        val.str_val = str.substr(start, idx - start);
        val.num_val = std::strtoull(val.str_val.c_str(), nullptr, 10);
        return true;
    }

    return false;
}

} // namespace

std::optional<JsonValue> JsonParser::parse(const std::string& str) {
    size_t idx = 0;
    skip_whitespace(str, idx);
    JsonValue root;
    if (parse_value(str, idx, root)) {
        skip_whitespace(str, idx);
        if (idx == str.size()) {
            return root;
        }
    }
    return std::nullopt;
}

std::string JsonParser::escape_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

bool parse_workload_command(const std::string& line, WorkloadCommand& cmd) {
    auto parsed = JsonParser::parse(line);
    if (!parsed || parsed->type != JsonType::Object) {
        return false;
    }

    const auto& obj = *parsed;
    if (!obj.has("id") || !obj.has("op")) {
        return false;
    }

    cmd.id = obj["id"].num_val;
    cmd.op = obj["op"].str_val;
    std::transform(cmd.op.begin(), cmd.op.end(), cmd.op.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    if (obj.has("key")) {
        cmd.key = obj["key"].num_val;
    } else {
        cmd.key = 0;
    }

    if (obj.has("value")) {
        cmd.has_value = true;
        cmd.value = obj["value"].str_val;
    } else {
        cmd.has_value = false;
        cmd.value.clear();
    }

    return true;
}

std::string format_response(uint64_t id, const std::string& status, const std::optional<std::string>& value) {
    std::ostringstream ss;
    ss << "{\"id\": " << id << ", \"status\": \"" << status << "\"";
    if (value.has_value()) {
        ss << ", \"value\": \"" << JsonParser::escape_string(*value) << "\"";
    }
    ss << "}";
    return ss.str();
}

} // namespace storage
