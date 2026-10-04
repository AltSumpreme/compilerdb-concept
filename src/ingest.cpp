#include "ingest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#include "depfile.h"
#include "json.h"

namespace cgdb {

namespace fs = std::filesystem;

namespace {

bool is_source_ext(const std::string& e) {
    return e == ".c" || e == ".cc" || e == ".cpp" || e == ".cxx" || e == ".C";
}
bool is_header_ext(const std::string& e) {
    return e == ".h" || e == ".hpp" || e == ".hh" || e == ".hxx";
}

}  // namespace

NodeKind kind_for_path(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    if (is_source_ext(e)) return NodeKind::Source;
    if (e == ".o" || e == ".obj") return NodeKind::Object;
    return NodeKind::Header;  // headers and anything else in a depfile
}

std::string normalize_path(const std::string& path, const std::string& base) {
    fs::path p(path);
    if (p.is_relative()) p = fs::path(base) / p;
    return p.lexically_normal().string();
}

std::vector<std::string> shell_split(const std::string& command) {
    std::vector<std::string> words;
    std::string cur;
    bool in_word = false, in_squote = false, in_dquote = false;
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        if (in_squote) {
            if (c == '\'') in_squote = false;
            else cur += c;
            continue;
        }
        if (in_dquote) {
            if (c == '"') in_dquote = false;
            else if (c == '\\' && i + 1 < command.size() &&
                     (command[i + 1] == '"' || command[i + 1] == '\\')) {
                cur += command[++i];
            } else {
                cur += c;
            }
            continue;
        }
        if (c == '\'') { in_squote = true; in_word = true; continue; }
        if (c == '"')  { in_dquote = true; in_word = true; continue; }
        if (c == '\\' && i + 1 < command.size()) {
            cur += command[++i];
            in_word = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (in_word) {
                words.push_back(cur);
                cur.clear();
                in_word = false;
            }
            continue;
        }
        cur += c;
        in_word = true;
    }
    if (in_word) words.push_back(cur);
    return words;
}

std::vector<std::string> extract_flags(const std::vector<std::string>& tokens,
                                       std::string* output) {
    // Options that consume the NEXT token as their argument: neither the
    // option nor its argument becomes a flag node.
    static const std::set<std::string> kTakesArg = {
        "-o", "-MF", "-MT", "-MQ", "-include", "-imacros", "-isystem",
        "-iquote", "-idirafter", "-iprefix", "-iwithprefix", "-x", "-std",
        "-I", "-D", "-U", "-L", "-l", "-isysroot", "--sysroot", "-target",
        "-B", "-G", "-T",
    };
    std::vector<std::string> flags;
    for (size_t i = 1; i < tokens.size(); ++i) {  // tokens[0] is the driver
        const std::string& t = tokens[i];
        if (kTakesArg.count(t)) {
            if (t == "-o" && output && i + 1 < tokens.size()) *output = tokens[i + 1];
            ++i;  // swallow the argument
            continue;
        }
        if (t.rfind("-o", 0) == 0 && t.size() > 2) {  // joined -ofile.o
            if (output) *output = t.substr(2);
            continue;
        }
        if (t == "-c" || t == "-S" || t == "-E") continue;  // universal noise
        if (!t.empty() && t[0] == '-' && t != "-") flags.push_back(t);
    }
    return flags;
}

namespace {

// Index of existing source/header files under the root, used to resolve
// #include directives textually (path suffix, then basename — matching
// what a recursive grep for the basename would find).
class FileIndex {
public:
    explicit FileIndex(const std::string& root) {
        std::error_code ec;
        fs::recursive_directory_iterator it(
            root, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            std::string e = it->path().extension().string();
            if (!is_source_ext(e) && !is_header_ext(e)) continue;
            std::string p = it->path().lexically_normal().string();
            files_.push_back(p);
            by_basename_[it->path().filename().string()].push_back(p);
        }
        std::sort(files_.begin(), files_.end());
        for (auto& [base, v] : by_basename_) std::sort(v.begin(), v.end());
    }

    const std::vector<std::string>& files() const { return files_; }

    // Resolves an include string like "util.h" or "sub/util.h" against
    // known files; returns "" when unresolvable (e.g. system headers).
    std::string resolve(const std::string& include) const {
        // Path-suffix match first: "/sub/util.h" ends the full path.
        std::string suffix = "/" + include;
        const std::string* best = nullptr;
        for (const std::string& f : files_) {
            if (f.size() >= suffix.size() &&
                f.compare(f.size() - suffix.size(), suffix.size(), suffix) == 0) {
                if (!best || f < *best) best = &f;
            }
        }
        if (best) return *best;
        // Then plain basename match.
        auto it = by_basename_.find(fs::path(include).filename().string());
        if (it == by_basename_.end() || it->second.empty()) return "";
        // Require the include's last component to be the whole basename.
        if (include.find('/') == std::string::npos) return it->second.front();
        return "";
    }

private:
    std::vector<std::string> files_;
    std::map<std::string, std::vector<std::string>> by_basename_;
};

// Extracts the include target of a line like `  # include "util.h"`;
// returns "" for non-include lines.
std::string parse_include_line(const std::string& line) {
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '#') return "";
    i = line.find_first_not_of(" \t", i + 1);
    if (i == std::string::npos) return "";
    constexpr const char* kw = "include";
    if (line.compare(i, 7, kw) != 0) return "";
    i += 7;
    if (i < line.size() && (isalnum(line[i]) || line[i] == '_')) return "";  // e.g. #included
    i = line.find_first_not_of(" \t", i);
    if (i == std::string::npos || (line[i] != '"' && line[i] != '<')) return "";
    char close = line[i] == '"' ? '"' : '>';
    size_t j = line.find(close, i + 1);
    if (j == std::string::npos) return "";
    return line.substr(i + 1, j - i - 1);
}

void ingest_compile_commands(Database& db, const std::string& cc_path) {
    json::Value doc = json::parse_file(cc_path);
    fs::path cc_dir = fs::absolute(fs::path(cc_path)).parent_path();
    for (const json::Value& entry : doc.as_array()) {
        const json::Value* dir_v = entry.find("directory");
        const json::Value* file_v = entry.find("file");
        if (!dir_v || !file_v) continue;  // not a compile entry; skip
        std::string dir = normalize_path(dir_v->as_string(), cc_dir.string());
        std::string src = normalize_path(file_v->as_string(), dir);

        std::vector<std::string> tokens;
        if (const json::Value* args = entry.find("arguments")) {
            for (const json::Value& a : args->as_array()) tokens.push_back(a.as_string());
        } else if (const json::Value* cmd = entry.find("command")) {
            tokens = shell_split(cmd->as_string());
        }
        if (tokens.empty()) continue;

        std::string output;
        std::vector<std::string> flags = extract_flags(tokens, &output);
        if (const json::Value* out_v = entry.find("output"))
            output = out_v->as_string();
        if (output.empty()) {
            // gcc's default: <stem>.o in the working directory.
            output = fs::path(src).stem().string() + ".o";
        }
        std::string obj = normalize_path(output, dir);

        db.add_edge(src, NodeKind::Source, obj, NodeKind::Object, EdgeKind::CompilesTo);
        for (const std::string& f : flags)
            db.add_edge(obj, NodeKind::Object, f, NodeKind::Flag, EdgeKind::Uses);
    }
}

void ingest_depfile(Database& db, const std::string& path, const std::string& root) {
    for (const DepStanza& stanza : parse_depfile_file(path)) {
        if (stanza.targets.empty() || stanza.deps.empty())
            continue;  // -MP phony target ("header.h:") carries no deps
        std::string obj = normalize_path(stanza.targets.front(), root);
        db.add_node(obj, NodeKind::Object);
        std::string src = normalize_path(stanza.deps.front(), root);
        db.add_node(src, NodeKind::Source);
        for (size_t i = 1; i < stanza.deps.size(); ++i) {
            std::string hdr = normalize_path(stanza.deps[i], root);
            db.add_edge(src, NodeKind::Source, hdr, NodeKind::Header,
                        EdgeKind::Includes);
        }
    }
}

void scan_includes(Database& db, const FileIndex& index) {
    for (const std::string& file : index.files()) {
        std::ifstream in(file);
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            std::string inc = parse_include_line(line);
            if (inc.empty()) continue;
            std::string target = index.resolve(inc);
            if (target.empty() || target == file) continue;
            db.add_edge(file, kind_for_path(file), target, kind_for_path(target),
                        EdgeKind::Includes);
        }
    }
}

}  // namespace

void ingest(Database& db, const IngestOptions& opt) {
    std::string root = fs::absolute(fs::path(opt.root)).lexically_normal().string();
    std::string deps = opt.deps_dir.empty()
                           ? root
                           : fs::absolute(fs::path(opt.deps_dir)).lexically_normal().string();

    ingest_compile_commands(db, opt.compile_commands);

    // Recursively collect *.d files (sorted for deterministic logging).
    std::vector<std::string> depfiles;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        deps, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".d")
            depfiles.push_back(it->path().string());
    }
    std::sort(depfiles.begin(), depfiles.end());
    for (const std::string& d : depfiles) ingest_depfile(db, d, root);

    // One-time textual pass: adds DIRECT include edges (also header->header).
    scan_includes(db, FileIndex(root));
}

}  // namespace cgdb
