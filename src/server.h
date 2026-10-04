#pragma once

// Hand-rolled HTTP server (POSIX sockets, one request per connection,
// `Connection: close`). Routes:
//   GET /                                    -> embedded Cytoscape.js page
//   GET /api/graph[?kind=source|header|object|flag]
//        -> whole graph as JSON, or with ?kind=K: nodes of kind K plus
//           their 1-hop neighbors and all edges incident to kind-K nodes
//   GET /api/neighborhood?path=<urlencoded>  -> 1-hop neighborhood (both
//           directions) of one node, same JSON shape; 404 if unknown
//   GET /api/cycles   -> {"cycles":[[path,...],...]} circular includes
//   anything else     -> 404

#include <string>

#include "graph.h"

namespace cgdb {

// {"nodes":[{"id","label","kind"}...],"edges":[{"from","to","kind"}...]}
// with numeric (global) node ids.
std::string graph_to_json(const Graph& g);

// Same shape: nodes of kind `kind` plus their direct neighbors, edges
// incident to them. Unknown kind -> empty graph.
std::string graph_to_json(const Graph& g, const std::string& kind);

// 1-hop neighborhood of `path`. Returns false when `path` is unknown.
bool neighborhood_json(const Graph& g, const std::string& path, std::string& out);

// {"cycles":[[path,...],...]} — circular include groups.
std::string cycles_json(const Graph& g);

// Serves the graph on the given port forever (until killed).
// Throws std::runtime_error on socket errors.
void serve(const Graph& g, int port);

}  // namespace cgdb
