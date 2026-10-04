#include "db.h"

namespace cgdb {

Database Database::open(const std::string& path) {
    Database db;
    db.log_ = Log::open(path, [&db](const Record& r) {
        if (r.type == kRecNode) {
            db.graph_.add_node(r.from, static_cast<NodeKind>(r.kind));
        } else if (r.type == kRecEdge) {
            // Endpoint kinds are not stored in edge records. A node
            // record is always logged BEFORE the first edge that names
            // it (add_edge logs its endpoints first), so at replay time
            // both endpoints already exist with their true kinds —
            // reuse them rather than guessing (a guess could clash with
            // the strongest-kind rule and corrupt e.g. flag nodes).
            auto known_kind = [&db](const std::string& name) {
                const Node* n = db.graph_.find(name);
                return n ? n->kind : NodeKind::Header;
            };
            db.graph_.add_edge(r.from, known_kind(r.from), r.to, known_kind(r.to),
                               static_cast<EdgeKind>(r.kind));
        }
    });
    return db;
}

void Database::add_node(const std::string& name, NodeKind kind) {
    // Skip logging when this exact (name, kind) fact is already stored;
    // a kind upgrade is a new fact and is logged as a fresh node record.
    const Node* existing = graph_.find(name);
    if (existing && existing->kind == kind) return;
    log_.append_node(kind, name);   // write ...
    graph_.add_node(name, kind);    // ... then apply in memory
}

void Database::add_edge(const std::string& from, NodeKind from_kind,
                        const std::string& to, NodeKind to_kind, EdgeKind kind) {
    add_node(from, from_kind);
    add_node(to, to_kind);
    if (graph_.has_edge(graph_.id_of(from), graph_.id_of(to), kind)) return;
    log_.append_edge(kind, from, to);                     // write ...
    graph_.add_edge(from, from_kind, to, to_kind, kind);  // ... then apply
}

}  // namespace cgdb
