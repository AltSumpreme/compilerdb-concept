#!/usr/bin/env python3
"""test_cycles_cli: the `cycles` subcommand on the sample project must
report the cycle_a.h <-> cycle_b.h pair (exit 0), and an acyclic mini
project must report 'no circular includes'."""

import json
import os
import subprocess
import sys

BIN, SAMPLE, TMP = sys.argv[1], sys.argv[2], sys.argv[3]


def cli(args):
    return subprocess.run([BIN] + args, capture_output=True, text=True,
                          check=True).stdout


def main():
    db = os.path.join(TMP, "cycles_cli.cgdb")
    if os.path.exists(db):
        os.remove(db)
    cli(["ingest", "--db", db, "--compile-commands",
         os.path.join(SAMPLE, "build", "compile_commands.json"),
         "--root", SAMPLE])

    out = cli(["cycles", "--db", db])
    ok = True
    if "cycle_a.h" not in out or "cycle_b.h" not in out:
        print("FAIL: cycle members missing from output:\n" + out)
        ok = False
    if "1 circular include group(s), 2 file(s)" not in out:
        print("FAIL: summary line wrong:\n" + out)
        ok = False

    # Acyclic control: tiny project without any include cycle.
    mini = os.path.join(TMP, "cycles_mini")
    os.makedirs(os.path.join(mini, "build"), exist_ok=True)
    with open(os.path.join(mini, "a.cpp"), "w") as f:
        f.write('#include "a.h"\nint a() { return 0; }\n')
    with open(os.path.join(mini, "a.h"), "w") as f:
        f.write("#pragma once\n")
    with open(os.path.join(mini, "build", "a.d"), "w") as f:
        f.write("build/a.o: a.cpp a.h\na.h:\n")
    with open(os.path.join(mini, "build", "compile_commands.json"), "w") as f:
        json.dump([{"directory": mini,
                    "arguments": ["c++", "-c", "a.cpp", "-o", "build/a.o"],
                    "file": "a.cpp", "output": "build/a.o"}], f)
    db2 = os.path.join(TMP, "cycles_mini.cgdb")
    if os.path.exists(db2):
        os.remove(db2)
    cli(["ingest", "--db", db2, "--compile-commands",
         os.path.join(mini, "build", "compile_commands.json"),
         "--root", mini])
    out2 = cli(["cycles", "--db", db2])
    if out2.strip() != "no circular includes":
        print("FAIL: expected 'no circular includes', got:\n" + out2)
        ok = False

    if not ok:
        return 1
    print("test_cycles_cli: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
