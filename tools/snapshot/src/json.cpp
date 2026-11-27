#include "json.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <system_error>

namespace tessera::snapshot_detail {

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t extra = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 4;
        if (extra == 4 || i + extra >= text.size()) return false;
        std::uint32_t scalar = extra == 0 ? c : extra == 1 ? c & 0x1f : extra == 2 ? c & 0x0f : c & 0x07;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        constexpr std::array<std::uint32_t, 4> minimum{0, 0x80, 0x800, 0x10000};
        if (scalar < minimum[extra] || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
        i += extra + 1;
    }
    return true;
}

bool identity(std::string_view text) {
    if (text.empty() || text.size() > 256 || !valid_utf8(text)) return false;
    for (const char c : text)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    return true;
}

namespace {

class Parser {
public:
    Parser(std::string_view text, std::size_t value_limit) : text_(text), remaining_(value_limit) {}

    Result<JsonValue> run() {
        if (!valid_utf8(text_)) return fail(0, "Member text is not valid UTF-8.");
        JsonValue root;
        skip();
        if (!value(root, 0)) return {std::nullopt, std::move(errors_)};
        skip();
        if (at_ < text_.size()) return fail(at_, "Unexpected text after the JSON value.");
        return {std::move(root), {}};
    }

private:
    Result<JsonValue> fail(std::size_t offset, std::string message) {
        error(offset, std::move(message));
        return {std::nullopt, std::move(errors_)};
    }
    bool error(std::size_t offset, std::string message) {
        if (errors_.empty()) {
            errors_.push_back({"invalid_member", Severity::error, "", std::move(message)});
            errors_.back().byte_offset = offset;
        }
        return false;
    }
    void skip() {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }
    bool literal(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return error(at_, "Invalid JSON literal.");
        at_ += word.size();
        return true;
    }

    bool value(JsonValue& out, std::size_t depth) {
        if (remaining_ == 0) return error(at_, "The member holds too many JSON values.");
        --remaining_;
        if (at_ >= text_.size()) return error(at_, "Unexpected end of JSON text.");
        switch (text_[at_]) {
            case '{':
                return object(out, depth);
            case '[':
                return array(out, depth);
            case '"': {
                std::string text;
                if (!string(text)) return false;
                out.value = std::move(text);
                return true;
            }
            case 't':
                out.value = true;
                return literal("true");
            case 'f':
                out.value = false;
                return literal("false");
            case 'n':
                out.value = nullptr;
                return literal("null");
            default:
                return number_value(out);
        }
    }

    bool object(JsonValue& out, std::size_t depth) {
        if (depth >= max_json_depth) return error(at_, "JSON nesting is too deep.");
        ++at_;
        JsonValue::Object result;
        skip();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            out.value = std::move(result);
            return true;
        }
        for (;;) {
            skip();
            if (at_ >= text_.size() || text_[at_] != '"') return error(at_, "Expected an object key.");
            const auto key_at = at_;
            std::string key;
            if (!string(key)) return false;
            skip();
            if (at_ >= text_.size() || text_[at_] != ':') return error(at_, "Expected ':' after an object key.");
            ++at_;
            skip();
            JsonValue item;
            if (!value(item, depth + 1)) return false;
            if (!result.emplace(std::move(key), std::move(item)).second) return error(key_at, "Duplicate object key.");
            skip();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
                out.value = std::move(result);
                return true;
            }
            return error(at_, "Expected ',' or '}' in an object.");
        }
    }

    bool array(JsonValue& out, std::size_t depth) {
        if (depth >= max_json_depth) return error(at_, "JSON nesting is too deep.");
        ++at_;
        JsonValue::Array result;
        skip();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            out.value = std::move(result);
            return true;
        }
        for (;;) {
            skip();
            JsonValue item;
            if (!value(item, depth + 1)) return false;
            result.push_back(std::move(item));
            skip();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
                out.value = std::move(result);
                return true;
            }
            return error(at_, "Expected ',' or ']' in an array.");
        }
    }

    bool hex4(std::uint32_t& out) {
        if (at_ + 4 > text_.size()) return error(at_, "Incomplete \\u escape.");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f')
                out |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out |= std::uint32_t(c - 'A' + 10);
            else
                return error(at_ - 1, "Invalid \\u escape.");
        }
        return true;
    }

    static void append(std::string& out, std::uint32_t scalar) {
        if (scalar < 0x80) {
            out.push_back(char(scalar));
        } else if (scalar < 0x800) {
            out.push_back(char(0xc0 | (scalar >> 6)));
            out.push_back(char(0x80 | (scalar & 0x3f)));
        } else if (scalar < 0x10000) {
            out.push_back(char(0xe0 | (scalar >> 12)));
            out.push_back(char(0x80 | ((scalar >> 6) & 0x3f)));
            out.push_back(char(0x80 | (scalar & 0x3f)));
        } else {
            out.push_back(char(0xf0 | (scalar >> 18)));
            out.push_back(char(0x80 | ((scalar >> 12) & 0x3f)));
            out.push_back(char(0x80 | ((scalar >> 6) & 0x3f)));
            out.push_back(char(0x80 | (scalar & 0x3f)));
        }
    }

    bool string(std::string& out) {
        ++at_; // Opening quote.
        for (;;) {
            if (at_ >= text_.size()) return error(at_, "Unterminated string.");
            const char c = text_[at_];
            if (c == '"') {
                ++at_;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) return error(at_, "Control character in a string.");
            if (c != '\\') {
                out.push_back(c);
                ++at_;
                continue;
            }
            if (++at_ >= text_.size()) return error(at_, "Unterminated escape.");
            const char e = text_[at_++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    const auto escape_at = at_ - 2;
                    std::uint32_t scalar = 0;
                    if (!hex4(scalar)) return false;
                    if (scalar >= 0xdc00 && scalar <= 0xdfff) return error(escape_at, "Unpaired surrogate escape.");
                    if (scalar >= 0xd800 && scalar <= 0xdbff) {
                        std::uint32_t low = 0;
                        if (text_.substr(at_, 2) != "\\u") return error(escape_at, "Unpaired surrogate escape.");
                        at_ += 2;
                        if (!hex4(low)) return false;
                        if (low < 0xdc00 || low > 0xdfff) return error(escape_at, "Unpaired surrogate escape.");
                        scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low - 0xdc00);
                    }
                    append(out, scalar);
                    break;
                }
                default:
                    return error(at_ - 2, "Invalid escape.");
            }
        }
    }

    bool number_value(JsonValue& out) {
        const auto begin = at_;
        const auto digits = [&] {
            const auto start = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            return at_ - start;
        };
        if (at_ < text_.size() && text_[at_] == '-') ++at_;
        if (at_ >= text_.size() || text_[at_] < '0' || text_[at_] > '9') return error(begin, "Invalid JSON value.");
        if (text_[at_] == '0')
            ++at_;
        else
            digits();
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            if (!digits()) return error(begin, "Invalid number.");
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
            if (!digits()) return error(begin, "Invalid number.");
        }
        double result = 0;
        const auto [end, ec] = std::from_chars(text_.data() + begin, text_.data() + at_, result);
        if (ec != std::errc{} || end != text_.data() + at_ || !std::isfinite(result))
            return error(begin, "Number is out of range.");
        out.value = result;
        return true;
    }

    std::string_view text_;
    std::size_t at_ = 0;
    std::size_t remaining_;
    std::vector<Diagnostic> errors_;
};

} // namespace

Result<JsonValue> parse_json(std::string_view text, std::size_t value_limit) { return Parser(text, value_limit).run(); }

std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xf];
                    out += hex[c & 0xf];
                } else {
                    out += c;
                }
        }
    }
    return out + '"';
}

std::string number(double value) {
    if (value == 0) value = 0; // Spell negative zero as 0.
    std::array<char, 32> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return ec == std::errc{} ? std::string(buffer.data(), end) : "null";
}

std::string write_json(const JsonValue& value) {
    struct Writer {
        std::string operator()(std::nullptr_t) const { return "null"; }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(double d) const { return number(d); }
        std::string operator()(const std::string& s) const { return quote(s); }
        std::string operator()(const JsonValue::Array& a) const {
            std::string out = "[";
            for (std::size_t i = 0; i < a.size(); ++i) out += (i ? ", " : "") + write_json(a[i]);
            return out + "]";
        }
        std::string operator()(const JsonValue::Object& o) const {
            std::string out = "{";
            bool first = true;
            for (const auto& [key, item] : o) {
                out += (first ? "" : ", ") + quote(key) + ": " + write_json(item);
                first = false;
            }
            return out + "}";
        }
    };
    return std::visit(Writer{}, value.value);
}

} // namespace tessera::snapshot_detail
