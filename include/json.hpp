#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace storage {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Object
};

struct JsonValue {
    JsonType type{JsonType::Null};
    std::string str_val;
    uint64_t num_val{0};
    bool bool_val{false};
    std::unordered_map<std::string, JsonValue> obj_val;

    bool has(const std::string& key) const {
        return obj_val.find(key) != obj_val.end();
    }

    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue empty_val;
        auto it = obj_val.find(key);
        return (it != obj_val.end()) ? it->second : empty_val;
    }
};

class JsonParser {
public:
    static std::optional<JsonValue> parse(const std::string& str);
    static std::string escape_string(const std::string& s);
};

struct WorkloadCommand {
    uint64_t id{0};
    std::string op;
    uint64_t key{0};
    std::string value;
    bool has_value{false};
};

bool parse_workload_command(const std::string& line, WorkloadCommand& cmd);
std::string format_response(uint64_t id, const std::string& status, const std::optional<std::string>& value = std::nullopt);

} // namespace storage
