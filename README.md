# CompileGraphDB

A persistent, embedded **graph database for C/C++ build dependencies**,
written from the ground up in C++17 with **zero third-party
dependencies** (the JSON parser, the `.d` depfile parser, CRC32 and the
HTTP server are all hand-rolled). It ingests a build's
`compile_commands.json` and `.d` depfiles into a durable append-only
log, and answers *"what do I need to build X?"* (upstream) and *"what
breaks if I change X?"* (downstream) in microseconds from its in-memory
graph — instead of minutes of recursive `grep`.

## Architecture

Six layers, data flowing strictly downwards:

```
 compile_commands.json      *.d depfiles        source tree
            \                    |                   /
             v                   v                  v
 +-------------------------------------------------------+
 | 1 INPUTS          (build system artifacts)            |
 +-------------------------------------------------------+
 | 2 INGESTION       hand-rolled JSON parser, .d parser, |
 |                   one-time `#include` scan            |
 +-------------------------------------------------------+
 | 3 IN-MEMORY CORE  graph builder -> node store + edge  |
 |                   index (FORWARD + REVERSE adjacency) |
 +-------------------------------------------------------+
 | 4 STORAGE ENGINE  append-only binary log, CRC32 per   |
 |                   record, mmap replay + torn-write    |
 |                   recovery                            |
 +-------------------------------------------------------+
 | 5 QUERY API       upstream BFS, downstream BFS,     |
 |                   cycle detection (Tarjan SCC)      |
 +-------------------------------------------------------+
 | 6 CLIENTS         CLI (ingest/upstream/downstream/    |
 |                   stats/cycles/export/serve/bench),   |
 |                   Graphviz dot, web UI, benchmark     |
 |                   harness                             |
 +-------------------------------------------------------+
```

Graph model — node kinds: `source`, `header`, `object`, `flag` (node
identity is the path string; a path seen as both header and source is a
`source`). Edge kinds: `includes` (file→header), `compiles_to`
(source→object), `uses` (object→flag). Queries never touch the disk
after ingest.

## Build

```sh
cmake -S . -B build -G Ninja   # or plain Makefiles
cmake --build build
ctest --test-dir build --output-on-failure
```

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set, so CompileGraphDB can ingest
its own build (see below).

## Quickstart

```sh
# 1. build the sample project (gcc -MMD depfiles + compile_commands.json)
examples/sample_project/build.sh

# 2. ingest it into a fresh database
./build/compilegraphdb ingest --db graph.cgdb \
    --compile-commands examples/sample_project/build/compile_commands.json \
    --root examples/sample_project

# 3. query
./build/compilegraphdb stats --db graph.cgdb
./build/compilegraphdb downstream --db graph.cgdb "$PWD/examples/sample_project/log.h"
./build/compilegraphdb upstream   --db graph.cgdb "$PWD/examples/sample_project/build/main.o"

# 4. circular-include report (Tarjan SCC over includes edges)
./build/compilegraphdb cycles --db graph.cgdb

# 5. export (coloured by node kind + legend; --no-color for plain dot;
#    renders via graphviz if -o ends in .png/.svg)
./build/compilegraphdb export --db graph.cgdb -o graph.png

# 6. interactive web view (Cytoscape.js): starts on a filtered object
#    view, click nodes to expand their neighborhood, toggle layers by
#    kind, highlight circular includes
./build/compilegraphdb serve --db graph.cgdb --port 8080   # open http://localhost:8080

# 7. benchmark: query API vs recursive grep
./build/compilegraphdb bench --db graph.cgdb --src examples/sample_project --runs 20

# 8. dogfood: ingest CompileGraphDB's own build
./build/compilegraphdb ingest --db self.cgdb \
    --compile-commands build/compile_commands.json --root .
```

A synthetic project generator (deterministic, no compiler needed — it
computes each source's transitive header closure itself) lives in
`tools/`, together with the script that regenerates `BENCHMARKS.md`:

```sh
python3 tools/gen_project.py --files 500 --out /tmp/big --seed 1
tools/run_big_benchmark.sh   # ~1200-file and ~5000-file benchmark runs
```

## Design decisions

1. **C++17, zero external dependencies, CMake build.** Hand-rolled JSON
   parser, `.d` parser, CRC32 and HTTP server. Core code lives in a
   static library `cgdb_core` so tests link it; the CLI is the single
   binary `compilegraphdb` (`--db` defaults to `graph.cgdb`).
2. **Graph model** as above: `source`/`header`/`object`/`flag` nodes;
   `includes`/`compiles_to`/`uses` edges; path-string identity with
   source-wins kind precedence.
3. **Persistence = append-only binary log, CRC32 (IEEE, hand-rolled
   table) per record.** Record layout (little-endian):
   `u32 magic=0x43474442 | u32 crc | u8 type | u32 payload_len | payload`;
   the CRC covers `type || payload_len || payload`. Node payload:
   `u8 kind | u16 len | name`; edge payload: `u8 kind | u16 from_len |
   from | u16 to_len | to`. Every mutation is written through to the log
   first, then applied in memory.
4. **Crash recovery**: on open the log is replayed through a read-only
   `mmap`; replay stops silently at the first truncated/corrupt record
   (torn write), the file is `ftruncate`d back to the last good offset,
   and the graph reflects exactly the last complete record
   (proven by `test_torn_write`).
5. **Fully automatic ingestion**: `compile_commands.json` yields
   source/object/flag nodes + `compiles_to`/`uses` edges (flags =
   tokens starting with `-`; object from `-o`/`output`/derived).
   `.d` depfiles yield `includes` edges per object. During ingest only,
   every existing source/header under the root is scanned once for
   `#include` directives, resolved against known files by path
   suffix/basename — these *direct* edges add the header→header
   structure. Edges are deduplicated on `(from, to, kind)`.
6. **Query semantics**: `upstream(target)` = BFS over `includes`
   forward + `compiles_to` reverse + `uses` forward (everything needed
   to build it); `downstream(file)` = the opposite directions
   (everything that breaks when it changes). The start node is excluded.
7. **CLI subcommands**: `ingest`, `upstream`, `downstream`, `stats`,
   `cycles`, `export`, `serve`, `bench` — see Quickstart. The web server
   is hand-rolled (POSIX sockets, one request per connection,
   `Connection: close`): `GET /` serves the embedded Cytoscape.js page,
   `GET /api/graph` serves the graph as JSON (optional `?kind=` filter),
   `GET /api/neighborhood?path=...` serves a node's 1-hop neighborhood,
   `GET /api/cycles` serves the circular-include groups; everything
   else 404.
8. **Tests** via CTest: storage round-trip (`test_recovery`), torn-write
   recovery (`test_torn_write`), parser exactness against fixtures
   (`test_parser`), query answers vs a recursive-grep reference
   (`test_queries_vs_grep`), HTTP endpoints (`test_http`), Tarjan SCC
   unit test (`test_cycles`) plus the CLI report (`test_cycles_cli`),
   coloured-export smoke (`test_export`), generated-project roundtrip
   (`test_gen_roundtrip`), plus a `bench --runs 2` smoke test.

## Status: Final review (complete)

Everything planned for the final review is implemented and tested:

- storage engine (append-only log, CRC32 per record, mmap replay,
  torn-write truncation)
- ingestion parsers (JSON compile commands, `.d` depfiles, `#include`
  scan)
- graph engine (node store, forward+reverse edge index, BFS)
- query API (`upstream` / `downstream`), validated against recursive
  grep on the sample project and on generated projects
- **circular-include detection**: Tarjan SCC over `includes` edges
  (groups of size > 1 and self-loops), `cycles` CLI command,
  `GET /api/cycles`, and a "Circular includes" panel with highlighting
  in the web page — the sample's `cycle_a.h` ↔ `cycle_b.h` pair is
  reported
- **interactive web page**: filtered initial view (objects + neighbors)
  with a "Load full graph" button, click-to-expand via
  `GET /api/neighborhood?path=...`, per-kind hide/show layer toggles
- **coloured Graphviz export**: per-kind fill colours + legend cluster;
  `--no-color` keeps the plain Review II output; `dot` rendering for
  `.png`/`.svg`
- **large-codebase benchmark**: ~1200-file and ~5000-file generated
  projects, results committed to `BENCHMARKS.md` (reproduce with
  `tools/run_big_benchmark.sh`)
- CLI, benchmark harness — `bench --runs 2` smoke-tested in CTest

