# External Sorting — ESO207 IIT Kanpur
**Author:** Tanay Pathode

---

## Overview

This project implements and compares two disk-aware external sorting algorithms:

| Algorithm | Source File | Binary |
|-----------|-------------|--------|
| **External Merge Sort** | `src/external_merge_sort.cpp` | `./external_mergesort` |
| **External Quick Sort** | `src/external_quick_sort.cpp` | `./external_quicksort` |

Both programs simulate disk I/O, tracking **seeks** and **transfers** per pass, matching the assignment requirements from ESO207.

---

## Project Structure

```
220830_mergesort_project/
├── src/
│   ├── external_merge_sort.cpp   ← Main deliverable (Merge Sort)
│   └── external_quick_sort.cpp   ← Provided Quicksort (reference)
├── docs/
│   ├── report.tex                ← LaTeX source
│   └── report.pdf                ← Compiled 12-page report
├── tests/
│   ├── test_10keys.txt           ← Small random test
│   ├── test_50keys.txt           ← Medium random test
│   ├── test_10000keys.txt        ← Large random test
│   ├── test_sorted_20.txt        ← Edge: already sorted
│   ├── test_reverse_100.txt      ← Edge: reverse sorted
│   └── test_same_keys_30.txt     ← Edge: all identical keys
├── scripts/
│   ├── gen_input.sh              ← Random input generator
│   └── benchmark.sh              ← Side-by-side benchmark
├── external_mergesort            ← Pre-built binary (after make)
├── external_quicksort            ← Pre-built binary (after make)
├── Makefile
└── README.md
```

---

## Build

```bash
# Requires: g++ with C++17 support
make

# Or manually:
g++ -std=c++17 -O2 -o external_mergesort src/external_merge_sort.cpp
g++ -std=c++17 -O2 -o external_quicksort src/external_quick_sort.cpp
```

---

## Usage

```
./external_mergesort <input-file> <n> <k> <b> <m>
./external_quicksort <input-file> <n> <k> <b> <m>
```

| Parameter | Meaning |
|-----------|---------|
| `input-file` | Path to file with one integer key per line |
| `n` | Total number of keys |
| `k` | Key size in bytes |
| `b` | Disk block size in bytes |
| `m` | Available memory in blocks (≥ 3 for merge sort, ≥ 4 for quicksort) |

**Example** (from assignment):
```bash
./external_mergesort tests/test_10000keys.txt 10000 4 1024 10
./external_quicksort tests/test_10000keys.txt 10000 4 1024 10
```

---

## Output Format

Both programs produce:

1. **Memory layout** — how memory blocks are allocated
2. **Pass-by-pass detailed output** — every batch read, flush, run boundary
3. **Pass-wise I/O summary table** — seeks and transfers per phase
4. **Overall statistics** — total seeks, transfers, files, blocks

Example summary output:
```
=== PASS-WISE I/O SUMMARY ===
Pass                                        Seeks       Transfers
----------------------------------------------------------------------
Pre-Pass  (Input File Load)                 1           40
Pass 0  (Phase 1: Run Generation)           45          80
Pass 1  (Phase 2: Merge pass 1)             27          27
----------------------------------------------------------------------
TOTAL                                       73          147

=== OVERALL STATISTICS ===
  Number of partition passes         : 2
  Initial runs produced (Phase 1)    : 3
  Fan-in used  (M-1)                 : 9
  Phase 2 merge passes               : 1
  Total algorithm passes (P1 + P2)   : 2
  Total disk seeks                   : 73
  Total disk transfers               : 147
```

---

## Test Suite

```bash
make test         # Run built-in tests
make compare      # Side-by-side benchmark at n=10000
```

Or manually:
```bash
# Generate input
./scripts/gen_input.sh 500 42 > my_input.txt

# Run benchmark
./scripts/benchmark.sh
```

---

## Algorithm Summary

### External Merge Sort (Phase 1 + Phase 2)

**Phase 1 — Replacement Selection:**
- Uses all M memory blocks as a min-heap
- Emits keys in order; "freezes" keys smaller than last emitted (deferred to next run)
- Average run length ≈ **2M blocks** on random data (vs. M blocks for simple fill-and-sort)

**Phase 2 — Multi-Way Merge:**
- Merges up to **(M−1)** runs simultaneously using a min-heap
- One block per input run + one output block
- Repeats until 1 run remains
- Pass count: ⌈log₍ₘ₋₁₎ R⌉ where R = number of initial runs

### External Quick Sort (provided reference)

- Median-of-3 pivot selection (3 seeks + 3 transfers per node)
- Streams through file in batches, partitions into lo/hi files
- Recurses until sub-file fits in memory
- Concatenates sorted sub-files at each recursion level

---

## Benchmark Results (n=10000, k=4, b=1024, m=10)

| Algorithm | Seeks | Transfers |
|-----------|-------|-----------|
| External Quicksort | 106 | 512 |
| **External Merge Sort** | **122** | **200** |

→ Merge Sort uses **61% fewer transfers** at large n, at the cost of ~15% more seeks.

---

## Report

The full 12-page academic report (`docs/report.pdf`) covers:
- Problem statement & I/O cost model
- Quicksort: algorithm, pseudocode, complexity
- Merge Sort: replacement selection, multi-way merge, pseudocode, complexity
- Implementation details with annotated code
- Experimental results with graphs
- Comprehensive comparison table
- Final recommendation

To recompile the report:
```bash
make docs
# or:
cd docs && pdflatex report.tex && pdflatex report.tex
```

---

## Resume Bullet Points

1. **Implementation:** *Engineered disk-aware external sorting simulator in C++ modeling precise seeks and transfers per block.*

2. **Comparative Analysis:** *Benchmarked external Quicksort vs. Merge Sort achieving 61% fewer transfers at scale.*

---

## Key range

Keys are integers in `[-100,000, +100,000]` as specified in the assignment.
