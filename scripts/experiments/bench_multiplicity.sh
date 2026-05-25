#!/usr/bin/env bash
#
# bench_multiplicity.sh — Benchmark the multiplicity cache against flat and LRU.
#
# ═══════════════════════════════════════════════════════════════════════════════
# WHAT THIS DOES
# ═══════════════════════════════════════════════════════════════════════════════
#
# Runs up to four comparisons to measure the multiplicity cache's performance:
#
#   A. Incremental vs from-scratch multiplicity (validates incremental speedup).
#      Requires a from-scratch binary — see PREREQUISITES below.
#
#   B. Flat vs multiplicity (no symmetry). Quantifies the overhead of the
#      multiplicity payload (64B vs 32B entries). Flat should win slightly.
#
#   C. LRU vs multiplicity (no symmetry). Multiplicity should beat LRU easily.
#
#   D. LRU vs multiplicity WITH SUIT-SYMMETRY. THE CRITICAL TEST — the entire
#      point of the multiplicity encoding project.
#
# ═══════════════════════════════════════════════════════════════════════════════
# USAGE
# ═══════════════════════════════════════════════════════════════════════════════
#
# With no arguments, prints this help and exits.
#
# To run something:
#
#   # Dry run — shows what would be executed, runs nothing:
#   ./scripts/experiments/bench_multiplicity.sh --dry-run --phase D --seeds 1-10
#
#   # Minimal smoke test (5 seeds, 1 game, phase D only, ~30 seconds):
#   ./scripts/experiments/bench_multiplicity.sh --phase D --seeds 1-5 --games klondike
#
#   # Quick validation (50 seeds, all phases, ~30 min on 1 core):
#   ./scripts/experiments/bench_multiplicity.sh --phase ABCD --seeds 1-50
#
#   # Full run (500 seeds, all phases, many hours — use on big machine):
#   ./scripts/experiments/bench_multiplicity.sh --phase ABCD --seeds 1-500 \
#       --timeout 300000
#
# Via bench (recommended for archiving results in DataLad):
#
#   bench --detached --bundle-format tar \
#       -m "Multiplicity benchmark" mult-bench-v1 \
#       -- scripts/experiments/bench_multiplicity.sh --phase D --seeds 1-500
#
# ═══════════════════════════════════════════════════════════════════════════════
# OPTIONS
# ═══════════════════════════════════════════════════════════════════════════════
#
#   --phase XY        Which comparisons to run (default: none — must specify)
#   --seeds N-M       Seed range (default: none — must specify)
#   --games g1,g2     Comma-separated game list (default: all games for phase)
#   --timeout MS      Timeout per instance in ms (default: 120000)
#   --outdir DIR      Output directory (default: $BENCH_RUN_DIR/data or
#                     benchmarks/mult_<timestamp>/)
#   --dry-run         Print commands without executing
#   --help, -h        Print this help
#
# ═══════════════════════════════════════════════════════════════════════════════
# PREREQUISITES
# ═══════════════════════════════════════════════════════════════════════════════
#
# 1. Build release binaries:
#        ./build.sh --release
#
# 2. (Comparison A only) Build a from-scratch multiplicity binary:
#
#        # In game_state.cpp, replace the two blocks that call
#        # incremental_update_none() / incremental_update() with:
#        #     desc_engine.recompute_all(make_desc_ctx());
#        cmake --build cmake-build-release --target solvitaire
#        cp cmake-build-release/bin/solvitaire cmake-build-release/bin/solvitaire-mult-scratch
#        git checkout src/main/game/search-state/game_state.cpp
#        cmake --build cmake-build-release --target solvitaire
#
#    If absent, Comparison A is skipped with a warning.
#
# ═══════════════════════════════════════════════════════════════════════════════
# OUTPUT
# ═══════════════════════════════════════════════════════════════════════════════
#
# Per-run CSV files: <phase>_<variant>_<game>.csv
# Combined per-phase: combined_<phase>_<variant>.csv
#
# Analyse with:
#   Rscript analysis/summary.R <outdir>/D_mult_klondike.csv
#   Rscript analysis/benchmark.R --baseline <outdir>/D_lru_klondike.csv \
#       --current <outdir>/D_mult_klondike.csv --output report.html
#
# Full plan: docs/multiplicity-encoding/stage5-4-benchmark-plan.md
#
# ═══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

PHASES=""
SEEDS=""
GAMES_OVERRIDE=""
TIMEOUT=""
RESULTS_DIR=""
DRY_RUN=false

show_help() {
    awk '/^set -euo pipefail/{exit} NR>1{sub(/^# ?/,""); print}' "$0"
}

if [[ $# -eq 0 ]]; then
    show_help
    exit 0
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --phase)    PHASES="$2"; shift 2 ;;
        --seeds)    SEEDS="$2"; shift 2 ;;
        --games)    GAMES_OVERRIDE="$2"; shift 2 ;;
        --timeout)  TIMEOUT="$2"; shift 2 ;;
        --outdir)   RESULTS_DIR="$2"; shift 2 ;;
        --dry-run)  DRY_RUN=true; shift ;;
        --help|-h)  show_help; exit 0 ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Try --help" >&2
            exit 1
            ;;
    esac
done

# Validate required arguments
if [[ -z "$PHASES" ]]; then
    echo "Error: --phase is required (e.g. --phase D or --phase ABCD)" >&2
    echo "Try --help" >&2
    exit 1
fi

if [[ -z "$SEEDS" ]]; then
    echo "Error: --seeds is required (e.g. --seeds 1-50)" >&2
    echo "Try --help" >&2
    exit 1
fi

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

TIMEOUT="${TIMEOUT:-120000}"

if [[ -z "$RESULTS_DIR" ]]; then
    if [[ -n "${BENCH_RUN_DIR:-}" ]]; then
        RESULTS_DIR="$BENCH_RUN_DIR/data"
    else
        RESULTS_DIR="benchmarks/mult_$(date +%Y%m%d_%H%M%S)"
    fi
fi

RUN_BENCH="$REPO_ROOT/scripts/run_benchmark.py"

# ---------------------------------------------------------------------------
# Game lists per phase
# ---------------------------------------------------------------------------
# Format: "game:streamliner" pairs.

# A: symmetric games (incremental vs from-scratch)
GAMES_A_DEFAULT="klondike:suit-symmetry free-cell:suit-symmetry black-hole:auto-foundations"

# B and C: flat-eligible games (no symmetry)
GAMES_BC_DEFAULT="klondike-deal-1:none free-cell:none bakers-game:none canfield:none somerset:none black-hole:auto-foundations"

# D: symmetric games (the critical LRU vs multiplicity test)
GAMES_D_DEFAULT="klondike:suit-symmetry klondike-deal-1:suit-symmetry free-cell:suit-symmetry black-hole:auto-foundations"

# Apply --games override if provided
apply_games_override() {
    local defaults="$1"
    if [[ -z "$GAMES_OVERRIDE" ]]; then
        echo "$defaults"
        return
    fi
    # Keep only entries whose game name is in the override list
    local result=""
    for entry in $defaults; do
        local game="${entry%%:*}"
        if echo ",$GAMES_OVERRIDE," | grep -q ",$game,"; then
            result="$result $entry"
        fi
    done
    if [[ -z "$result" ]]; then
        echo "Warning: --games filter matched nothing for this phase" >&2
    fi
    echo "$result"
}

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------

if ! $DRY_RUN; then
    MISSING=""
    [[ ! -x "$SOLVER" ]]     && MISSING="${MISSING}  $SOLVER (default solvitaire)\n"
    [[ ! -x "$SOLVER_LRU" ]] && [[ "$PHASES" == *[CD]* ]] && MISSING="${MISSING}  $SOLVER_LRU (LRU variant)\n"
    [[ ! -x "$SOLVER_FLAT" ]] && [[ "$PHASES" == *B* ]] && MISSING="${MISSING}  $SOLVER_FLAT (flat variant)\n"

    if [[ -n "$MISSING" ]]; then
        echo "FATAL: missing binaries:" >&2
        echo -e "$MISSING" >&2
        echo "Run: ./build.sh --release" >&2
        exit 1
    fi

    if [[ ! -f "$RUN_BENCH" ]]; then
        echo "FATAL: run_benchmark.py not found at $RUN_BENCH" >&2
        exit 1
    fi

    mkdir -p "$RESULTS_DIR"
fi

HAS_SCRATCH=true
if [[ ! -x "$SOLVER_SCRATCH" ]]; then
    HAS_SCRATCH=false
fi

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

echo "═══════════════════════════════════════════════════════════════"
echo " Multiplicity Cache Benchmark"
if $DRY_RUN; then
    echo " (DRY RUN — commands printed, nothing executed)"
fi
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "  Phases:   $PHASES"
echo "  Seeds:    $SEEDS"
echo "  Timeout:  ${TIMEOUT}ms"
echo "  Output:   $RESULTS_DIR"
if [[ -n "$GAMES_OVERRIDE" ]]; then
    echo "  Games:    $GAMES_OVERRIDE"
fi
if [[ "$PHASES" == *A* ]]; then
    if $HAS_SCRATCH; then
        echo "  Scratch:  $SOLVER_SCRATCH"
    else
        echo "  Scratch:  (not found — phase A will be skipped)"
    fi
fi
echo ""

# ---------------------------------------------------------------------------
# Helper: run a single benchmark (sequential, Ctrl-C kills it)
# ---------------------------------------------------------------------------

run_bench() {
    local label="$1"
    local solver="$2"
    local game="$3"
    local streamliner="$4"
    local outfile="$5"
    shift 5
    # remaining args passed to solver via --

    local cmd=(
        python3 "$RUN_BENCH"
        --solver "$solver"
        --type "$game"
        --seeds "$SEEDS"
        --timeout "$TIMEOUT"
        --output "$outfile"
        --label "$label"
        --no-summary
    )

    if [[ "$streamliner" != "none" ]]; then
        cmd+=(--streamliner "$streamliner")
    fi

    if [[ $# -gt 0 ]]; then
        cmd+=(-- "$@")
    fi

    if $DRY_RUN; then
        echo "  ${cmd[*]}"
    else
        echo "  [$(date +%H:%M:%S)] $label / $game (seeds $SEEDS, ${TIMEOUT}ms timeout)"
        "${cmd[@]}" || {
            echo "  WARNING: $label/$game failed (exit $?)" >&2
        }
    fi
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
# Same search tree, different descriptor update strategy.
# states_searched MUST be identical between the two.
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *A* ]]; then
    if ! $HAS_SCRATCH && ! $DRY_RUN; then
        echo "═══ Comparison A: SKIPPED (no from-scratch binary) ═══════════"
        echo "  See --help for build instructions."
        echo ""
    else
        echo "═══ Comparison A: Incremental vs From-Scratch Multiplicity ════"
        echo ""

        GAMES_A=$(apply_games_override "$GAMES_A_DEFAULT")
        for entry in $GAMES_A; do
            GAME="${entry%%:*}"
            STR="${entry##*:}"

            run_bench "A_incr" "$SOLVER" "$GAME" "$STR" \
                "$RESULTS_DIR/A_incr_${GAME}.csv" \
                --cache-type multiplicity

            run_bench "A_scratch" "$SOLVER_SCRATCH" "$GAME" "$STR" \
                "$RESULTS_DIR/A_scratch_${GAME}.csv" \
                --cache-type multiplicity
        done

        if ! $DRY_RUN; then
            merge_csvs "A_incr"
            merge_csvs "A_scratch"
        fi
        echo ""
    fi
fi

# ---------------------------------------------------------------------------
# Comparison B: Flat vs Multiplicity (no symmetry)
# ---------------------------------------------------------------------------
# Flat has smaller clusters (32B vs 64B). Quantifies multiplicity overhead.
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *B* ]]; then
    echo "═══ Comparison B: Flat vs Multiplicity (No Symmetry) ══════════"
    echo ""

    GAMES_B=$(apply_games_override "$GAMES_BC_DEFAULT")
    for entry in $GAMES_B; do
        GAME="${entry%%:*}"
        STR="${entry##*:}"

        run_bench "B_flat" "$SOLVER_FLAT" "$GAME" "$STR" \
            "$RESULTS_DIR/B_flat_${GAME}.csv"

        run_bench "B_mult" "$SOLVER" "$GAME" "$STR" \
            "$RESULTS_DIR/B_mult_${GAME}.csv" \
            --cache-type multiplicity
    done

    if ! $DRY_RUN; then
        merge_csvs "B_flat"
        merge_csvs "B_mult"
    fi
    echo ""
fi

# ---------------------------------------------------------------------------
# Comparison C: LRU vs Multiplicity (no symmetry)
# ---------------------------------------------------------------------------
# Multiplicity's flat O(1) lookup vs LRU's pile-ordering + Boost overhead.
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *C* ]]; then
    echo "═══ Comparison C: LRU vs Multiplicity (No Symmetry) ═══════════"
    echo ""

    GAMES_C=$(apply_games_override "$GAMES_BC_DEFAULT")
    for entry in $GAMES_C; do
        GAME="${entry%%:*}"
        STR="${entry##*:}"

        run_bench "C_lru" "$SOLVER_LRU" "$GAME" "$STR" \
            "$RESULTS_DIR/C_lru_${GAME}.csv"

        run_bench "C_mult" "$SOLVER" "$GAME" "$STR" \
            "$RESULTS_DIR/C_mult_${GAME}.csv" \
            --cache-type multiplicity
    done

    if ! $DRY_RUN; then
        merge_csvs "C_lru"
        merge_csvs "C_mult"
    fi
    echo ""
fi

# ---------------------------------------------------------------------------
# Comparison D: LRU vs Multiplicity WITH SUIT-SYMMETRY (the critical test)
# ---------------------------------------------------------------------------
# The whole point of the multiplicity encoding: suit-symmetry in flat cache.
# COLOUR mode (26 classes of 2): klondike, klondike-deal-1
# SUIT_IRRELEVANT mode (13 classes of 4): free-cell, black-hole
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *D* ]]; then
    echo "═══ Comparison D: LRU vs Multiplicity + Suit-Symmetry ═════════"
    echo "═══ (THE CRITICAL TEST)                                ═════════"
    echo ""

    GAMES_D=$(apply_games_override "$GAMES_D_DEFAULT")
    for entry in $GAMES_D; do
        GAME="${entry%%:*}"
        STR="${entry##*:}"

        run_bench "D_lru" "$SOLVER_LRU" "$GAME" "$STR" \
            "$RESULTS_DIR/D_lru_${GAME}.csv"

        run_bench "D_mult" "$SOLVER" "$GAME" "$STR" \
            "$RESULTS_DIR/D_mult_${GAME}.csv" \
            --cache-type multiplicity
    done

    if ! $DRY_RUN; then
        merge_csvs "D_lru"
        merge_csvs "D_mult"
    fi
    echo ""
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo "═══════════════════════════════════════════════════════════════"
if $DRY_RUN; then
    echo " Dry run complete. No benchmarks were executed."
else
    echo " Done. Results in $RESULTS_DIR/"
    echo ""
    echo "Files:"
    ls -1 "$RESULTS_DIR"/combined_*.csv 2>/dev/null || echo "  (no combined files)"
fi
echo ""
echo "Next steps:"
echo "  # Quick summary:"
echo "  Rscript analysis/summary.R $RESULTS_DIR/D_mult_klondike.csv"
echo ""
echo "  # Comparison report:"
echo "  Rscript analysis/benchmark.R \\"
echo "      --baseline $RESULTS_DIR/D_lru_klondike.csv \\"
echo "      --current  $RESULTS_DIR/D_mult_klondike.csv \\"
echo "      --output   $RESULTS_DIR/report_D_klondike.html"
if [[ "$PHASES" == *A* ]]; then
    echo ""
    echo "  # Validate Comparison A (states_searched must match):"
    echo "  diff <(cut -d, -f1,6 $RESULTS_DIR/combined_A_incr.csv) \\"
    echo "       <(cut -d, -f1,6 $RESULTS_DIR/combined_A_scratch.csv)"
fi
echo ""
echo "═══════════════════════════════════════════════════════════════"
