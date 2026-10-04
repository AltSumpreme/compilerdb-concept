#!/usr/bin/env python3
"""test_http: launch `serve` on a high port; curl -f checks / ,
/api/graph (whole + ?kind= filter), /api/neighborhood and /api/cycles;
then kill the server."""

import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.parse

BIN, SAMPLE, TMP = sys.argv[1], sys.argv[2], sys.argv[3]


def cli(args, check=True):
    return subprocess.run([BIN] + args, capture_output=True, text=True,
                          check=check).stdout


def curl(port, path):
    return subprocess.run(["curl", "-sf", f"http://127.0.0.1:{port}{path}"],
                          capture_output=True, text=True)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    db = os.path.join(TMP, "http.cgdb")
    if os.path.exists(db):
        os.remove(db)
    cli(["ingest", "--db", db, "--compile-commands",
         os.path.join(SAMPLE, "build", "compile_commands.json"),
         "--root", SAMPLE])

    stats = cli(["stats", "--db", db])
    nodes = int(re.search(r"^nodes: (\d+)", stats, re.M).group(1))
    edges = int(re.search(r"^edges: (\d+)", stats, re.M).group(1))

    port = free_port()
    server = subprocess.Popen([BIN, "serve", "--db", db, "--port", str(port)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        up = False
        for _ in range(100):
            if server.poll() is not None:
                break
            if curl(port, "/api/graph").returncode == 0:
                up = True
                break
            time.sleep(0.1)
        if not up:
            print("server did not come up", file=sys.stderr)
            return 1

        r = curl(port, "/")
        assert r.returncode == 0, "GET / failed"
        assert "CompileGraphDB" in r.stdout, "index page missing title"
        assert "cytoscape" in r.stdout, "index page missing cytoscape"
        # interactive features wired into the page
        assert "/api/neighborhood" in r.stdout, "page lacks click-to-expand"
        assert "/api/cycles" in r.stdout, "page lacks cycles panel"
        assert "kind=object" in r.stdout, "page lacks filtered initial view"

        r = curl(port, "/api/graph")
        assert r.returncode == 0, "GET /api/graph failed"
        data = json.loads(r.stdout)
        assert len(data["nodes"]) == nodes, \
            f"nodes: api={len(data['nodes'])} stats={nodes}"
        assert len(data["edges"]) == edges, \
            f"edges: api={len(data['edges'])} stats={edges}"
        for n in data["nodes"]:
            assert set(n) == {"id", "label", "kind"}, f"bad node {n}"
            assert n["kind"] in ("source", "header", "object", "flag")
        for e in data["edges"]:
            assert set(e) == {"from", "to", "kind"}, f"bad edge {e}"
            assert e["kind"] in ("includes", "compiles_to", "uses")
            assert 0 <= e["from"] < nodes and 0 <= e["to"] < nodes

        # ?kind=object: object nodes + their 1-hop neighbors only
        r = curl(port, "/api/graph?kind=object")
        assert r.returncode == 0, "GET /api/graph?kind=object failed"
        sub = json.loads(r.stdout)
        by_id = {n["id"]: n for n in sub["nodes"]}
        n_objects = sum(1 for n in sub["nodes"] if n["kind"] == "object")
        assert n_objects == 5, f"expected 5 object nodes, got {n_objects}"
        assert not any(n["kind"] == "header" for n in sub["nodes"]), \
            "kind=object view should contain no headers"
        for e in sub["edges"]:
            assert by_id[e["from"]]["kind"] == "object" or \
                   by_id[e["to"]]["kind"] == "object", "edge outside filter"
        assert len(sub["nodes"]) < nodes, "filtered view not smaller"

        # /api/neighborhood of log.h: 1-hop, both directions
        log_h = os.path.join(SAMPLE, "log.h")
        r = curl(port, "/api/neighborhood?path=" + urllib.parse.quote(log_h))
        assert r.returncode == 0, "GET /api/neighborhood failed"
        nb = json.loads(r.stdout)
        labels = {n["label"] for n in nb["nodes"]}
        assert log_h in labels, "center missing from its neighborhood"
        assert os.path.join(SAMPLE, "config.h") in labels, \
            "direct includer missing from neighborhood"
        center_id = next(n["id"] for n in nb["nodes"] if n["label"] == log_h)
        for e in nb["edges"]:
            assert e["from"] == center_id or e["to"] == center_id, \
                "neighborhood contains an edge not incident to the center"
        r = curl(port, "/api/neighborhood?path=/no/such/file.h")
        assert r.returncode != 0, "expected 404 for unknown neighborhood path"

        # /api/cycles: the sample's cycle_a.h <-> cycle_b.h pair
        r = curl(port, "/api/cycles")
        assert r.returncode == 0, "GET /api/cycles failed"
        cyc = json.loads(r.stdout)
        assert list(cyc) == ["cycles"], "bad cycles JSON shape"
        assert len(cyc["cycles"]) == 1, f"expected 1 cycle group: {cyc}"
        group = cyc["cycles"][0]
        assert os.path.join(SAMPLE, "cycle_a.h") in group
        assert os.path.join(SAMPLE, "cycle_b.h") in group

        r = curl(port, "/no/such/route")
        assert r.returncode != 0, "expected curl -f to fail on 404"
    finally:
        server.terminate()
        server.wait(timeout=10)

    print(f"test_http: ok ({nodes} nodes, {edges} edges over HTTP)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
