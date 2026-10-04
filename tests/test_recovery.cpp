// test_recovery: ingest the sample project -> record node/edge counts ->
// close, reopen -> counts must be identical (mmap replay round-trip).

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "db.h"
#include "ingest.h"

namespace fs = std::filesystem;
using namespace cgdb;

namespace {
int failures = 0;
#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n"; \
            ++failures;                                          \
        }                                                        \
    } while (0)

std::vector<std::string> sorted_downstream(const Graph& g, const std::string& h) {
    std::vector<std::string> names;
    for (NodeId id : g.downstream(h)) names.push_back(g.nodes()[id].name);
    std::sort(names.begin(), names.end());
    return names;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: test_recovery <sample_project_dir> <tmp_dir>\n";
        return 2;
    }
    std::string sample = argv[1];
    std::string dbpath = (fs::path(argv[2]) / "recovery.cgdb").string();
    fs::remove(dbpath);

    size_t nodes1, edges1;
    std::vector<std::string> down1;
    std::string probe_header;
    {
        Database db = Database::open(dbpath);
        IngestOptions opt;
        opt.compile_commands = sample + "/build/compile_commands.json";
        opt.root = sample;
        ingest(db, opt);
        nodes1 = db.graph().node_count();
        edges1 = db.graph().edge_count();
        CHECK(nodes1 > 0);
        CHECK(edges1 > 0);
        for (const Node& n : db.graph().nodes())
            if (n.kind == NodeKind::Header) { probe_header = n.name; break; }
        down1 = sorted_downstream(db.graph(), probe_header);
    }  // close (destructor)

    {
        Database db = Database::open(dbpath);  // reopen: full mmap replay
        CHECK(db.graph().node_count() == nodes1);
        CHECK(db.graph().edge_count() == edges1);
        for (int k = 1; k <= 4; ++k)
            CHECK(db.graph().node_count(static_cast<NodeKind>(k)) > 0);
        for (int k = 1; k <= 3; ++k)
            CHECK(db.graph().edge_count(static_cast<EdgeKind>(k)) > 0);
        CHECK(sorted_downstream(db.graph(), probe_header) == down1);
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_recovery: ok (" << nodes1 << " nodes, " << edges1
              << " edges round-tripped)\n";
    return 0;
}
