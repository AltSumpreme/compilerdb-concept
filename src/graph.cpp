#include "graph.h"

#include <algorithm>
#include <deque>
#include <utility>

namespace cgdb {

const char* to_string(NodeKind k) {
    switch (k) {
        case NodeKind::Source: return "source";
        case NodeKind::Header: return "header";
        case NodeKind::Object: return "object";
        case NodeKind::Flag:   return "flag";
    }
    return "?";
}

const char* to_string(EdgeKind k) {
    switch (k) {
        case EdgeKind::Includes:   return "includes";
        case EdgeKind::CompilesTo: return "compiles_to";
        case EdgeKind::Uses:       return "uses";
    }
    return "?";
}

static int rank(NodeKind k) {
    // Precedence for identity collisions: source beats everything.
    switch (k) {
        case NodeKind::Source: return 4;
        case NodeKind::Object: return 3;
        case NodeKind::Header: return 2;
        case NodeKind::Flag:   return 1;
    }
    return 0;
}

NodeKind strongest(NodeKind a, NodeKind b) { return rank(a) >= rank(b) ? a : b; }

static uint64_t edge_key(NodeId f, NodeId t, EdgeKind kind) {
    return (uint64_t{f} << 34) | (uint64_t{t} << 2) |
           uint64_t{static_cast<uint8_t>(kind)};
}

NodeId Graph::add_node(const std::string& name, NodeKind kind) {
    auto it = id_by_name_.find(name);
    if (it != id_by_name_.end()) {
        Node& n = nodes_[it->second];
        n.kind = strongest(n.kind, kind);
        return it->second;
    }
    NodeId id = static_cast<NodeId>(nodes_.size());
    id_by_name_.emplace(name, id);
    nodes_.push_back(Node{kind, name});
    out_.emplace_back();
    in_.emplace_back();
    return id;
}

void Graph::add_edge(const std::string& from, NodeKind from_kind,
                     const std::string& to, NodeKind to_kind, EdgeKind kind) {
    NodeId f = add_node(from, from_kind);
    NodeId t = add_node(to, to_kind);
    if (!edge_set_.insert(edge_key(f, t, kind)).second)
        return;  // duplicate (from,to,kind)
    out_[f].push_back(Half{t, kind});
    in_[t].push_back(Half{f, kind});
    ++edge_count_;
}

bool Graph::has_edge(NodeId from, NodeId to, EdgeKind kind) const {
    return edge_set_.count(edge_key(from, to, kind)) != 0;
}

const Node* Graph::find(const std::string& name) const {
    auto it = id_by_name_.find(name);
    return it == id_by_name_.end() ? nullptr : &nodes_[it->second];
}

NodeId Graph::id_of(const std::string& name) const {
    return id_by_name_.at(name);
}

size_t Graph::node_count(NodeKind k) const {
    size_t n = 0;
    for (const Node& node : nodes_)
        if (node.kind == k) ++n;
    return n;
}

size_t Graph::edge_count(EdgeKind k) const {
    size_t n = 0;
    for (const auto& adj : out_)
        for (const Half& h : adj)
            if (h.kind == k) ++n;
    return n;
}

// Should the BFS follow this half-edge from the current node?
static bool follow(EdgeKind edge_kind, bool down, bool forward_list) {
    // upstream   = includes fwd + compiles_to rev + uses fwd
    // downstream = includes rev + compiles_to fwd + uses rev
    bool want_forward;
    switch (edge_kind) {
        case EdgeKind::Includes:   want_forward = !down; break;
        case EdgeKind::CompilesTo: want_forward = down;  break;
        case EdgeKind::Uses:       want_forward = !down; break;
        default:                   want_forward = false; break;
    }
    return want_forward == forward_list;
}

std::vector<NodeId> Graph::bfs(NodeId start, bool down) const {
    std::vector<NodeId> result;
    std::vector<char> seen(nodes_.size(), 0);
    std::deque<NodeId> q;
    seen[start] = 1;
    q.push_back(start);
    while (!q.empty()) {
        NodeId cur = q.front();
        q.pop_front();
        for (int list = 0; list < 2; ++list) {
            bool forward_list = (list == 0);
            const auto& adj = forward_list ? out_[cur] : in_[cur];
            for (const Half& h : adj) {
                if (!follow(h.kind, down, forward_list) || seen[h.other])
                    continue;
                seen[h.other] = 1;
                q.push_back(h.other);
                result.push_back(h.other);
            }
        }
    }
    return result;
}

std::vector<NodeId> Graph::upstream(const std::string& target) const {
    auto it = id_by_name_.find(target);
    if (it == id_by_name_.end()) return {};
    return bfs(it->second, /*down=*/false);
}

std::vector<NodeId> Graph::downstream(const std::string& file) const {
    auto it = id_by_name_.find(file);
    if (it == id_by_name_.end()) return {};
    return bfs(it->second, /*down=*/true);
}

std::vector<std::vector<NodeId>> Graph::cycles() const {
    // Iterative Tarjan SCC (recursion depth would grow with the graph).
    const NodeId n = static_cast<NodeId>(nodes_.size());
    std::vector<int> index(n, -1), low(n, 0);
    std::vector<char> on_stack(n, 0);
    std::vector<NodeId> stack;
    std::vector<std::vector<NodeId>> groups;
    int next_index = 0;

    // Work items simulate the call stack: (node, next child slot).
    for (NodeId root = 0; root < n; ++root) {
        if (index[root] != -1) continue;
        std::vector<std::pair<NodeId, size_t>> work{{root, 0}};
        index[root] = low[root] = next_index++;
        stack.push_back(root);
        on_stack[root] = 1;
        while (!work.empty()) {
            NodeId v = work.back().first;
            size_t& ci = work.back().second;
            // Advance to the next `includes` child of v.
            const std::vector<Half>& adj = out_[v];
            while (ci < adj.size() && adj[ci].kind != EdgeKind::Includes) ++ci;
            if (ci < adj.size()) {
                NodeId w = adj[ci].other;
                ++ci;
                if (index[w] == -1) {
                    index[w] = low[w] = next_index++;
                    stack.push_back(w);
                    on_stack[w] = 1;
                    work.emplace_back(w, 0);
                } else if (on_stack[w]) {
                    low[v] = std::min(low[v], index[w]);
                }
            } else {
                // v is finished: propagate lowlink, pop SCC if v is a root.
                work.pop_back();
                if (!work.empty())
                    low[work.back().first] =
                        std::min(low[work.back().first], low[v]);
                if (low[v] == index[v]) {
                    std::vector<NodeId> scc;
                    NodeId w;
                    do {
                        w = stack.back();
                        stack.pop_back();
                        on_stack[w] = 0;
                        scc.push_back(w);
                    } while (w != v);
                    if (scc.size() > 1) {
                        groups.push_back(std::move(scc));
                    } else {
                        // Self-loop? (a file that includes itself)
                        for (const Half& h : out_[v])
                            if (h.kind == EdgeKind::Includes && h.other == v)
                                groups.push_back(std::move(scc));
                    }
                }
            }
        }
    }

    // Deterministic output: members sorted by name, groups by first name.
    for (auto& g : groups)
        std::sort(g.begin(), g.end(), [&](NodeId a, NodeId b) {
            return nodes_[a].name < nodes_[b].name;
        });
    std::sort(groups.begin(), groups.end(), [&](const auto& a, const auto& b) {
        return nodes_[a.front()].name < nodes_[b.front()].name;
    });
    return groups;
}

}  // namespace cgdb
