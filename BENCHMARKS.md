# Benchmarks — CompileGraphDB vs recursive grep

Reproduce with:

```sh
cmake -S . -B build && cmake --build build
tools/run_big_benchmark.sh        # or: tools/run_big_benchmark.sh <binary> <workdir>
```

## Methodology

- Synthetic projects from `tools/gen_project.py --seed 11`
  (deterministic; random include DAG with a bounded window plus
  deliberate header cycles; the generator computes each source's
  transitive header closure itself and emits `.d` files +
  `compile_commands.json` — no compiler involved).
- `bench` samples up to 8 headers present in the db (evenly spaced by
  name). Per header, `downstream` and `upstream` are timed through the
  in-memory query API; the baseline answers "what breaks if this header
  changes" by recursive `grep -rl -E` over the source tree until
  fixpoint (one grep per BFS level, frontier basenames batched by
  alternation — identical result set to one grep per basename, fewer
  process spawns; this only makes the baseline faster, so the
  comparison stays conservative).
- The OS file cache is warmed with one full-tree grep before any
  timing; each query API measurement does one untimed warm-up run.
- 1200-file project: 20 runs averaged (the Review II methodology).
  5000-file project: 5 runs averaged (caps total grep time; measured
  wall time for the whole run was ~7 s, far under the ~4 min budget).
- "Graph build/load" is the mmap replay of the persisted log
  (cold open of the 13543-edge / 100557-edge databases); grep has no
  equivalent persistent structure (`n/a`).

## Machine

- Intel Core i5-1035G1 (1.00 GHz base), 7 GiB RAM
- Linux 6.18.33-1-lts x86-64, g++ 16.1.1, GNU grep 3.12
- Measured 2026-09-29

## ~1200-file project (600 modules: 600 headers + 600 sources)

`stats`: 1803 nodes, 13543 edges (11143 includes, 600 compiles_to,
1800 uses). `cycles`: 12 circular include groups (30 files).

| Measurement | CompileGraphDB | grep based search |
| --- | --- | --- |
| Graph build/load (mmap replay) | 18.413 ms | n/a |
| Avg downstream(file) query | 0.017 ms | 100.116 ms |
| Avg upstream(target) query | 0.005 ms | n/a |

## ~5000-file project (2500 modules: 2500 headers + 2500 sources)

`stats`: 7503 nodes, 100557 edges (90557 includes, 2500 compiles_to,
7500 uses). `cycles`: 47 circular include groups (116 files).

| Measurement | CompileGraphDB | grep based search |
| --- | --- | --- |
| Graph build/load (mmap replay) | 142.694 ms | n/a |
| Avg downstream(file) query | 0.009 ms | 168.710 ms |
| Avg upstream(target) query | 0.006 ms | n/a |

## Reading the numbers

The graph database answers the average "what breaks" question in
~0.01–0.02 ms where recursive grep needs ~100–170 ms — roughly four
orders of magnitude faster (≈5900× at 1200 files, ≈18700× at 5000
files), and the advantage grows with project size because the BFS walks
only the affected subgraph while grep re-scans the whole tree at every
level. (The Review II report quoted ~0.03 ms vs ~850 ms at 1200 files;
absolute numbers depend on machine and project shape — what matters is
the orders-of-magnitude gap, which reproduces clearly.)
