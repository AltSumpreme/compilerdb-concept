#pragma once

// Database = Graph (in memory) + Log (on disk), with write-through
// semantics: every mutation is appended to the log first, then applied
// to the in-memory graph.

#include <string>

#include "graph.h"
#include "log.h"

namespace cgdb {

class Database {
public:
    // Opens (or creates) the database at `path`, replaying the log into
    // the graph and truncating any torn tail.
    static Database open(const std::string& path);

    void add_node(const std::string& name, NodeKind kind);
    void add_edge(const std::string& from, NodeKind from_kind,
                  const std::string& to, NodeKind to_kind, EdgeKind kind);

    const Graph& graph() const { return graph_; }

private:
    Graph graph_;
    Log log_;
};

}  // namespace cgdb
