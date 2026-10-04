#pragma once

// Benchmark harness (report Section 5 methodology): the same questions
// answered through the query API vs. a recursive-grep baseline.
//   * sample up to 8 headers present in the db
//   * per header: downstream+upstream via BFS (avg over N runs, after a
//     warm-up run) vs. recursive grep BFS ("what breaks if H changes")
//   * file cache warmed with one full grep before any timing
//   * output: markdown table

#include <ostream>
#include <set>
#include <string>

namespace cgdb {

// The recursive-grep reference: starting from `basename`, repeatedly
// greps the tree for files including any found basename, until fixpoint.
// Returns the set of file paths found (sources and headers only — grep
// cannot see object files). Exposed for reuse by tests.
std::set<std::string> grep_include_closure(const std::string& root,
                                           const std::string& basename);

// Runs the benchmark and prints the markdown table to `out`.
// Returns process exit code.
int run_bench(const std::string& db_path, const std::string& src_root, int runs,
              std::ostream& out);

}  // namespace cgdb
