#pragma once

// Minimal hand-rolled JSON parser (RFC 8259): just enough to read
// compile_commands.json, but a complete parser for values, objects,
// arrays, strings (with \uXXXX), numbers, booleans and null.

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cgdb::json {

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    enum Type { Null, Bool, Number, String, ObjectT, ArrayT } type = Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    Object object;
    Array array;

    // Convenience accessors (throw std::runtime_error on type mismatch).
    const Value& at(const std::string& key) const;
    const Value* find(const std::string& key) const;  // nullptr if absent
    const std::string& as_string() const;
    const Array& as_array() const;
};

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses a complete JSON document; trailing garbage is an error.
Value parse(const std::string& text);

// Reads and parses a whole file. Throws std::runtime_error if unreadable.
Value parse_file(const std::string& path);

}  // namespace cgdb::json
