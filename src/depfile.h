#pragma once

// Hand-rolled GNU-make-style depfile (.d) parser.
// Handles: backslash-newline continuations, escaped characters
// (\ , \\, \#, \:), $$ escapes, comments, multiple stanzas and the
// empty phony targets produced by -MP ("header.h:" with no deps).

#include <string>
#include <vector>

namespace cgdb {

struct DepStanza {
    std::vector<std::string> targets;  // usually exactly one (the object)
    std::vector<std::string> deps;     // deps[0] is the source, rest headers
};

// Parses depfile text into stanzas.
std::vector<DepStanza> parse_depfile(const std::string& text);

// Reads and parses a depfile. Throws std::runtime_error if unreadable.
std::vector<DepStanza> parse_depfile_file(const std::string& path);

}  // namespace cgdb
