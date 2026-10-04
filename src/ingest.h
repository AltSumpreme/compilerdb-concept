#pragma once

// Ingestion pipeline: compile_commands.json (hand-rolled JSON parser),
// depfiles (*.d, hand-rolled parser) and a one-time `#include` scan of
// every existing source/header under the project root. After ingest,
// queries never touch the disk again.

#include <string>
#include <vector>

#include "db.h"

namespace cgdb {

struct IngestOptions {
    std::string compile_commands;  // path to compile_commands.json
    std::string root;              // project root (relative .d paths resolve here)
    std::string deps_dir;          // recursive *.d search dir (defaults to root)
};

// Classifies a path by extension: source / header / object.
NodeKind kind_for_path(const std::string& path);

// Splits a shell command line into words (quotes and backslash escapes).
std::vector<std::string> shell_split(const std::string& command);

// Extracts compiler flags (tokens starting with '-') from a command's
// tokens; skips the driver name and the argument-taking options
// (-o, -MF, -I foo, -D foo, ...). `tokens` must not be empty.
// `*output` (when non-null) receives the -o argument if present.
std::vector<std::string> extract_flags(const std::vector<std::string>& tokens,
                                       std::string* output);

// Normalizes a path to an absolute, lexically normal form. Relative
// paths are resolved against `base`.
std::string normalize_path(const std::string& path, const std::string& base);

// Runs the full ingestion into `db`.
void ingest(Database& db, const IngestOptions& opt);

}  // namespace cgdb
