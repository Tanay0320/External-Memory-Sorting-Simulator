# ─────────────────────────────────────────────────────────────────────────────
# Makefile — External Sorting Project
# CS315 | IIT Kanpur | Author: Prashant Shekhar
# ─────────────────────────────────────────────────────────────────────────────

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic

SRC_DIR  := src
BIN_DIR  := .
TEST_DIR := tests

MS_SRC   := $(SRC_DIR)/external_merge_sort.cpp
QS_SRC   := $(SRC_DIR)/external_quick_sort.cpp
MS_BIN   := $(BIN_DIR)/external_mergesort
QS_BIN   := $(BIN_DIR)/external_quicksort

.PHONY: all clean test test_ms test_qs compare docs help

# ── Default target ────────────────────────────────────────────────────────────
all: $(MS_BIN) $(QS_BIN)
	@echo ""
	@echo "✓  Built successfully:"
	@echo "     $(MS_BIN)   — External Merge Sort"
	@echo "     $(QS_BIN)  — External Quick Sort"

# ── Compile merge sort ────────────────────────────────────────────────────────
$(MS_BIN): $(MS_SRC)
	@echo "Compiling External Merge Sort..."
	$(CXX) $(CXXFLAGS) -o $@ $<

# ── Compile quick sort ────────────────────────────────────────────────────────
$(QS_BIN): $(QS_SRC)
	@echo "Compiling External Quick Sort..."
	$(CXX) $(CXXFLAGS) -o $@ $<

# ── Run test suite ────────────────────────────────────────────────────────────
test: all test_ms test_qs
	@echo ""
	@echo "All tests passed."

test_ms: $(MS_BIN)
	@echo ""
	@echo "=== Merge Sort: Small test (10 keys, b=8, m=4) ==="
	@./scripts/gen_input.sh 10 42 > /tmp/ms_test.txt
	@$(MS_BIN) /tmp/ms_test.txt 10 4 8 4

test_qs: $(QS_BIN)
	@echo ""
	@echo "=== Quick Sort: Small test (10 keys, b=8, m=4) ==="
	@./scripts/gen_input.sh 10 42 > /tmp/qs_test.txt
	@$(QS_BIN) /tmp/qs_test.txt 10 4 8 4

# ── Side-by-side comparison (standard benchmark) ─────────────────────────────
compare: all
	@echo ""
	@echo "=== Comparative Benchmark (n=10000, k=4, b=1024, m=10) ==="
	@./scripts/gen_input.sh 10000 42 > /tmp/bench.txt
	@echo ""
	@echo "--- External Merge Sort ---"
	@$(MS_BIN) /tmp/bench.txt 10000 4 1024 10 2>&1 | grep -E "(TOTAL|Overall|Pass|seeks|transfers)" | head -20
	@echo ""
	@echo "--- External Quick Sort ---"
	@$(QS_BIN) /tmp/bench.txt 10000 4 1024 10 2>&1 | grep -E "(TOTAL|Overall|Pass|seeks|transfers)" | head -20

# ── Build PDF report ─────────────────────────────────────────────────────────
docs:
	@echo "Building LaTeX report..."
	@cd docs && pdflatex -interaction=nonstopmode report.tex > /dev/null 2>&1
	@cd docs && pdflatex -interaction=nonstopmode report.tex > /dev/null 2>&1
	@echo "✓  Report built: docs/report.pdf"

# ── Clean build artifacts ─────────────────────────────────────────────────────
clean:
	rm -f $(MS_BIN) $(QS_BIN)
	rm -f docs/*.aux docs/*.log docs/*.toc docs/*.out docs/*.pdf
	@echo "Cleaned."

# ── Help ──────────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "External Sorting Project — Available targets:"
	@echo "  make            Build both executables"
	@echo "  make test       Run test suite"
	@echo "  make compare    Side-by-side benchmark"
	@echo "  make docs       Compile LaTeX report to PDF"
	@echo "  make clean      Remove build artefacts"
	@echo ""
	@echo "Usage after build:"
	@echo "  ./external_mergesort  <input.txt> <n> <k> <b> <m>"
	@echo "  ./external_quicksort  <input.txt> <n> <k> <b> <m>"
