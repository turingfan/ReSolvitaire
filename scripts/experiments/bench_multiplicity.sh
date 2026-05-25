#!/usr/bin/env bash
#
# bench_multiplicity.sh — Benchmark the multiplicity cache against flat and LRU.
#
# ═══════════════════════════════════════════════════════════════════════════════
# WHAT THIS DOES
# ═══════════════════════════════════════════════════════════════════════════════
#
# Runs four comparisons to measure the multiplicity cache's performance:
#
#   A. Incremental vs from-scratch multiplicity (same search tree, different
#      descriptor update strategy). Validates that incremental updates are faster.
#      Requires a from-scratch binary — see PREREQUISITES below.
#
#   B. Flat vs multiplicity (no symmetry). On games where both work, flat has
#      smaller clusters (32B vs 64B entries) so should be faster. This quantifies
#      the overhead of the multiplicity payload.
#
#   C. LRU vs multiplicity (no symmetry). Multiplicity should beat LRU easily
#      thanks to flat-cache O(1) lookup vs LRU's pile-ordering + Boost overhead.
#
#   D. LRU vs multiplicity WITH SUIT-SYMMETRY. This is the critical test — the
#      entire point of the multiplicity encoding is to enable suit-symmetry
#      canonicalisation in the flat cache. If mult+symmetry beats LRU+symmetry,
#      the project delivers its core value.
#
# ═══════════════════════════════════════════════════════════════════════════════
# PREREQUISITES
# ═══════════════════════════════════════════════════════════════════════════════
#
# 1. Build release binaries:
#        ./build.sh --release --unit-tests
#
# 2. (Comparison A only) Build a from-scratch multiplicity binary. This is a
#    copy of solvitaire that always calls recompute_all() instead of incremental
#    updates. To create it:
#
#        # In game_state.cpp, replace the two blocks that call
#        # incremental_update_none() / incremental_update() with:
#        #     desc_engine.recompute_all(make_desc_ctx());
#        # Then:
#        cmake --build cmake-build-release --target solvitaire
#        cp cmake-build-release/bin/solvitaire cmake-build-release/bin/solvitaire-mult-scratch
#        # Revert game_state.cpp and rebuild the normal binary:
#        git checkout src/main/game/search-state/game_state.cpp
#        cmake --build cmake-build-release --target solvitaire
#
#    If the from-scratch binary is absent, Comparison A is skipped.
#
# ═══════════════════════════════════════════════════════════════════════════════
# USAGE
# ═══════════════════════════════════════════════════════════════════════════════
#
# Standalone (results in a local directory):
#
#     ./scripts/experiments/bench_multiplicity.sh [--quick] [--phase ABCD] [RESULTS_DIR]
#
# Via bench (recommended for archiving results in DataLad):
#
#     bench --detached --bundle-format tar \
#         -m "Multiplicity benchmark, full run" mult-bench-v1 \
#         -- scripts/experiments/bench_multiplicity.sh
#
# Options:
#   --quick       Use 50 seeds instead of 500, shorter timeouts (for validation)
#   --phase XY    Run only the listed comparisons, e.g. --phase AD
#   RESULTS_DIR   Override output directory (default: $BENCH_RUN_DIR/data or
#                 benchmarks/mult_<timestamp>/)
#
# Environment overrides:
#   SEEDS_SHORT=1-50      Seed range for Comparisons A (default: 1-150 / 1-50 quick)
#   SEEDS_LONG=1-500      Seed range for Comparisons B/C/D (default: 1-500 / 1-50 quick)
#   TIMEOUT_SHORT=120000  Timeout for A/B/C in ms (default: 120000 / 30000 quick)
#   TIMEOUT_LONG=300000   Timeout for D in ms (default: 300000 / 60000 quick)
#   BIN_DIR=path          Binary directory (default: cmake-build-release/bin)
#   SOLVER=path           Override default solvitaire binary
#   SOLVER_SCRATCH=path   From-scratch binary for Comparison A
#
# ═══════════════════════════════════════════════════════════════════════════════
# OUTPUT
# ═══════════════════════════════════════════════════════════════════════════════
#
# Per-comparison CSV files in RESULTS_DIR:
#   A_incr_<game>.csv, A_scratch_<game>.csv
#   B_flat_<game>.csv, B_mult_<game>.csv
#   C_lru_<game>.csv, C_mult_<game>.csv
#   D_lru_<game>.csv, D_mult_<game>.csv
#
# Plus combined_<comparison>.csv merging all games for that comparison.
#
# Analyse with:
#   Rscript analysis/benchmark.R --baseline D_lru_klondike.csv \
#       --current D_mult_klondike.csv --output report.html
#
# ═══════════════════════════════════════════════════════════════════════════════
# FULL PLAN
# ═══════════════════════════════════════════════════════════════════════════════
#
# See docs/multiplicity-encoding/stage5-4-benchmark-plan.md for rationale,
# success criteria, and analysis instructions.
#
# ═══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

QUICK=false
PHASES="ABCD"
RESULTS_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)  QUICK=true; shift ;;
        --phase)  PHASES="$2"; shift 2 ;;
        --help|-h)
            awk '/^set -euo pipefail/{exit} NR>1{sub(/^# ?/,""); print}' "$0"
            exit 0
            ;;
        *)        RESULTS_DIR="$1"; shift ;;
    esac
done

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

BIN_DIR="${BIN_DIR:-cmake-build-release/bin}"
SOLVER="${SOLVER:-$BIN_DIR/solvitaire}"
SOLVER_LRU="${SOLVER_LRU:-$BIN_DIR/solvitaire-lru}"
SOLVER_FLAT="${SOLVER_FLAT:-$BIN_DIR/solvitaire-flat}"
SOLVER_SCRATCH="${SOLVER_SCRATCH:-$BIN_DIR/solvitaire-mult-scratch}"

if $QUICK; then
    SEEDS_SHORT="${SEEDS_SHORT:-1-50}"
    SEEDS_LONG="${SEEDS_LONG:-1-50}"
    TIMEOUT_SHORT="${TIMEOUT_SHORT:-30000}"
    TIMEOUT_LONG="${TIMEOUT_LONG:-60000}"
else
    SEEDS_SHORT="${SEEDS_SHORT:-1-150}"
    SEEDS_LONG="${SEEDS_LONG:-1-500}"
    TIMEOUT_SHORT="${TIMEOUT_SHORT:-120000}"
    TIMEOUT_LONG="${TIMEOUT_LONG:-300000}"
fi

if [[ -z "$RESULTS_DIR" ]]; then
    if [[ -n "${BENCH_RUN_DIR:-}" ]]; then
        RESULTS_DIR="$BENCH_RUN_DIR/data"
    else
        RESULTS_DIR="benchmarks/mult_$(date +%Y%m%d_%H%M%S)"
    fi
fi

mkdir -p "$RESULTS_DIR"

RUN_BENCH="$REPO_ROOT/scripts/run_benchmark.py"

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------

MISSING=""
[[ ! -x "$SOLVER" ]]     && MISSING="$MISSING  $SOLVER (default solvitaire)\n"
[[ ! -x "$SOLVER_LRU" ]] && MISSING="$MISSING  $SOLVER_LRU (LRU variant)\n"
[[ ! -x "$SOLVER_FLAT" ]] && MISSING="$MISSING  $SOLVER_FLAT (flat variant)\n"

if [[ -n "$MISSING" ]]; then
    echo "FATAL: missing binaries:" >&2
    echo -e "$MISSING" >&2
    echo "Run: ./build.sh --release --unit-tests" >&2
    exit 1
fi

HAS_SCRATCH=true
if [[ ! -x "$SOLVER_SCRATCH" ]]; then
    HAS_SCRATCH=false
    if [[ "$PHASES" == *A* ]]; then
        echo "WARNING: from-scratch binary not found at $SOLVER_SCRATCH"
        echo "         Comparison A will be skipped."
        echo "         See script header for build instructions."
        echo ""
    fi
fi

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

echo "═══════════════════════════════════════════════════════════════"
echo " Multiplicity Cache Benchmark"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "  Mode:          $(if $QUICK; then echo QUICK; else echo FULL; fi)"
echo "  Phases:        $PHASES"
echo "  Seeds (short): $SEEDS_SHORT"
echo "  Seeds (long):  $SEEDS_LONG"
echo "  Timeout (A-C): ${TIMEOUT_SHORT}ms"
echo "  Timeout (D):   ${TIMEOUT_LONG}ms"
echo "  Output:        $RESULTS_DIR"
echo "  Scratch binary:$(if $HAS_SCRATCH; then echo " $SOLVER_SCRATCH"; else echo " (not found, A skipped)"; fi)"
echo ""

# ---------------------------------------------------------------------------
# Helper: run a single benchmark
# ---------------------------------------------------------------------------

run_bench() {
    local label="$1"
    local solver="$2"
    local game="$3"
    local seeds="$4"
    local timeout="$5"
    local outfile="$6"
    shift 6
    # remaining args passed to solver via --

    local cmd=(
        python3 "$RUN_BENCH"
        --solver "$solver"
        --type "$game"
        --seeds "$seeds"
        --timeout "$timeout"
        --output "$outfile"
        --label "$label"
        --no-summary
    )

    # Add streamliner if specified
    local streamliner=""
    local solver_args=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --streamliner) streamliner="$2"; shift 2 ;;
            *)             solver_args+=("$1"); shift ;;
        esac
    done

    if [[ -n "$streamliner" ]]; then
        cmd+=(--streamliner "$streamliner")
    fi

    if [[ ${#solver_args[@]} -gt 0 ]]; then
        cmd+=(-- "${solver_args[@]}")
    fi

    echo "  [$(date +%H:%M:%S)] $label / $game / seeds $seeds"
    "${cmd[@]}" || {
        echo "  WARNING: $label/$game failed (exit $?)" >&2
        return 0  # don't abort the whole script
    }
}

# ---------------------------------------------------------------------------
# Helper: merge per-game CSVs into a combined file
# ---------------------------------------------------------------------------

merge_csvs() {
    local prefix="$1"
    local outfile="$RESULTS_DIR/combined_${prefix}.csv"
    local first=1

    for f in "$RESULTS_DIR"/${prefix}_*.csv; do
        [[ -f "$f" ]] || continue
        [[ "$(basename "$f")" == combined_* ]] && continue
        if [[ "$first" == 1 ]]; then
            head -1 "$f" > "$outfile"
            first=0
        fi
        tail -n +2 "$f" >> "$outfile"
    done

    if [[ "$first" == 0 ]]; then
        local rows
        rows=$(wc -l < "$outfile")
        echo "  -> combined_${prefix}.csv ($rows rows)"
    fi
}

# ---------------------------------------------------------------------------
# Comparison A: Incremental vs from-scratch multiplicity
# ---------------------------------------------------------------------------
# Same search tree (same hash, same cache decisions). The only difference is
# whether descriptors are updated incrementally (O(k) per move) or recomputed
# from scratch (O(52) per move). states_searched MUST be identical.
#
# Games: symmetric games where the cascade matters most.
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *A* ]] && $HAS_SCRATCH; then
    echo ""
    echo "═══ Comparison A: Incremental vs From-Scratch Multiplicity ════"
    echo ""

    for GAME in klondike free-cell black-hole; do
        STREAMLINER="suit-symmetry"
        [[ "$GAME" == "black-hole" ]] && STREAMLINER="auto-foundations"

        run_bench "A_incr"    "$SOLVER"         "$GAME" "$SEEDS_SHORT" "$TIMEOUT_SHORT" \
            "$RESULTS_DIR/A_incr_${GAME}.csv" \
            --streamliner "$STREAMLINER" --cache-type multiplicity &

        run_bench "A_scratch" "$SOLVER_SCRATCH"  "$GAME" "$SEEDS_SHORT" "$TIMEOUT_SHORT" \
            "$RESULTS_DIR/A_scratch_${GAME}.csv" \
            --streamliner "$STREAMLINER" --cache-type multiplicity &
    done
    wait

    merge_csvs "A_incr"
    merge_csvs "A_scratch"

    echo ""
    echo "  VALIDATION: states_searched must match between incr and scratch."
    echo "  Check with: diff <(cut -d, -f1,6 combined_A_incr.csv) <(cut -d, -f1,6 combined_A_scratch.csv)"
fi

# ---------------------------------------------------------------------------
# Comparison B: Flat vs Multiplicity (no symmetry)
# ---------------------------------------------------------------------------
# Both use flat-cache architecture. Flat has 32B entries (compact_state),
# multiplicity has 64B entries (multiplicity_descriptor_store). Flat should be
# slightly faster due to smaller cache footprint. This quantifies the overhead.
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *B* ]]; then
    echo ""
    echo "═══ Comparison B: Flat vs Multiplicity (No Symmetry) ══════════"
    echo ""

    GAMES_B=(klondike-deal-1 free-cell bakers-game canfield somerset black-hole)
    STREAMLINERS_B=(none none none none none auto-foundations)

    for i in "${!GAMES_B[@]}"; do
        GAME="${GAMES_B[$i]}"
        STR="${STREAMLINERS_B[$i]}"

        run_bench "B_flat" "$SOLVER_FLAT" "$GAME" "$SEEDS_LONG" "$TIMEOUT_SHORT" \
            "$RESULTS_DIR/B_flat_${GAME}.csv" \
            --streamliner "$STR" &

        run_bench "B_mult" "$SOLVER" "$GAME" "$SEEDS_LONG" "$TIMEOUT_SHORT" \
            "$RESULTS_DIR/B_mult_${GAME}.csv" \
            --streamliner "$STR" --cache-type multiplicity &
    done
    wait

    merge_csvs "B_flat"
    merge_csvs "B_mult"
fi

# ---------------------------------------------------------------------------
# Comparison C: LRU vs Multiplicity (no symmetry)
# ---------------------------------------------------------------------------
# LRU has pile-ordering overhead + Boost MultiIndex vs multiplicity's flat
# O(1) lookup. Multiplicity should win comfortably.
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *C* ]]; then
    echo ""
    echo "═══ Comparison C: LRU vs Multiplicity (No Symmetry) ═══════════"
    echo ""

    GAMES_C=(klondike-deal-1 free-cell bakers-game canfield somerset black-hole)
    STREAMLINERS_C=(none none none none none auto-foundations)

    for i in "${!GAMES_C[@]}"; do
        GAME="${GAMES_C[$i]}"
        STR="${STREAMLINERS_C[$i]}"

        run_bench "C_lru" "$SOLVER_LRU" "$GAME" "$SEEDS_LONG" "$TIMEOUT_SHORT" \
            "$RESULTS_DIR/C_lru_${GAME}.csv" \
            --streamliner "$STR" &

        run_bench "C_mult" "$SOLVER" "$GAME" "$SEEDS_LONG" "$TIMEOUT_SHORT" \
            "$RESULTS_DIR/C_mult_${GAME}.csv" \
            --streamliner "$STR" --cache-type multiplicity &
    done
    wait

    merge_csvs "C_lru"
    merge_csvs "C_mult"
fi

# ---------------------------------------------------------------------------
# Comparison D: LRU vs Multiplicity WITH SUIT-SYMMETRY (the critical test)
# ---------------------------------------------------------------------------
# The whole point of the multiplicity encoding: enable suit-symmetry
# canonicalisation in the flat cache. LRU currently handles this via
# pile-order canonicalisation. If multiplicity + symmetry is faster than
# LRU + symmetry, the project delivers its core value.
#
# COLOUR mode (26 classes of 2): klondike, klondike-deal-1
# SUIT_IRRELEVANT mode (13 classes of 4): free-cell, black-hole
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *D* ]]; then
    echo ""
    echo "═══ Comparison D: LRU vs Multiplicity + Suit-Symmetry ═════════"
    echo "═══ (THE CRITICAL TEST)                                ═════════"
    echo ""

    GAMES_D=(klondike klondike-deal-1 free-cell black-hole)
    # black-hole has inherent suit-irrelevance; the others need the streamliner
    STREAMLINERS_D=(suit-symmetry suit-symmetry suit-symmetry auto-foundations)

    for i in "${!GAMES_D[@]}"; do
        GAME="${GAMES_D[$i]}"
        STR="${STREAMLINERS_D[$i]}"

        run_bench "D_lru" "$SOLVER_LRU" "$GAME" "$SEEDS_LONG" "$TIMEOUT_LONG" \
            "$RESULTS_DIR/D_lru_${GAME}.csv" \
            --streamliner "$STR" &

        run_bench "D_mult" "$SOLVER" "$GAME" "$SEEDS_LONG" "$TIMEOUT_LONG" \
            "$RESULTS_DIR/D_mult_${GAME}.csv" \
            --streamliner "$STR" --cache-type multiplicity &
    done
    wait

    merge_csvs "D_lru"
    merge_csvs "D_mult"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " Done. Results in $RESULTS_DIR/"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Files:"
ls -1 "$RESULTS_DIR"/combined_*.csv 2>/dev/null || echo "  (no combined files)"
echo ""
echo "Next steps:"
echo "  # Quick summary of one file:"
echo "  Rscript analysis/summary.R $RESULTS_DIR/combined_D_mult.csv"
echo ""
echo "  # Full comparison report (e.g. for Comparison D on klondike):"
echo "  Rscript analysis/benchmark.R \\"
echo "      --baseline $RESULTS_DIR/D_lru_klondike.csv \\"
echo "      --current  $RESULTS_DIR/D_mult_klondike.csv \\"
echo "      --output   $RESULTS_DIR/report_D_klondike.html"
echo ""
echo "  # Validate Comparison A (states_searched must match):"
echo "  diff <(cut -d, -f1,6 $RESULTS_DIR/combined_A_incr.csv) \\"
echo "       <(cut -d, -f1,6 $RESULTS_DIR/combined_A_scratch.csv)"
