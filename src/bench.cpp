#include "bench.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "db.h"

namespace cgdb {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    out += "'";
    return out;
}

// One grep for a whole BFS frontier: basenames batched by alternation.
// The result set is identical to running one grep per basename (the
// report's reference one-liner: files whose #include line mentions
// BASENAME before the closing > or ") and unioning, but costs one
// process per level instead of one per name — this is what keeps the
// recursive-grep baseline feasible on 5000-file projects, and it only
// makes the baseline FASTER, i.e. the comparison against the graph
// database stays conservative.
std::string grep_command_batch(const std::string& root,
                               const std::vector<std::string>& basenames) {
    std::string alt;
    for (const std::string& b : basenames) {
        if (!alt.empty()) alt += "|";
        alt += b;
    }
    return "grep -rl -E '#[[:space:]]*include[[:space:]]*[<\"][^>\"]*(" + alt +
           ")[>\"]' " + shell_quote(root) +
           " --include=*.h --include=*.hpp --include=*.hh --include=*.c"
           " --include=*.cc --include=*.cpp --include=*.cxx 2>/dev/null";
}

// Runs `cmd`, returns stdout split into lines.
std::vector<std::string> run_lines(const std::string& cmd) {
    std::vector<std::string> lines;
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) return lines;
    char buf[4096];
    std::string cur;
    while (size_t n = std::fread(buf, 1, sizeof buf, pipe)) {
        for (size_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (!cur.empty()) lines.push_back(cur);
                cur.clear();
            } else {
                cur += buf[i];
            }
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    ::pclose(pipe);
    return lines;
}

std::string base_name(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::set<std::string> grep_include_closure(const std::string& root,
                                           const std::string& basename) {
    std::set<std::string> found;
    std::vector<std::string> frontier{basename};
    while (!frontier.empty()) {
        std::vector<std::string> next;
        for (const std::string& f : run_lines(grep_command_batch(root, frontier))) {
            if (found.insert(f).second) next.push_back(base_name(f));
        }
        frontier = std::move(next);
    }
    return found;
}

int run_bench(const std::string& db_path, const std::string& src_root, int runs,
              std::ostream& out) {
    // --- graph build/load time (mmap replay of the log) ---
    Clock::time_point t0 = Clock::now();
    Database db = Database::open(db_path);
    double load_ms = ms_since(t0);
    const Graph& g = db.graph();

    // --- sample up to 8 headers present in the db ---
    std::vector<std::string> headers;
    for (const Node& n : g.nodes())
        if (n.kind == NodeKind::Header) headers.push_back(n.name);
    std::sort(headers.begin(), headers.end());
    if (headers.size() > 8) {
        std::vector<std::string> picked;
        for (int i = 0; i < 8; ++i)
            picked.push_back(headers[size_t(i) * headers.size() / 8]);
        headers = std::move(picked);
    }
    if (headers.empty()) {
        std::cerr << "bench: no headers in the database\n";
        return 1;
    }

    // --- warm the OS file cache with one full grep, before any timing ---
    (void)run_lines("grep -rl -E '#[[:space:]]*include[[:space:]]*[<\"][^>\"]*[>\"]' " +
                    shell_quote(src_root) +
                    " --include=*.h --include=*.hpp --include=*.hh --include=*.c"
                    " --include=*.cc --include=*.cpp --include=*.cxx 2>/dev/null");

    double cgdb_down_ms = 0, cgdb_up_ms = 0, grep_down_ms = 0;
    for (const std::string& h : headers) {
        // warm-up (untimed) through the query API
        (void)g.downstream(h);
        (void)g.upstream(h);
        for (int r = 0; r < runs; ++r) {
            Clock::time_point t = Clock::now();
            (void)g.downstream(h);
            cgdb_down_ms += ms_since(t);
            t = Clock::now();
            (void)g.upstream(h);
            cgdb_up_ms += ms_since(t);
            t = Clock::now();
            (void)grep_include_closure(src_root, base_name(h));
            grep_down_ms += ms_since(t);
        }
    }
    double n = double(headers.size()) * runs;
    cgdb_down_ms /= n;
    cgdb_up_ms /= n;
    grep_down_ms /= n;

    auto fmt = [](double ms) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.3f ms", ms);
        return std::string(buf);
    };
    out << "Benchmark over " << headers.size() << " sampled header(s), " << runs
        << " run(s) each (averaged, warm-up excluded).\n\n";
    out << "| Measurement | CompileGraphDB | grep based search |\n";
    out << "| --- | --- | --- |\n";
    out << "| Graph build/load (mmap replay) | " << fmt(load_ms) << " | n/a |\n";
    out << "| Avg downstream(file) query | " << fmt(cgdb_down_ms) << " | "
        << fmt(grep_down_ms) << " |\n";
    out << "| Avg upstream(target) query | " << fmt(cgdb_up_ms) << " | n/a |\n";
    return 0;
}

}  // namespace cgdb
