#!/usr/bin/env bash
# Builds the sample project and generates build/compile_commands.json
# (via python3 — no jq needed) plus gcc -MMD depfiles.
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-c++}"
SOURCES=(main.cpp util.cpp config.cpp net.cpp log.cpp)
FLAGS=(-std=c++17 -O2 -Wall)

mkdir -p build
for src in "${SOURCES[@]}"; do
  stem="${src%.cpp}"
  "$CXX" "${FLAGS[@]}" -MMD -MP -MF "build/${stem}.d" -c "$src" -o "build/${stem}.o"
done

python3 - "$PWD" <<'PY'
import json, sys

root = sys.argv[1]
sources = ["main.cpp", "util.cpp", "config.cpp", "net.cpp", "log.cpp"]
entries = []
for src in sources:
    stem = src[:-4]
    entries.append({
        "directory": root,
        "arguments": ["c++", "-std=c++17", "-O2", "-Wall", "-MMD", "-MP",
                      "-MF", f"build/{stem}.d", "-c", src, "-o", f"build/{stem}.o"],
        "file": src,
        "output": f"build/{stem}.o",
    })
with open("build/compile_commands.json", "w") as f:
    json.dump(entries, f, indent=2)
PY

echo "sample project built: $(ls build/*.o | wc -l) objects, depfiles + compile_commands.json in build/"
