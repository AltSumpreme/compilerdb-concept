#!/usr/bin/env python3
"""Before/after demo: CRC failure + cyclic includes.

1. Appends crafted records to a copy of the sample log:
   - a VALID node record (survives: good tail record)
   - an edge record with a deliberately BAD CRC (the 'torn/corrupt write')
   - a VALID node record placed AFTER the corruption (discarded: replay is
     positional, it stops at the first bad record)
2. Parses the whole file record by record, checking CRCs, prints the audit.
3. Emits before.dot (everything, with doomed content marked) and lets the
   real tool recover; after.dot is generated from the truncated file.
"""

import struct
import zlib

MAGIC = 0x43474442
NODE, EDGE = 1, 2
NK = {1: "source", 2: "header", 3: "object", 4: "flag"}
EK = {1: "includes", 2: "compiles_to", 3: "uses"}

DB = "/tmp/demo/graph.cgdb"


def node_payload(kind, name):
    b = name.encode()
    return bytes([kind]) + struct.pack("<H", len(b)) + b


def edge_payload(kind, frm, to):
    f, t = frm.encode(), to.encode()
    return bytes([kind]) + struct.pack("<H", len(f)) + f + struct.pack("<H", len(t)) + t


def record(rtype, payload, bad_crc=False):
    body = bytes([rtype]) + struct.pack("<I", len(payload)) + payload
    crc = zlib.crc32(body) ^ (0xDEADBEEF if bad_crc else 0)
    return struct.pack("<II", MAGIC, crc) + body


# ------------------------------------------------------------- 1. corrupt it
raw = open(DB, "rb").read()
main_cpp = "/home/revlin/dev/nosql/compilegraphdb-full/examples/sample_project/main.cpp"
phantom = "/home/revlin/dev/nosql/compilegraphdb-full/examples/sample_project/phantom_new.cpp"
lost = "/home/revlin/dev/nosql/compilegraphdb-full/examples/sample_project/lost_header.h"

with open(DB, "ab") as f:
    f.write(record(NODE, node_payload(1, phantom)))                       # valid
    f.write(record(EDGE, edge_payload(1, main_cpp, phantom), bad_crc=True))  # CORRUPT
    f.write(record(NODE, node_payload(2, lost)))                          # valid bytes, doomed position
print(f"corrupted log size: {len(raw)} -> {len(open(DB,'rb').read())} bytes\n")

# ------------------------------------------------------------ 2. audit pass
data = open(DB, "rb").read()
off, good = 0, 0
valid_nodes, valid_edges = {}, []
doomed_nodes, doomed_edges, doomed_reason = {}, [], {}
failed_at = None
i = 0
print("record-by-record CRC audit:")
while off + 13 <= len(data):
    magic, crc = struct.unpack_from("<II", data, off)
    rtype = data[off + 8]
    plen = struct.unpack_from("<I", data, off + 9)[0]
    i += 1
    if magic != MAGIC or off + 13 + plen > len(data):
        break
    payload = data[off + 13: off + 13 + plen]
    ok = zlib.crc32(data[off + 8: off + 13 + plen]) == crc
    kind = payload[0]
    if rtype == NODE:
        nlen = struct.unpack_from("<H", payload, 1)[0]
        name = payload[3:3 + nlen].decode()
        desc = f"node {NK.get(kind, '?')} {name.rsplit('/', 1)[-1]}"
    else:
        flen = struct.unpack_from("<H", payload, 1)[0]
        frm = payload[3:3 + flen].decode()
        tlen = struct.unpack_from("<H", payload, 3 + flen)[0]
        to = payload[5 + flen: 5 + flen + tlen].decode()
        desc = f"edge {EK.get(kind, '?')} {frm.rsplit('/',1)[-1]} -> {to.rsplit('/',1)[-1]}"
    status = "OK" if ok else "CRC FAIL"
    if failed_at is None and ok:
        if rtype == NODE:
            valid_nodes[name] = kind
        else:
            valid_edges.append((frm, to, kind))
        good = off + 13 + plen
    else:
        if failed_at is None:
            failed_at = i
        reason = "CRC FAIL (torn write)" if not ok else "after first bad record"
        if rtype == NODE:
            doomed_nodes[name] = kind
        else:
            doomed_edges.append((frm, to, kind))
        doomed_reason[desc.split(" ", 1)[1]] = reason
        if not ok:
            status = "CRC FAIL  <-- replay stops here, file truncated to offset %d" % good
    print(f"  rec {i:>3} @off {off:>5}: {status:<9} {desc}")
    off += 13 + plen
print(f"\nlast good offset: {good} of {len(data)} bytes "
      f"({len(data) - good} bytes will be truncated away)\n")

# ------------------------------------------------------------- 3. renderings
COLOR = {1: "#4C8BF5", 2: "#F5A623", 3: "#7ED321", 4: "#BD10E0"}
name_of = lambda p: p.rsplit("/", 1)[-1]

def cycle_members(edges):
    # tiny SCC-free approach: known pair from mutual includes
    inc = [(f, t) for f, t, k in edges if k == 1]
    s = set(inc)
    return {(f, t) for f, t in inc if (t, f) in s}

all_edges = valid_edges + doomed_edges
cyc = cycle_members(all_edges)

def write_dot(path, nodes_d, edges_d, extra_nodes=None, extra_edges=None,
              title="", cyc_edges=None):
    L = ['digraph demo {', '  rankdir=LR;', '  node [shape=box, style=filled];',
         f'  label="{title}";', '  labelloc=t;', '  fontsize=16;']
    idx = {}
    def nid(n):
        if n not in idx:
            idx[n] = f"n{len(idx)}"
        return idx[n]
    for n, k in sorted(nodes_d.items()):
        L.append(f'  {nid(n)} [label="{name_of(n)}", fillcolor="{COLOR[k]}"];')
    for n, k in (extra_nodes or {}).items():
        L.append(f'  {nid(n)} [label="{name_of(n)}\\n(discarded)", fillcolor="#FF6B6B",'
                 f' style="filled,dashed"];')
    for f, t, k in edges_d:
        if cyc_edges and (f, t) in cyc_edges:
            L.append(f'  {nid(f)} -> {nid(t)} [label="{EK[k]} (circular!)",'
                     f' color="red", penwidth=2.5, fontcolor="red"];')
        else:
            L.append(f'  {nid(f)} -> {nid(t)} [label="{EK[k]}"];')
    for f, t, k in (extra_edges or []):
        L.append(f'  {nid(f)} -> {nid(t)} [label="{EK[k]} — CRC FAIL, discarded",'
                 f' color="red", style=dashed, fontcolor="red"];')
    L.append("}")
    open(path, "w").write("\n".join(L) + "\n")

write_dot("/tmp/demo/before.dot", valid_nodes, valid_edges,
          extra_nodes=doomed_nodes, extra_edges=doomed_edges,
          title="BEFORE: raw log — CRC failure + circular include present",
          cyc_edges=cyc)
print("wrote before.dot (valid:", len(valid_nodes), "nodes, doomed:",
      len(doomed_nodes), "nodes +", len(doomed_edges), "edges, cycle edges:", len(cyc), ")")
