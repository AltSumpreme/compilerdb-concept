// test_cycles: unit-level Tarjan SCC check on a hand-built graph —
// a 3-cycle, a self-loop and acyclic control structure.

#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "graph.h"

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
}  // namespace

int main() {
    Graph g;
    auto inc = [&](const std::string& a, const std::string& b) {
        g.add_edge(a, NodeKind::Header, b, NodeKind::Header, EdgeKind::Includes);
    };
    // 3-cycle: a -> b -> c -> a
    inc("a.h", "b.h");
    inc("b.h", "c.h");
    inc("c.h", "a.h");
    // self-loop: d includes itself
    inc("d.h", "d.h");
    // acyclic control: e -> f -> a (f reaches the cycle but is not in it)
    inc("e.h", "f.h");
    inc("f.h", "a.h");
    // other edge kinds must not interfere: an object "cycle"
    g.add_edge("x.cpp", NodeKind::Source, "x.o", NodeKind::Object,
               EdgeKind::CompilesTo);
    g.add_edge("x.o", NodeKind::Object, "-O2", NodeKind::Flag, EdgeKind::Uses);

    auto groups = g.cycles();
    std::set<std::set<std::string>> got;
    for (const auto& group : groups) {
        std::set<std::string> names;
        for (NodeId id : group) names.insert(g.nodes()[id].name);
        got.insert(std::move(names));
    }
    CHECK(got.size() == 2);
    CHECK(got.count({"a.h", "b.h", "c.h"}) == 1);
    CHECK(got.count({"d.h"}) == 1);
    // acyclic controls absent
    for (const auto& s : got) {
        CHECK(s.count("e.h") == 0);
        CHECK(s.count("f.h") == 0);
    }

    // Acyclic graph -> no groups.
    Graph dag;
    dag.add_edge("p.h", NodeKind::Header, "q.h", NodeKind::Header,
                 EdgeKind::Includes);
    dag.add_edge("r.h", NodeKind::Header, "q.h", NodeKind::Header,
                 EdgeKind::Includes);
    CHECK(dag.cycles().empty());

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_cycles: ok\n";
    return 0;
}
