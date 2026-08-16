/**
 * json.hpp — 轻量级 JSON 包装器
 *
 * 基于 simdjson 的 JSON 解析器 + 内置 JSON 构建器。
 * 提供与 nlohmann::json 兼容的 API 子集，用于替换 nlohmann_json 库。
 *
 * 支持的 API：
 *   - 构造：Json(), Json::object(), Json::array(), Json::parse()
 *   - 初始化列表：{{"key", val}, ...}
 *   - 访问：operator[], contains(), value()
 *   - 修改：push_back(), operator[]=
 *   - 序列化：dump(indent)
 */

#pragma once

#include <cstring>
#include <initializer_list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#define SIMDJSON_HEADER_ONLY 1
#include "simdjson.h"

namespace codegraph {

class Json {
public:
    // ── 内部类型 ──
    using NullData   = std::nullptr_t;
    using BoolData   = bool;
    using IntData    = int64_t;
    using DoubleData = double;
    using StringData = std::string;
    using ArrayData  = std::vector<Json>;
    using ObjectData = std::map<std::string, Json, std::less<>>;

    // ── 构造函数 ──

    Json() : data_(NullData{}) {}
    Json(std::nullptr_t) : data_(NullData{}) {}
    Json(bool v) : data_(v) {}
    Json(int v) : data_(static_cast<IntData>(v)) {}
    Json(unsigned int v) : data_(static_cast<IntData>(v)) {}
    Json(size_t v) : data_(static_cast<IntData>(v)) {}
    Json(IntData v) : data_(v) {}
    Json(double v) : data_(v) {}
    Json(const char* v) : data_(StringData(v)) {}
    Json(std::string v) : data_(std::move(v)) {}
    Json(std::string_view v) : data_(StringData(v)) {}
    Json(ArrayData arr) : data_(std::move(arr)) {}
    Json(ObjectData obj) : data_(std::move(obj)) {}

    Json(std::initializer_list<std::pair<const char*, Json>> init) {
        ObjectData obj;
        for (auto& [k, v] : init) {
            obj.emplace(k, v);
        }
        data_ = std::move(obj);
    }

    // ── 静态工厂方法 ──

    static Json object() {
        return Json(ObjectData{});
    }

    static Json array() {
        return Json(ArrayData{});
    }

    static Json array(std::initializer_list<Json> init) {
        ArrayData arr;
        for (auto& v : init) {
            arr.push_back(v);
        }
        return Json(std::move(arr));
    }

    static Json parse(const std::string& s) {
        // simdjson ondemand 要求输入缓冲尾部带 SIMDJSON_PADDING 零字节
        // (iterate 时检查 has_padding); 直接 iterate(s) 对无 padding 输入
        // 报 INSUFFICIENT_PADDING。此处补齐后以原长度 string_view 解析。
        std::string padded(s.size() + simdjson::SIMDJSON_PADDING, '\0');
        std::memcpy(padded.data(), s.data(), s.size());
        simdjson::ondemand::parser parser;
        // 带 capacity 的重载: 声明底层缓冲含 padding (simdjson 检查 capacity)
        auto doc = parser.iterate(
            std::string_view(padded.data(), s.size()), padded.capacity()
        );
        return from_simdjson_value(doc.get_value());
    }

    // ── 访问操作符 ──

    Json& operator[](const std::string& key) {
        if (!std::holds_alternative<ObjectData>(data_)) {
            data_ = ObjectData{};
        }
        return std::get<ObjectData>(data_)[key];
    }

    const Json& operator[](const std::string& key) const {
        const auto& obj = std::get<ObjectData>(data_);
        auto it = obj.find(key);
        if (it == obj.end()) {
            throw std::runtime_error("Key not found: " + key);
        }
        return it->second;
    }

    Json& operator[](size_t index) {
        return std::get<ArrayData>(data_)[index];
    }

    const Json& operator[](size_t index) const {
        return std::get<ArrayData>(data_)[index];
    }

    // ── 键存在性检查 ──

    bool contains(const std::string& key) const {
        if (!std::holds_alternative<ObjectData>(data_)) return false;
        const auto& obj = std::get<ObjectData>(data_);
        return obj.find(key) != obj.end();
    }

    // ── 带默认值的取值 ──

    // const char* 默认值 → 返回 std::string
    std::string value(const std::string& key, const char* default_val) const;

    // 通用模板（用于 int, Json 等类型）
    template <typename T>
    T value(const std::string& key, const T& default_val) const;

    // ── 数组操作 ──

    void push_back(const Json& val) {
        ensure_array();
        std::get<ArrayData>(data_).push_back(val);
    }

    void push_back(Json&& val) {
        ensure_array();
        std::get<ArrayData>(data_).push_back(std::move(val));
    }

    void push_back(std::initializer_list<std::pair<const char*, Json>> init) {
        ensure_array();
        std::get<ArrayData>(data_).push_back(Json(init));
    }

    size_t size() const {
        if (std::holds_alternative<ArrayData>(data_)) {
            return std::get<ArrayData>(data_).size();
        }
        if (std::holds_alternative<ObjectData>(data_)) {
            return std::get<ObjectData>(data_).size();
        }
        return 0;
    }

    // ── 比较运算符 ──

    friend bool operator==(const Json& a, const Json& b) {
        return a.data_ == b.data_;
    }

    friend bool operator!=(const Json& a, const Json& b) {
        return !(a == b);
    }

    // ── 类型检查 ──

    bool is_null()    const { return std::holds_alternative<NullData>(data_); }
    bool is_boolean() const { return std::holds_alternative<BoolData>(data_); }
    bool is_number()  const {
        return std::holds_alternative<IntData>(data_)
            || std::holds_alternative<DoubleData>(data_);
    }
    bool is_string()  const { return std::holds_alternative<StringData>(data_); }
    bool is_array()   const { return std::holds_alternative<ArrayData>(data_); }
    bool is_object()  const { return std::holds_alternative<ObjectData>(data_); }

    // ── 值提取 ──

    template <typename T>
    T get() const;

    // ── 序列化 ──

    std::string dump(int indent = -1) const {
        std::ostringstream os;
        dump_to(os, indent, 0);
        return os.str();
    }

private:
    using Data = std::variant<
        NullData, BoolData, IntData, DoubleData, StringData, ArrayData, ObjectData
    >;
    Data data_;

    void ensure_array() {
        if (!std::holds_alternative<ArrayData>(data_)) {
            data_ = ArrayData{};
        }
    }

    static Json from_simdjson_value(simdjson::ondemand::value val);

    void dump_to(std::ostringstream& os, int indent, int level) const;
};

// ── get<T>() 模板特化 ──

template <> inline std::string Json::get<std::string>() const {
    if (std::holds_alternative<StringData>(data_)) {
        return std::get<StringData>(data_);
    }
    throw std::runtime_error("JSON value is not a string");
}

template <> inline int64_t Json::get<int64_t>() const {
    if (std::holds_alternative<IntData>(data_)) {
        return std::get<IntData>(data_);
    }
    if (std::holds_alternative<DoubleData>(data_)) {
        return static_cast<int64_t>(std::get<DoubleData>(data_));
    }
    throw std::runtime_error("JSON value is not a number");
}

template <> inline int Json::get<int>() const {
    return static_cast<int>(get<int64_t>());
}

template <> inline double Json::get<double>() const {
    if (std::holds_alternative<DoubleData>(data_)) {
        return std::get<DoubleData>(data_);
    }
    if (std::holds_alternative<IntData>(data_)) {
        return static_cast<double>(std::get<IntData>(data_));
    }
    throw std::runtime_error("JSON value is not a number");
}

template <> inline bool Json::get<bool>() const {
    if (std::holds_alternative<BoolData>(data_)) {
        return std::get<BoolData>(data_);
    }
    throw std::runtime_error("JSON value is not a boolean");
}

template <> inline Json Json::get<Json>() const {
    return *this;
}

// ── value() 实现（必须在 get<T>() 特化之后） ──

inline std::string Json::value(const std::string& key, const char* default_val) const {
    if (contains(key)) {
        return (*this)[key].get<std::string>();
    }
    return std::string(default_val);
}

template <typename T>
inline T Json::value(const std::string& key, const T& default_val) const {
    if (contains(key)) {
        return (*this)[key].get<T>();
    }
    return default_val;
}

// ── simdjson → Json 转换 ──

inline Json Json::from_simdjson_value(simdjson::ondemand::value val) {
    using namespace simdjson;

    ondemand::json_type t = val.type().value();
    switch (t) {
        case ondemand::json_type::null:
            return Json(nullptr);

        case ondemand::json_type::boolean:
            return Json(static_cast<bool>(val.get_bool().value()));

        case ondemand::json_type::number: {
            ondemand::number_type nt = val.get_number_type().value();
            if (nt == ondemand::number_type::floating_point_number) {
                return Json(static_cast<double>(val.get_double().value()));
            }
            return Json(static_cast<int64_t>(val.get_int64().value()));
        }

        case ondemand::json_type::string: {
            std::string_view sv = val.get_string().value();
            return Json(std::string(sv));
        }

        case ondemand::json_type::array: {
            ArrayData arr;
            ondemand::array a = val.get_array().value();
            for (auto elem : a) {
                arr.push_back(from_simdjson_value(elem.value()));
            }
            return Json(std::move(arr));
        }

        case ondemand::json_type::object: {
            ObjectData obj;
            ondemand::object o = val.get_object().value();
            for (auto field : o) {
                auto key = field.key().value().raw();
                obj.emplace(std::string(key),
                            from_simdjson_value(field.value().value()));
            }
            return Json(std::move(obj));
        }
    }

    return Json(nullptr);
}

// ── dump 实现 ──

inline void Json::dump_to(std::ostringstream& os, int indent, int level) const {
    std::visit([&](const auto& val) {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, NullData>) {
            os << "null";
        }
        else if constexpr (std::is_same_v<T, BoolData>) {
            os << (val ? "true" : "false");
        }
        else if constexpr (std::is_same_v<T, IntData>) {
            os << val;
        }
        else if constexpr (std::is_same_v<T, DoubleData>) {
            os << val;
        }
        else if constexpr (std::is_same_v<T, StringData>) {
            os << '"';
            for (char c : val) {
                switch (c) {
                    case '"':  os << "\\\""; break;
                    case '\\': os << "\\\\"; break;
                    case '\n': os << "\\n";  break;
                    case '\r': os << "\\r";  break;
                    case '\t': os << "\\t";  break;
                    default:   os << c;      break;
                }
            }
            os << '"';
        }
        else if constexpr (std::is_same_v<T, ArrayData>) {
            if (val.empty()) {
                os << "[]";
                return;
            }
            os << "[";
            if (indent >= 0) os << "\n";
            for (size_t i = 0; i < val.size(); ++i) {
                if (indent >= 0) os << std::string((level + 1) * indent, ' ');
                val[i].dump_to(os, indent, level + 1);
                if (i < val.size() - 1) os << ",";
                if (indent >= 0) os << "\n";
            }
            if (indent >= 0) os << std::string(level * indent, ' ');
            os << "]";
        }
        else if constexpr (std::is_same_v<T, ObjectData>) {
            if (val.empty()) {
                os << "{}";
                return;
            }
            os << "{";
            if (indent >= 0) os << "\n";
            size_t i = 0;
            for (const auto& [k, v] : val) {
                if (indent >= 0) os << std::string((level + 1) * indent, ' ');
                os << '"' << k << '"' << ":";
                if (indent >= 0) os << " ";
                v.dump_to(os, indent, level + 1);
                if (i < val.size() - 1) os << ",";
                if (indent >= 0) os << "\n";
                ++i;
            }
            if (indent >= 0) os << std::string(level * indent, ' ');
            os << "}";
        }
    }, data_);
}

}  // namespace codegraph