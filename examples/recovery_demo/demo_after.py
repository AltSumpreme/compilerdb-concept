#!/usr/bin/env python3
"""AFTER: parse the tool-recovered log, render the final clean graph."""

import struct
import zlib

MAGIC = 0x43474442
NK = {1: "source", 2: "header", 3: "object", 4: "flag"}
EK = {1: "includes", 2: "compiles_to", 3: "uses"}
COLOR = {1: "#4C8BF5", 2: "#F5A623", 3: "#7ED321", 4: "#BD10E0"}

data = open("/tmp/demo/graph.cgdb", "rb").read()
nodes, edges = {}, []
off = 0
while off + 13 <= len(data):
    magic, crc = struct.unpack_from("<II", data, off)
    rtype = data[off + 8]
    plen = struct.unpack_from("<I", data, off + 9)[0]
    if magic != MAGIC or off + 13 + plen > len(data):
        break
    payload = data[off + 13: off + 13 + plen]
    assert zlib.crc32(data[off + 8: off + 13 + plen]) == crc  # all must pass now
    kind = payload[0]
    if rtype == 1:
        nlen = struct.unpack_from("<H", payload, 1)[0]
        nodes[payload[3:3 + nlen].decode()] = kind
    else:
        flen = struct.unpack_from("<H", payload, 1)[0]
        frm = payload[3:3 + flen].decode()
        tlen = struct.unpack_from("<H", payload, 3 + flen)[0]
        edges.append((frm, payload[5 + flen: 5 + flen + tlen].decode(), kind))
    off += 13 + plen

inc = [(f, t) for f, t, k in edges if k == 1]
s = set(inc)
cyc = {(f, t) for f, t in inc if (t, f) in s}

name_of = lambda p: p.rsplit("/", 1)[-1]
L = ["digraph demo {", "  rankdir=LR;", "  node [shape=box, style=filled];",
     '  label="AFTER: recovered graph — torn tail truncated, cycle detected & reported";',
     "  labelloc=t;", "  fontsize=16;"]
idx = {}
def nid(n):
    if n not in idx:
        idx[n] = f"n{len(idx)}"
    return idx[n]
for n, k in sorted(nodes.items()):
    L.append(f'  {nid(n)} [label="{name_of(n)}", fillcolor="{COLOR[k]}"];')
for f, t, k in edges:
    if (f, t) in cyc:
        L.append(f'  {nid(f)} -> {nid(t)} [label="{EK[k]} (circular!)",'
                 f' color="red", penwidth=2.5, fontcolor="red"];')
    else:
        L.append(f'  {nid(f)} -> {nid(t)} [label="{EK[k]}"];')
L.append("}")
open("/tmp/demo/after.dot", "w").write("\n".join(L) + "\n")
print(f"after.dot: {len(nodes)} nodes, {len(edges)} edges, {len(cyc)} cycle edges flagged")
