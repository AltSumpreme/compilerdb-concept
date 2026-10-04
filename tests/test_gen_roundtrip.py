#!/usr/bin/env python3
"""test_gen_roundtrip: generate a ~200-file project with
tools/gen_project.py (no compiler needed), ingest it, and compare CLI
`downstream` of 2 headers against the recursive-grep reference."""

import json
import os
import subprocess
import sys

BIN, GEN_SCRIPT, TMP = sys.argv[1], sys.argv[2], sys.argv[3]

GREP_INCLUDE_ARGS = [
    "--include=*.h", "--include=*.hpp", "--include=*.hh",
    "--include=*.c", "--include=*.cc", "--include=*.cpp", "--include=*.cxx",
]


def grep_closure(root, basename):
    found, frontier = set(), [basename]
    while frontier:
        nxt = []
        for b in frontier:
            pattern = r'#[[:space:]]*include[[:space:]]*[<"][^>"]*%s[>"]' % b
            out = subprocess.run(
                ["grep", "-rl", "-E", pattern, root] + GREP_INCLUDE_ARGS,
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
    proj = os.path.join(TMP, "gen_roundtrip")
    subprocess.run([sys.executable, GEN_SCRIPT, "--files", "100",
                    "--out", proj, "--seed", "3"], check=True,
                   capture_output=True)

    db = os.path.join(TMP, "gen_roundtrip.cgdb")
    if os.path.exists(db):
        os.remove(db)
    cli(["ingest", "--db", db, "--compile-commands",
         os.path.join(proj, "build", "compile_commands.json"),
         "--root", proj])

    with open(os.path.join(proj, "build", "compile_commands.json")) as f:
        cc = json.load(f)
    obj_of = {}
    for e in cc:
        d = e["directory"]
        src = e["file"]
        if not os.path.isabs(src):
            src = os.path.normpath(os.path.join(d, src))
        out = e["output"]
        if not os.path.isabs(out):
            out = os.path.normpath(os.path.join(d, out))
        obj_of[src] = out

    failures = 0
    for name in ("mod_0030.h", "mod_0095.h"):
        header = os.path.join(proj, "include", name)
        expected = grep_closure(proj, name)
        expected.discard(header)
        expected |= {obj_of[s] for s in list(expected) if s in obj_of}
        got = set(cli(["downstream", "--db", db, header]).split())
        if got != expected:
            failures += 1
            print(f"MISMATCH downstream({name}):")
            print(f"  only in grep reference: {sorted(expected - got)[:8]}")
            print(f"  only in compilegraphdb: {sorted(got - expected)[:8]}")
        else:
            print(f"ok downstream({name}): {len(got)} nodes match")

    if failures:
        return 1
    print("test_gen_roundtrip: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
