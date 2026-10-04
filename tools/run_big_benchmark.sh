#!/usr/bin/env bash
# Reproduces BENCHMARKS.md end to end: generate -> ingest -> stats ->
# bench, for a ~1200-file project (20 runs, mirroring the Review II
# methodology) and a ~5000-file project (5 runs, to bound grep time).
#
# usage: tools/run_big_benchmark.sh [path-to-compilegraphdb] [workdir]
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$HERE/build/compilegraphdb}"
WORK="${2:-/tmp/cgdb_bigbench}"

run_one() {
  local pairs="$1" runs="$2" name="$3"
  local proj="$WORK/$name"
  echo "=== $name: $((2 * pairs)) files ($pairs modules), $runs runs ==="
  python3 "$HERE/tools/gen_project.py" --files "$pairs" --out "$proj" --seed 11
  rm -f "$proj/graph.cgdb"
  "$BIN" ingest --db "$proj/graph.cgdb" \
    --compile-commands "$proj/build/compile_commands.json" --root "$proj"
  "$BIN" stats --db "$proj/graph.cgdb"
  "$BIN" cycles --db "$proj/graph.cgdb" | tail -1
  time "$BIN" bench --db "$proj/graph.cgdb" --src "$proj" --runs "$runs"
  echo
}

run_one 600 20 bench_1200
run_one 2500 5 bench_5000
