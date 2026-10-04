#include "export.h"

namespace cgdb {

namespace {

std::string dot_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// One palette for the whole project (same colours as the web page).
const char* kind_color(NodeKind k) {
    switch (k) {
        case NodeKind::Source: return "#4C8BF5";  // blue
        case NodeKind::Header: return "#F5A623";  // orange
        case NodeKind::Object: return "#7ED321";  // green
        case NodeKind::Flag:   return "#BD10E0";  // purple
    }
    return "#999999";
}

}  // namespace

void export_dot(const Graph& g, std::ostream& out, bool colored) {
    out << "digraph compilegraph {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box];\n";
    const auto& nodes = g.nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        out << "  n" << i << " [label=\"" << dot_escape(nodes[i].name) << "\"";
        if (colored)
            out << ", style=filled, fillcolor=\"" << kind_color(nodes[i].kind)
                << "\"";
        out << "];\n";
    }
    g.for_each_edge([&](NodeId from, NodeId to, EdgeKind kind) {
        out << "  n" << from << " -> n" << to << " [label=\"" << to_string(kind)
            << "\"];\n";
    });
    if (colored) {
        out << "  subgraph cluster_legend {\n";
        out << "    label=\"Legend\";\n";
        out << "    style=filled; fillcolor=\"#F5F5F5\";\n";
        for (NodeKind k : {NodeKind::Source, NodeKind::Header, NodeKind::Object,
                           NodeKind::Flag}) {
            out << "    legend_" << to_string(k) << " [label=\"" << to_string(k)
                << "\", style=filled, fillcolor=\"" << kind_color(k) << "\"];\n";
        }
        out << "  }\n";
    }
    out << "}\n";
}

}  // namespace cgdb
