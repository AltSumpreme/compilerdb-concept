// compilegraphdb — persistent, embedded graph database for C/C++ build
// dependencies. Single binary with subcommands; --db defaults to
// graph.cgdb.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "bench.h"
#include "db.h"
#include "export.h"
#include "ingest.h"
#include "server.h"

namespace {

const char* kUsage =
    "compilegraphdb — build dependency graph database\n"
    "usage:\n"
    "  compilegraphdb ingest --compile-commands CC --root R [--db D] [--deps DIR]\n"
    "  compilegraphdb upstream <path> [--db D]\n"
    "  compilegraphdb downstream <path> [--db D]\n"
    "  compilegraphdb stats [--db D]\n"
    "  compilegraphdb cycles [--db D]\n"
    "  compilegraphdb export -o OUT[.dot|.png|.svg] [--db D] [--no-color]\n"
    "  compilegraphdb serve [--db D] [--port P]\n"
    "  compilegraphdb bench --src R [--db D] [--runs N]\n";

// Tiny option parser: --key value pairs, everything else positional.
struct Args {
    std::map<std::string, std::string> opt;
    std::vector<std::string> pos;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 0; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--no-", 0) == 0) {  // boolean flag, no value
            a.opt[s] = "true";
        } else if (s.size() > 1 && s[0] == '-') {  // --key value or -k value
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + s);
            a.opt[s] = argv[++i];
        } else {
            a.pos.push_back(s);
        }
    }
    return a;
}

std::string get(const Args& a, const std::string& key, const std::string& dflt = "") {
    auto it = a.opt.find(key);
    return it == a.opt.end() ? dflt : it->second;
}

void require(const Args& a, const std::string& key) {
    if (a.opt.count(key) == 0) throw std::runtime_error("missing required " + key);
}

int cmd_ingest(const Args& a) {
    require(a, "--compile-commands");
    require(a, "--root");
    cgdb::IngestOptions opt;
    opt.compile_commands = get(a, "--compile-commands");
    opt.root = get(a, "--root");
    opt.deps_dir = get(a, "--deps");  // empty -> defaults to root
    cgdb::Database db = cgdb::Database::open(get(a, "--db", "graph.cgdb"));
    cgdb::ingest(db, opt);
    std::cerr << "ingested: " << db.graph().node_count() << " nodes, "
              << db.graph().edge_count() << " edges\n";
    return 0;
}

// Shared by `upstream` and `downstream`: print one node per line, plus a
// count line on stderr.
int cmd_query(const Args& a, bool down) {
    if (a.pos.empty()) throw std::runtime_error("missing <path> argument");
    cgdb::Database db = cgdb::Database::open(get(a, "--db", "graph.cgdb"));
    const cgdb::Graph& g = db.graph();
    std::vector<cgdb::NodeId> res =
        down ? g.downstream(a.pos[0]) : g.upstream(a.pos[0]);
    std::vector<std::string> names;
    names.reserve(res.size());
    for (cgdb::NodeId id : res) names.push_back(g.nodes()[id].name);
    std::sort(names.begin(), names.end());
    for (const std::string& n : names) std::cout << n << "\n";
    std::cerr << names.size() << " node(s)\n";
    return 0;
}

int cmd_stats(const Args& a) {
    cgdb::Database db = cgdb::Database::open(get(a, "--db", "graph.cgdb"));
    const cgdb::Graph& g = db.graph();
    std::cout << "nodes: " << g.node_count() << "\n"
              << "  source: " << g.node_count(cgdb::NodeKind::Source) << "\n"
              << "  header: " << g.node_count(cgdb::NodeKind::Header) << "\n"
              << "  object: " << g.node_count(cgdb::NodeKind::Object) << "\n"
              << "  flag:   " << g.node_count(cgdb::NodeKind::Flag) << "\n"
              << "edges: " << g.edge_count() << "\n"
              << "  includes:    " << g.edge_count(cgdb::EdgeKind::Includes) << "\n"
              << "  compiles_to: " << g.edge_count(cgdb::EdgeKind::CompilesTo) << "\n"
              << "  uses:        " << g.edge_count(cgdb::EdgeKind::Uses) << "\n";
    return 0;
}

// Circular include report: every SCC of size > 1 and every self-loop
// over `includes` edges, one group per block.
int cmd_cycles(const Args& a) {
    cgdb::Database db = cgdb::Database::open(get(a, "--db", "graph.cgdb"));
    auto groups = db.graph().cycles();
    if (groups.empty()) {
        std::cout << "no circular includes\n";
        return 0;
    }
    size_t members = 0;
    for (size_t i = 0; i < groups.size(); ++i) {
        std::cout << "group " << (i + 1) << " (" << groups[i].size() << " members):\n";
        for (cgdb::NodeId id : groups[i])
            std::cout << "  " << db.graph().nodes()[id].name << "\n";
        members += groups[i].size();
    }
    std::cout << groups.size() << " circular include group(s), " << members
              << " file(s) involved\n";
    return 0;
}

int cmd_export(const Args& a) {
    require(a, "-o");
    std::string out_path = get(a, "-o");
    bool colored = a.opt.count("--no-color") == 0;
    cgdb::Database db = cgdb::Database::open(get(a, "--db", "graph.cgdb"));

    std::string ext;
    size_t dot = out_path.find_last_of('.');
    if (dot != std::string::npos) ext = out_path.substr(dot + 1);

    if (ext == "png" || ext == "svg") {
        // Render via graphviz: write the .dot next to the target, then
        // invoke `dot` if it is installed.
        std::string dot_path = out_path.substr(0, dot) + ".dot";
        {
            std::ofstream f(dot_path);
            if (!f) throw std::runtime_error("cannot write " + dot_path);
            cgdb::export_dot(db.graph(), f, colored);
        }
        std::string cmd = "dot -T" + ext + " '" + dot_path + "' -o '" + out_path + "'";
        if (std::system(cmd.c_str()) != 0) {
            std::cerr << "warning: could not render with graphviz `dot` "
                         "(not installed?); plain dot kept at "
                      << dot_path << "\n";
            return 0;
        }
        std::cerr << "wrote " << out_path << " (dot source: " << dot_path << ")\n";
        return 0;
    }

    std::ofstream f(out_path);
    if (!f) throw std::runtime_error("cannot write " + out_path);
    cgdb::export_dot(db.graph(), f, colored);
    std::cerr << "wrote " << out_path << "\n";
    return 0;
}

int cmd_serve(const Args& a) {
    int port = std::stoi(get(a, "--port", "8080"));
    cgdb::Database db = cgdb::Database::open(get(a, "--db", "graph.cgdb"));
    std::cerr << "serving " << db.graph().node_count() << " nodes on port " << port
              << "\n";
    cgdb::serve(db.graph(), port);
    return 0;
}

int cmd_bench(const Args& a) {
    require(a, "--src");
    int runs = std::stoi(get(a, "--runs", "20"));
    return cgdb::run_bench(get(a, "--db", "graph.cgdb"), get(a, "--src"), runs,
                           std::cout);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << kUsage;
        return 2;
    }
    std::string sub = argv[1];
    try {
        Args a = parse_args(argc - 2, argv + 2);
        if (sub == "ingest") return cmd_ingest(a);
        if (sub == "upstream") return cmd_query(a, false);
        if (sub == "downstream") return cmd_query(a, true);
        if (sub == "stats") return cmd_stats(a);
        if (sub == "cycles") return cmd_cycles(a);
        if (sub == "export") return cmd_export(a);
        if (sub == "serve") return cmd_serve(a);
        if (sub == "bench") return cmd_bench(a);
        std::cerr << "unknown subcommand: " << sub << "\n" << kUsage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
