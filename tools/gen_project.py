#!/usr/bin/env python3
"""gen_project.py — deterministic synthetic C++ project generator.

Writes include/*.h + src/*.cpp with a random include DAG (plus a few
deliberate header cycles), then computes each source's transitive header
closure ITSELF (no compiler needed) and emits build/*.d depfiles and
build/compile_commands.json with absolute paths.

Usage: gen_project.py --files N --out DIR [--seed S]
"""

import argparse
import json
import os
import random


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", type=int, required=True,
                    help="number of header/source module pairs")
    ap.add_argument("--out", required=True, help="output project directory")
    ap.add_argument("--seed", type=int, default=42, help="RNG seed")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    n = args.files
    out = os.path.abspath(args.out)
    inc_dir = os.path.join(out, "include")
    src_dir = os.path.join(out, "src")
    build_dir = os.path.join(out, "build")
    for d in (inc_dir, src_dir, build_dir):
        os.makedirs(d, exist_ok=True)

    def header(i):
        return f"mod_{i:04d}.h"

    def source(i):
        return f"mod_{i:04d}.cpp"

    # Random include DAG between headers: i may only include j > i, drawn
    # from a window of ~n/10 ahead. The wide window keeps include chains
    # ~20 levels deep (realistic, and keeps the recursive-grep benchmark
    # baseline affordable) while transitive closures stay large.
    window = max(4, n // 10)
    includes = {i: set() for i in range(n)}
    for i in range(n):
        hi = min(n, i + 1 + window)
        if i + 1 >= hi:
            continue
        for _ in range(rng.randint(0, 2)):
            includes[i].add(rng.randrange(i + 1, hi))

    # A few deliberate header cycles, built as guaranteed short loops:
    # a -> a+k -> (a+2k ->) a (the forward hops fit the window; the last
    # edge is the only back edge). With random long-range back edges the
    # sparse chains would almost never close a loop.
    n_cycles = max(1, n // 50)
    for _ in range(n_cycles):
        k = rng.randrange(1, 4)
        length = rng.choice((2, 2, 3))  # mostly 2-cycles, some 3-cycles
        a = rng.randrange(0, n - (length - 1) * k - 1)
        ring = [a + i * k for i in range(length)]
        for i in range(length):
            includes[ring[i]].add(ring[(i + 1) % length])

    # Sources: each includes its own header plus 0-2 random others.
    src_includes = {}
    for i in range(n):
        s = {i}
        for _ in range(rng.randint(0, 2)):
            s.add(rng.randrange(n))
        src_includes[i] = s

    # Transitive header closure per header (over the possibly cyclic graph).
    def closure(start):
        seen, stack = set(), [start]
        while stack:
            cur = stack.pop()
            for nxt in includes[cur]:
                if nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        return seen

    closure_cache = {i: closure(i) for i in range(n)}

    # Write headers.
    for i in range(n):
        deps = sorted(includes[i])
        lines = ["#pragma once", ""]
        lines += [f'#include "{header(j)}"' for j in deps]
        lines += ["", f"namespace gen {{ inline int value_{i:04d}() {{ return {i}; }} }}", ""]
        with open(os.path.join(inc_dir, header(i)), "w") as f:
            f.write("\n".join(lines))

    # Write sources, depfiles and compile commands.
    entries = []
    for i in range(n):
        with open(os.path.join(src_dir, source(i)), "w") as f:
            f.write("\n".join(
                [f'#include "{header(j)}"' for j in sorted(src_includes[i])] +
                ["", f"int use_{i:04d}() {{ return gen::value_{i:04d}(); }}", ""]))

        # .d: object, then source, then the source's TRANSITIVE closure.
        dep_hdrs = set()
        for h in src_includes[i]:
            dep_hdrs.add(h)
            dep_hdrs |= closure_cache[h]
        deps = [os.path.join(src_dir, source(i))] + \
               [os.path.join(inc_dir, header(h)) for h in sorted(dep_hdrs)]
        target = os.path.join(build_dir, f"mod_{i:04d}.o")
        # Wrap long dependency lists with backslash continuations.
        chunks = [deps[k:k + 8] for k in range(0, len(deps), 8)]
        body = (target + ": " + " \\\n  ".join(" ".join(c) for c in chunks) + "\n")
        body += "".join(os.path.join(inc_dir, header(h)) + ":\n"
                        for h in sorted(dep_hdrs))  # -MP style phony targets
        with open(os.path.join(build_dir, f"mod_{i:04d}.d"), "w") as f:
            f.write(body)

        entries.append({
            "directory": out,
            "arguments": ["c++", "-std=c++17", "-O2", "-Iinclude",
                          "-c", os.path.join("src", source(i)),
                          "-o", os.path.join("build", f"mod_{i:04d}.o")],
            "file": os.path.join("src", source(i)),
            "output": os.path.join("build", f"mod_{i:04d}.o"),
        })

    with open(os.path.join(build_dir, "compile_commands.json"), "w") as f:
        json.dump(entries, f, indent=2)

    print(f"generated {n} modules ({2 * n} files) in {out} "
          f"(seed={args.seed}, cycles={n_cycles})")


if __name__ == "__main__":
    main()
