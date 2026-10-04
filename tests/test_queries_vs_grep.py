#!/usr/bin/env python3
"""test_queries_vs_grep: the CLI `downstream` answers must match a
recursive-grep reference exactly on the sample project.

Reference semantics (mirrors the graph's downstream BFS):
  * fixpoint of "files whose #include line mentions basename(X)",
    starting from the header's basename (this is what 'grep' thinks
    breaks when the header changes),
  * minus the start header itself,
  * plus the object file of every source in the set (compiles_to),
    read from compile_commands.json.
"""

import json
import os
import subprocess
import sys

BIN, SAMPLE, TMP = sys.argv[1], sys.argv[2], sys.argv[3]

GREP_INCLUDE_ARGS = [
    "--include=*.h", "--include=*.hpp", "--include=*.hh",
    "--include=*.c", "--include=*.cc", "--include=*.cpp", "--include=*.cxx",
]


def grep_closure(basename):
    found, frontier = set(), [basename]
    while frontier:
        nxt = []
        for b in frontier:
            pattern = r'#[[:space:]]*include[[:space:]]*[<"][^>"]*%s[>"]' % b
            out = subprocess.run(
                ["grep", "-rl", "-E", pattern, SAMPLE] + GREP_INCLUDE_ARGS,
                capture_output=True, text=True).stdout
            for line in out.splitlines():
                line = line.strip()
                if line and line not in found:
                    found.add(line)
                    nxt.append(os.path.basename(line))
        frontier = nxt
    return found


def cli(args):
    return subprocess.run([BIN] + args, capture_output=True, text=True,
                          check=True).stdout


def main():
    db = os.path.join(TMP, "qvg.cgdb")
    if os.path.exists(db):
        os.remove(db)
    cli(["ingest", "--db", db, "--compile-commands",
         os.path.join(SAMPLE, "build", "compile_commands.json"),
         "--root", SAMPLE])

    # source -> object map from compile_commands.json (absolute paths)
    with open(os.path.join(SAMPLE, "build", "compile_commands.json")) as f:
        cc = json.load(f)
    obj_of = {}
    for e in cc:
        d = e["directory"]
        src = e["file"]
        if not os.path.isabs(src):
            src = os.path.normpath(os.path.join(d, src))
        out = e.get("output")
        if not out:
            out = os.path.splitext(os.path.basename(src))[0] + ".o"
        if not os.path.isabs(out):
            out = os.path.normpath(os.path.join(d, out))
        obj_of[src] = out

    headers = sorted(
        os.path.join(SAMPLE, f) for f in os.listdir(SAMPLE) if f.endswith(".h"))
    assert headers, "no headers found in sample project"

    failures = 0
    for header in headers:
        expected = grep_closure(os.path.basename(header))
        expected.discard(header)  # start node excluded from results
        expected |= {obj_of[s] for s in list(expected) if s in obj_of}

        got = set(cli(["downstream", "--db", db, header]).split())
        if got != expected:
            failures += 1
            print(f"MISMATCH downstream({header}):")
            print(f"  only in grep reference: {sorted(expected - got)}")
            print(f"  only in compilegraphdb: {sorted(got - expected)}")
        else:
            print(f"ok downstream({os.path.basename(header)}): "
                  f"{len(got)} nodes match")

    if failures:
        print(f"{failures} header(s) mismatched", file=sys.stderr)
        return 1
    print("test_queries_vs_grep: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
