#pragma once

// Graphviz export. Coloured by default: distinct fill colour per node
// kind plus a legend cluster. `colored == false` reproduces the plain
// Review II output (`--no-color`).

#include <ostream>

#include "graph.h"

namespace cgdb {

// Writes the whole graph as a digraph to `out`.
void export_dot(const Graph& g, std::ostream& out, bool colored = true);

}  // namespace cgdb
