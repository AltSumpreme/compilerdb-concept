#pragma once

// In-memory graph core: node store + edge index with FORWARD and REVERSE
// adjacency lists, plus the BFS traversals behind the query API.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cgdb {

enum class NodeKind : uint8_t { Source = 1, Header = 2, Object = 3, Flag = 4 };
enum class EdgeKind : uint8_t { Includes = 1, CompilesTo = 2, Uses = 3 };

const char* to_string(NodeKind k);
const char* to_string(EdgeKind k);

// A path seen under several kinds keeps the strongest one
// (a file that is both header and source is a source).
NodeKind strongest(NodeKind a, NodeKind b);

using NodeId = uint32_t;

struct Node {
    NodeKind kind;
    std::string name;  // identity: the path string
};

class Graph {
public:
    // Adds the node if new; upgrades its kind if `kind` is stronger.
    // Returns the node's id (existing or freshly created).
    NodeId add_node(const std::string& name, NodeKind kind);

    // Adds edge (from -> to, kind); duplicate (from,to,kind) edges are
    // silently ignored. Creates endpoint nodes with the given kinds.
    void add_edge(const std::string& from, NodeKind from_kind,
                  const std::string& to, NodeKind to_kind, EdgeKind kind);

    const Node* find(const std::string& name) const;
    NodeId id_of(const std::string& name) const;  // requires: name exists
    bool has_edge(NodeId from, NodeId to, EdgeKind kind) const;
    const std::vector<Node>& nodes() const { return nodes_; }

    size_t node_count() const { return nodes_.size(); }
    size_t edge_count() const { return edge_count_; }
    size_t node_count(NodeKind k) const;
    size_t edge_count(EdgeKind k) const;

    // BFS following, from each visited node:
    //   upstream:   includes FORWARD + compiles_to REVERSE + uses FORWARD
    //               (everything needed to build the target)
    //   downstream: includes REVERSE + compiles_to FORWARD + uses REVERSE
    //               (everything that breaks when the file changes)
    // The start node itself is excluded from the result.
    std::vector<NodeId> upstream(const std::string& target) const;
    std::vector<NodeId> downstream(const std::string& file) const;

    // Visits every stored edge exactly once, in forward direction.
    template <typename F>
    void for_each_edge(F&& fn) const {
        for (NodeId f = 0; f < out_.size(); ++f)
            for (const Half& h : out_[f]) fn(f, h.other, h.kind);
    }

    // Circular-include detection: Tarjan SCC over the `includes` edges.
    // Returns every SCC of size > 1 and every self-loop (a file that
    // includes itself), each as a group of node ids. Groups are sorted
    // by their lexicographically smallest member name; members are
    // sorted by name — output is deterministic.
    std::vector<std::vector<NodeId>> cycles() const;

private:
    struct Half {  // one adjacency entry
        NodeId other;
        EdgeKind kind;
    };

    std::vector<Node> nodes_;
    std::unordered_map<std::string, NodeId> id_by_name_;
    std::vector<std::vector<Half>> out_;  // forward adjacency, per node
    std::vector<std::vector<Half>> in_;   // reverse adjacency, per node
    std::unordered_set<uint64_t> edge_set_;  // dedup key: from|to|kind
    size_t edge_count_ = 0;

    std::vector<NodeId> bfs(NodeId start, bool down) const;
};

}  // namespace cgdb
