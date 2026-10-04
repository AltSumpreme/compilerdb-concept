#include "json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace cgdb::json {

const Value& Value::at(const std::string& key) const {
    const Value* v = find(key);
    if (!v) throw std::runtime_error("json: missing key '" + key + "'");
    return *v;
}

const Value* Value::find(const std::string& key) const {
    if (type != ObjectT) throw std::runtime_error("json: not an object");
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

const std::string& Value::as_string() const {
    if (type != String) throw std::runtime_error("json: not a string");
    return string;
}

const Array& Value::as_array() const {
    if (type != ArrayT) throw std::runtime_error("json: not an array");
    return array;
}

namespace {

struct Parser {
    const char* p;
    const char* end;

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    char peek() {
        if (p >= end) throw ParseError("json: unexpected end of input");
        return *p;
    }

    char take() {
        char c = peek();
        ++p;
        return c;
    }

    void expect(char c) {
        if (take() != c)
            throw ParseError(std::string("json: expected '") + c + "'");
    }

    Value parse_value() {
        skip_ws();
        char c = peek();
        switch (c) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': {
                Value v;
                v.type = Value::String;
                v.string = parse_string();
                return v;
            }
            case 't': literal("true"); { Value v; v.type = Value::Bool; v.boolean = true; return v; }
            case 'f': literal("false"); { Value v; v.type = Value::Bool; v.boolean = false; return v; }
            case 'n': literal("null"); return Value{};
            default:  return parse_number();
        }
    }

    void literal(const char* word) {
        for (const char* w = word; *w; ++w)
            if (take() != *w) throw ParseError("json: bad literal");
    }

    Value parse_object() {
        Value v;
        v.type = Value::ObjectT;
        take();  // '{'
        skip_ws();
        if (peek() == '}') { take(); return v; }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            expect(':');
            v.object.emplace(std::move(key), parse_value());
            skip_ws();
            char c = take();
            if (c == '}') break;
            if (c != ',') throw ParseError("json: expected ',' or '}' in object");
        }
        return v;
    }

    Value parse_array() {
        Value v;
        v.type = Value::ArrayT;
        take();  // '['
        skip_ws();
        if (peek() == ']') { take(); return v; }
        while (true) {
            v.array.push_back(parse_value());
            skip_ws();
            char c = take();
            if (c == ']') break;
            if (c != ',') throw ParseError("json: expected ',' or ']' in array");
        }
        return v;
    }

    void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    unsigned parse_hex4() {
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
            char c = take();
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') cp |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= unsigned(c - 'A' + 10);
            else throw ParseError("json: bad \\u escape");
        }
        return cp;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            char c = take();
            if (c == '"') break;
            if (c == '\\') {
                char e = take();
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        unsigned cp = parse_hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair
                            if (take() != '\\' || take() != 'u')
                                throw ParseError("json: lone surrogate");
                            unsigned lo = parse_hex4();
                            if (lo < 0xDC00 || lo > 0xDFFF)
                                throw ParseError("json: bad surrogate pair");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        if (cp > 0xFFFF) {  // encode 4-byte UTF-8
                            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            append_utf8(out, cp);
                        }
                        break;
                    }
                    default: throw ParseError("json: bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    Value parse_number() {
        const char* start = p;
        if (p < end && *p == '-') ++p;
        while (p < end && *p >= '0' && *p <= '9') ++p;
        if (p < end && *p == '.') {
            ++p;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        if (p == start || (p == start + 1 && *start == '-'))
            throw ParseError("json: bad number");
        Value v;
        v.type = Value::Number;
        v.number = std::strtod(std::string(start, p).c_str(), nullptr);
        return v;
    }
};

}  // namespace

Value parse(const std::string& text) {
    Parser parser{text.data(), text.data() + text.size()};
    Value v = parser.parse_value();
    parser.skip_ws();
    if (parser.p != parser.end)
        throw ParseError("json: trailing garbage after document");
    return v;
}

Value parse_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

}  // namespace cgdb::json
