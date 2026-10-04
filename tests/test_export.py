#!/usr/bin/env python3
"""test_export: coloured Graphviz export carries per-kind fill colours
and a legend cluster; --no-color reproduces the plain Review II output."""

import os
import subprocess
import sys

BIN, SAMPLE, TMP = sys.argv[1], sys.argv[2], sys.argv[3]


def cli(args):
    return subprocess.run([BIN] + args, capture_output=True, text=True,
                          check=True).stdout


def main():
    db = os.path.join(TMP, "export.cgdb")
    if not os.path.exists(db):
        cli(["ingest", "--db", db, "--compile-commands",
             os.path.join(SAMPLE, "build", "compile_commands.json"),
             "--root", SAMPLE])

    ok = True
    colored = os.path.join(TMP, "colored.dot")
    cli(["export", "--db", db, "-o", colored])
    with open(colored) as f:
        text = f.read()
    for color in ("#4C8BF5", "#F5A623", "#7ED321", "#BD10E0"):
        if f'fillcolor="{color}"' not in text:
            print(f"FAIL: missing fill colour {color}")
            ok = False
    if "cluster_legend" not in text:
        print("FAIL: missing legend cluster")
        ok = False
    for kind in ("source", "header", "object", "flag"):
        if f"legend_{kind}" not in text:
            print(f"FAIL: missing legend node for {kind}")
            ok = False

    plain = os.path.join(TMP, "plain.dot")
    cli(["export", "--db", db, "-o", plain, "--no-color"])
    with open(plain) as f:
        text = f.read()
    if "fillcolor" in text or "cluster_legend" in text:
        print("FAIL: --no-color output is not plain")
        ok = False
    if "digraph compilegraph" not in text:
        print("FAIL: not a digraph")
        ok = False

    if not ok:
        return 1
    print("test_export: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
