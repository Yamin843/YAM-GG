// ===========================================================================
// yam_json.cpp — JSON parser + stringify + Value converters
// ===========================================================================

#include "yam_internal.hpp"

namespace yam {

// ===========================================================================
// SECTION 1 — JsonValue::stringify
// ===========================================================================

namespace {
void stringify_impl(const JsonValue& v, std::string& out) {
    switch (v.type) {
    case JsonValue::Type::Null:
        out += "null";
        break;
    case JsonValue::Type::Bool:
        out += v.bool_val ? "true" : "false";
        break;
    case JsonValue::Type::Number: {
        char buf[64];
        // Detect integers.
        f64 n = v.num_val;
        if (n == static_cast<f64>(static_cast<i64>(n))) {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        } else {
            std::snprintf(buf, sizeof(buf), "%.17g", n);
        }
        out += buf;
        break;
    }
    case JsonValue::Type::String: {
        out += '"';
        for (char c : v.str_val) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    out += tmp;
                } else out += c;
            }
        }
        out += '"';
        break;
    }
    case JsonValue::Type::Array: {
        out += '[';
        for (size_t i = 0; i < v.arr_val.size(); ++i) {
            if (i) out += ',';
            stringify_impl(v.arr_val[i], out);
        }
        out += ']';
        break;
    }
    case JsonValue::Type::Object: {
        out += '{';
        bool first = true;
        for (auto& p : v.obj_val) {
            if (!first) out += ',';
            first = false;
            out += '"';
            out += str::escape_json(p.first);
            out += "\":";
            stringify_impl(p.second, out);
        }
        out += '}';
        break;
    }
    }
}
} // namespace

String JsonValue::stringify() const {
    std::string s;
    s.reserve(128);
    stringify_impl(*this, s);
    return s;
}

// ===========================================================================
// SECTION 2 — JSON parser
// ===========================================================================

namespace detail {

void JsonParserImpl::skip_ws() {
    while (pos_ < s_.size()) {
        char c = s_[pos_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
        else break;
    }
}

bool JsonParserImpl::consume(char c) {
    if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
    return false;
}

Result<JsonValue> JsonParserImpl::parse() {
    skip_ws();
    auto r = parse_value();
    if (!r) return r;
    skip_ws();
    return r;
}

Result<JsonValue> JsonParserImpl::parse_value() {
    skip_ws();
    if (pos_ >= s_.size())
        return Result<JsonValue>::err(ErrorCode::JsonParseError, "unexpected end");
    char c = s_[pos_];
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '"') return parse_string();
    if (c == 't') return parse_true();
    if (c == 'f') return parse_false();
    if (c == 'n') return parse_null();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
        return parse_number();
    return Result<JsonValue>::err(ErrorCode::JsonParseError,
        String("unexpected char: ") + c);
}

Result<JsonValue> JsonParserImpl::parse_string() {
    if (!consume('"'))
        return Result<JsonValue>::err(ErrorCode::JsonParseError, "expected \"");
    String out;
    out.reserve(64);
    while (pos_ < s_.size()) {
        char c = s_[pos_++];
        if (c == '"') {
            JsonValue v;
            v.type = JsonValue::Type::String;
            v.str_val = std::move(out);
            return Result<JsonValue>::ok(std::move(v));
        }
        if (c == '\\') {
            if (pos_ >= s_.size()) break;
            char e = s_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (pos_ + 4 > s_.size()) {
                    return Result<JsonValue>::err(ErrorCode::JsonParseError,
                        "bad unicode escape");
                }
                unsigned long cp = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = s_[pos_++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                    else return Result<JsonValue>::err(ErrorCode::JsonParseError,
                        "bad hex");
                }
                // Handle surrogate pairs.
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pos_ + 6 <= s_.size() && s_[pos_] == '\\' &&
                        s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned long lo = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = s_[pos_++];
                            lo <<= 4;
                            if (h >= '0' && h <= '9') lo |= (h - '0');
                            else if (h >= 'a' && h <= 'f') lo |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo |= (h - 'A' + 10);
                        }
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                    }
                }
                // Encode UTF-8.
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += e;
            }
        } else {
            out += c;
        }
    }
    return Result<JsonValue>::err(ErrorCode::JsonParseError, "unterminated string");
}

Result<JsonValue> JsonParserImpl::parse_number() {
    size_t start = pos_;
    if (peek() == '-') ++pos_;
    while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    if (pos_ < s_.size() && s_[pos_] == '.') {
        ++pos_;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
        ++pos_;
        if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    String num(s_.substr(start, pos_ - start));
    f64 d = 0;
    if (!str::parse_f64(num, d))
        return Result<JsonValue>::err(ErrorCode::JsonParseError, "bad number");
    JsonValue v;
    v.type = JsonValue::Type::Number;
    v.num_val = d;
    return Result<JsonValue>::ok(std::move(v));
}

Result<JsonValue> JsonParserImpl::parse_array() {
    if (!consume('['))
        return Result<JsonValue>::err(ErrorCode::JsonParseError, "expected [");
    JsonValue v;
    v.type = JsonValue::Type::Array;
    skip_ws();
    if (consume(']')) return Result<JsonValue>::ok(std::move(v));
    while (true) {
        auto r = parse_value();
        if (!r) return r;
        v.arr_val.push_back(std::move(r.value()));
        skip_ws();
        if (consume(',')) continue;
        if (consume(']')) break;
        return Result<JsonValue>::err(ErrorCode::JsonParseError, "expected , or ]");
    }
    return Result<JsonValue>::ok(std::move(v));
}

Result<JsonValue> JsonParserImpl::parse_object() {
    if (!consume('{'))
        return Result<JsonValue>::err(ErrorCode::JsonParseError, "expected {");
    JsonValue v;
    v.type = JsonValue::Type::Object;
    skip_ws();
    if (consume('}')) return Result<JsonValue>::ok(std::move(v));
    while (true) {
        skip_ws();
        if (peek() != '"')
            return Result<JsonValue>::err(ErrorCode::JsonParseError, "expected key");
        auto key_r = parse_string();
        if (!key_r) return key_r;
        skip_ws();
        if (!consume(':'))
            return Result<JsonValue>::err(ErrorCode::JsonParseError, "expected :");
        auto val_r = parse_value();
        if (!val_r) return val_r;
        v.obj_val[key_r.value().str_val] = std::move(val_r.value());
        skip_ws();
        if (consume(',')) continue;
        if (consume('}')) break;
        return Result<JsonValue>::err(ErrorCode::JsonParseError, "expected , or }");
    }
    return Result<JsonValue>::ok(std::move(v));
}

Result<JsonValue> JsonParserImpl::parse_true() {
    if (s_.substr(pos_, 4) == "true") {
        pos_ += 4;
        JsonValue v;
        v.type = JsonValue::Type::Bool;
        v.bool_val = true;
        return Result<JsonValue>::ok(std::move(v));
    }
    return Result<JsonValue>::err(ErrorCode::JsonParseError, "bad true");
}

Result<JsonValue> JsonParserImpl::parse_false() {
    if (s_.substr(pos_, 5) == "false") {
        pos_ += 5;
        JsonValue v;
        v.type = JsonValue::Type::Bool;
        v.bool_val = false;
        return Result<JsonValue>::ok(std::move(v));
    }
    return Result<JsonValue>::err(ErrorCode::JsonParseError, "bad false");
}

Result<JsonValue> JsonParserImpl::parse_null() {
    if (s_.substr(pos_, 4) == "null") {
        pos_ += 4;
        JsonValue v;
        v.type = JsonValue::Type::Null;
        return Result<JsonValue>::ok(std::move(v));
    }
    return Result<JsonValue>::err(ErrorCode::JsonParseError, "bad null");
}

} // namespace detail

Result<JsonValue> JsonValue::parse(const String& s) {
    return parse(StringView(s));
}

Result<JsonValue> JsonValue::parse(StringView s) {
    detail::JsonParserImpl p(s);
    return p.parse();
}

// ===========================================================================
// SECTION 3 — Value <-> JsonValue
// ===========================================================================

namespace detail {

JsonValue value_to_json(const Value& v) {
    switch (v.kind()) {
    case Value::Kind::Null: return JsonValue();
    case Value::Kind::Undefined: return JsonValue();
    case Value::Kind::Bool: return JsonValue(v.as_bool());
    case Value::Kind::Int: return JsonValue(v.as_int());
    case Value::Kind::Long: return JsonValue(v.as_long());
    case Value::Kind::Float: return JsonValue(static_cast<f64>(v.as_float()));
    case Value::Kind::Double: return JsonValue(v.as_double());
    case Value::Kind::String: return JsonValue(v.as_string());
    default: return JsonValue(v.as_string());
    }
}

Value json_to_value(const JsonValue& j) {
    switch (j.type) {
    case JsonValue::Type::Null: return Value();
    case JsonValue::Type::Bool: return Value(j.bool_val);
    case JsonValue::Type::Number: {
        f64 d = j.num_val;
        if (d == static_cast<f64>(static_cast<i64>(d))) {
            return Value(static_cast<i64>(d));
        }
        return Value(d);
    }
    case JsonValue::Type::String: return Value(j.str_val);
    default: return Value(j.stringify());
    }
}

} // namespace detail

// ===========================================================================
// SECTION 4 — JSON helpers on strings
// ===========================================================================

namespace detail { namespace json {

String get_string(const String& s, const char* key, const String& def) {
    auto r = JsonValue::parse(s);
    if (!r) return def;
    auto* v = r.value().get(key);
    if (!v) return def;
    return v->as_str(def);
}
i64 get_i64(const String& s, const char* key, i64 def) {
    auto r = JsonValue::parse(s);
    if (!r) return def;
    auto* v = r.value().get(key);
    if (!v) return def;
    return v->as_i64(def);
}
bool get_bool(const String& s, const char* key, bool def) {
    auto r = JsonValue::parse(s);
    if (!r) return def;
    auto* v = r.value().get(key);
    if (!v) return def;
    return v->as_bool(def);
}
std::vector<String> strings_of_array(const String& s) {
    std::vector<String> out;
    auto r = JsonValue::parse(s);
    if (!r || !r.value().is_arr()) return out;
    for (auto& e : r.value().arr_val) out.push_back(e.as_str());
    return out;
}
std::vector<String> object_bodies_of_array(const String& s, const char* key) {
    std::vector<String> out;
    auto r = JsonValue::parse(s);
    if (!r) return out;
    auto* v = r.value().get(key);
    if (!v || !v->is_arr()) return out;
    for (auto& e : v->arr_val) out.push_back(e.stringify());
    return out;
}

}} // namespace detail::json

// ===========================================================================
// SECTION 5 — Value::as_string
// ===========================================================================

String Value::as_string() const {
    switch (kind_) {
    case Kind::Null: return "null";
    case Kind::Undefined: return "undefined";
    case Kind::Bool: return b_ ? "true" : "false";
    case Kind::Int: return std::to_string(i_);
    case Kind::Long: return std::to_string(l_);
    case Kind::Float: {
        char b[64]; std::snprintf(b, sizeof(b), "%g", f_); return b;
    }
    case Kind::Double: {
        char b[64]; std::snprintf(b, sizeof(b), "%g", d_); return b;
    }
    default: return s_;
    }
}

} // namespace yam
