#!/usr/bin/env bash
# Benchmark flat-eligible unwinnable Level 5 instances across four solver variants:
#   solvitaire, solvitaire-flat, solvitaire-lru (--force-lru), legacy (--legacy)
#
# Usage:
#   ./scripts/experiments/bench_level5_unwinnable.sh [RESULTS_DIR]
#
# RESULTS_DIR defaults to benchmarks/level5_unwinnable_<timestamp>/
# Run from the repo root.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

ORACLE="tests/oracles/level5.json"
BIN_DIR="cmake-build-release/bin"
# Override LEGACY_BIN via environment to use a platform-appropriate binary, e.g.:
#   LEGACY_BIN=/path/to/solvitaire-linux-arm64 ./scripts/experiments/bench_level5_unwinnable.sh
LEGACY_BIN="${LEGACY_BIN:-$REPO_ROOT/../05-Executables/reference/solvitaire-reference-mac-arm64}"
RESULTS_DIR="${1:-benchmarks/level5_unwinnable_$(date +%Y%m%d_%H%M%S)}"

WARMUP=1
ITERATIONS=3
TIMEOUT=1800000   # 1800 s — Level 5 standard

mkdir -p "$RESULTS_DIR"
echo "Results: $RESULTS_DIR"

run_variant() {
    local label="$1"
    local solver="$2"
    local output="$RESULTS_DIR/${label}.csv"
    shift 2
    # remaining args are forwarded to oracle_to_benchmark_cmds.py
    # use -- to pass solver flags (e.g. -- --force-lru), or bare flags for oracle script flags (e.g. --legacy)

    echo "=== $label ==="
    python3 scripts/oracle_to_benchmark_cmds.py \
        --oracle "$ORACLE" \
        --solution-type unsolvable \
        --solver "$solver" \
        --warmup "$WARMUP" \
        --iterations "$ITERATIONS" \
        --timeout "$TIMEOUT" \
        --output "$output" \
        --label "$label" \
        --skip-ineligible \
        --no-summary \
        "$@" | bash
    echo "  -> $output"
}

run_variant "solvitaire"      "$BIN_DIR/solvitaire"
run_variant "solvitaire-flat" "$BIN_DIR/solvitaire-flat"
run_variant "solvitaire-lru"  "$BIN_DIR/solvitaire-lru"  -- --force-lru

run_variant "legacy" "$LEGACY_BIN" --legacy

echo "Done. Results in $RESULTS_DIR/"
