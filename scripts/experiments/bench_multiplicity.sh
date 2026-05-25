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
#   B. Flat vs multiplicity (no symmetry). Quantifies multiplicity overhead.
#   C. LRU vs multiplicity (no symmetry). Multiplicity should beat LRU easily.
#   D. LRU vs multiplicity WITH SUIT-SYMMETRY. THE CRITICAL TEST.
#
# Each comparison generates run_benchmark.py commands chunked by seed range,
# then runs them in parallel using xargs -P. This reuses the existing
# benchmarking infrastructure (run_benchmark.py handles timing, memory
# measurement, CSV output, timeout handling) and gives proper job control
# (Ctrl-C kills all workers cleanly via process group).
#
# ═══════════════════════════════════════════════════════════════════════════════
# USAGE
# ═══════════════════════════════════════════════════════════════════════════════
#
# With no arguments, prints this help and exits.
#
#   # Dry run — shows commands, runs nothing:
#   ./scripts/experiments/bench_multiplicity.sh --dry-run --phase D --seeds 1-10
#
#   # Smoke test (5 seeds, 1 game, 1 worker):
#   ./scripts/experiments/bench_multiplicity.sh --phase D --seeds 1-5 --games klondike
#
#   # Quick validation (50 seeds, all phases, 4 workers):
#   ./scripts/experiments/bench_multiplicity.sh --phase ABCD --seeds 1-50 --workers 4
#
#   # Full run on big machine (500 seeds, 16 workers):
#   ./scripts/experiments/bench_multiplicity.sh --phase ABCD --seeds 1-500 --workers 16
#
# Via bench (recommended for archiving results in DataLad):
#
#   bench --detached --bundle-format tar \
#       -m "Multiplicity benchmark" mult-bench-v1 \
#       -- scripts/experiments/bench_multiplicity.sh --phase D --seeds 1-500 --workers 16
#
# ═══════════════════════════════════════════════════════════════════════════════
# OPTIONS
# ═══════════════════════════════════════════════════════════════════════════════
#
#   --phase XY        Which comparisons to run (required; e.g. D or ABCD)
#   --seeds N-M       Seed range (required; e.g. 1-500)
#   --workers N       Parallel workers (default: 1 = sequential)
#   --chunk-size N    Seeds per chunk for parallelism (default: 25)
#   --games g1,g2     Comma-separated game filter (default: all games for phase)
#   --timeout MS      Timeout per instance in ms (default: 120000)
#   --outdir DIR      Output directory (default: benchmarks/mult_<timestamp>/)
#   --dry-run         Print commands without executing
#   --help, -h        Print this help
#
# Environment overrides:
#   BIN_DIR           Binary directory (default: cmake-build-release/bin)
#   SOLVER            Override default solvitaire binary
#   SOLVER_SCRATCH    From-scratch binary for Comparison A
#
# ═══════════════════════════════════════════════════════════════════════════════
# COMPARISONS
# ═══════════════════════════════════════════════════════════════════════════════
#
# A — Incremental vs from-scratch multiplicity
#     Games: klondike (suit-sym), free-cell (suit-sym), black-hole (auto-found)
#     Requires solvitaire-mult-scratch binary; skipped if absent.
#     states_searched MUST be identical — any mismatch is a bug.
#
# B — Flat vs multiplicity (no symmetry)
#     Games: klondike-deal-1, free-cell, bakers-game, canfield, somerset, black-hole
#     Flat has 32B entries, multiplicity has 64B. Flat should win slightly.
#
# C — LRU vs multiplicity (no symmetry)
#     Same games as B. Multiplicity should beat LRU comfortably.
#
# D — LRU vs multiplicity WITH SUIT-SYMMETRY (critical test)
#     Games: klondike (COLOUR), klondike-deal-1 (COLOUR), free-cell (SI),
#            black-hole (SI, inherent)
#     The whole point of the multiplicity encoding project.
#
# ═══════════════════════════════════════════════════════════════════════════════
# PREREQUISITES
# ═══════════════════════════════════════════════════════════════════════════════
#
# 1. Build release binaries:  ./build.sh --release
#
# 2. (Comparison A only) Build a from-scratch multiplicity binary:
#    In game_state.cpp, replace incremental_update_none()/incremental_update()
#    calls with recompute_all(), build, copy to solvitaire-mult-scratch, revert.
#    See docs/multiplicity-encoding/stage5-4-benchmark-plan.md for details.
#
# ═══════════════════════════════════════════════════════════════════════════════
# OUTPUT & ANALYSIS
# ═══════════════════════════════════════════════════════════════════════════════
#
# Per-run CSVs:   <outdir>/<phase>_<variant>_<game>_<start>_<end>.csv
# Combined CSVs:  <outdir>/combined_<phase>_<variant>.csv
#
# Analyse with:
#   Rscript analysis/summary.R <outdir>/combined_D_mult.csv
#   Rscript analysis/benchmark.R --baseline <outdir>/combined_D_lru.csv \
#       --current <outdir>/combined_D_mult.csv --output report.html
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
WORKERS=1
CHUNK_SIZE=25
GAMES_OVERRIDE=""
TIMEOUT=120000
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
        --phase)      PHASES="$2"; shift 2 ;;
        --seeds)      SEEDS="$2"; shift 2 ;;
        --workers)    WORKERS="$2"; shift 2 ;;
        --chunk-size) CHUNK_SIZE="$2"; shift 2 ;;
        --games)      GAMES_OVERRIDE="$2"; shift 2 ;;
        --timeout)    TIMEOUT="$2"; shift 2 ;;
        --outdir)     RESULTS_DIR="$2"; shift 2 ;;
        --dry-run)    DRY_RUN=true; shift ;;
        --help|-h)    show_help; exit 0 ;;
        *)
            echo "Unknown option: $1  (try --help)" >&2
            exit 1
            ;;
    esac
done

if [[ -z "$PHASES" ]]; then
    echo "Error: --phase is required (e.g. --phase D or --phase ABCD)" >&2
    exit 1
fi
if [[ -z "$SEEDS" ]]; then
    echo "Error: --seeds is required (e.g. --seeds 1-50)" >&2
    exit 1
fi

SEED_LO="${SEEDS%-*}"
SEED_HI="${SEEDS#*-}"
if ! [[ "$SEED_LO" =~ ^[0-9]+$ ]] || ! [[ "$SEED_HI" =~ ^[0-9]+$ ]]; then
    echo "Error: --seeds must be N-M (e.g. 1-500)" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

BIN_DIR="${BIN_DIR:-cmake-build-release/bin}"
SOLVER="${SOLVER:-$BIN_DIR/solvitaire}"
SOLVER_LRU="$BIN_DIR/solvitaire-lru"
SOLVER_FLAT="$BIN_DIR/solvitaire-flat"
SOLVER_SCRATCH="${SOLVER_SCRATCH:-$BIN_DIR/solvitaire-mult-scratch}"

RUN_BENCH="$REPO_ROOT/scripts/run_benchmark.py"

if [[ -z "$RESULTS_DIR" ]]; then
    if [[ -n "${BENCH_RUN_DIR:-}" ]]; then
        RESULTS_DIR="$BENCH_RUN_DIR/data"
    else
        RESULTS_DIR="benchmarks/mult_$(date +%Y%m%d_%H%M%S)"
    fi
fi

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------

if ! $DRY_RUN; then
    MISSING=""
    [[ ! -x "$SOLVER" ]]      && MISSING="${MISSING}  $SOLVER\n"
    [[ ! -x "$SOLVER_LRU" ]]  && [[ "$PHASES" == *[CD]* ]] && MISSING="${MISSING}  $SOLVER_LRU\n"
    [[ ! -x "$SOLVER_FLAT" ]] && [[ "$PHASES" == *B* ]]    && MISSING="${MISSING}  $SOLVER_FLAT\n"
    [[ ! -f "$RUN_BENCH" ]]   && MISSING="${MISSING}  $RUN_BENCH\n"

    if [[ -n "$MISSING" ]]; then
        echo "FATAL: missing:" >&2
        echo -e "$MISSING" >&2
        echo "Run: ./build.sh --release" >&2
        exit 1
    fi
    mkdir -p "$RESULTS_DIR"
fi

HAS_SCRATCH=true
[[ ! -x "$SOLVER_SCRATCH" ]] && HAS_SCRATCH=false

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

echo "═══════════════════════════════════════════════════════════════"
echo " Multiplicity Cache Benchmark"
$DRY_RUN && echo " (DRY RUN — commands printed, nothing executed)"
echo "═══════════════════════════════════════════════════════════════"
echo "  Phases:     $PHASES"
echo "  Seeds:      $SEEDS"
echo "  Workers:    $WORKERS"
echo "  Chunk size: $CHUNK_SIZE"
echo "  Timeout:    ${TIMEOUT}ms"
echo "  Output:     $RESULTS_DIR"
[[ -n "$GAMES_OVERRIDE" ]] && echo "  Games:      $GAMES_OVERRIDE"
if [[ "$PHASES" == *A* ]]; then
    $HAS_SCRATCH && echo "  Scratch:    $SOLVER_SCRATCH" || echo "  Scratch:    (not found — phase A skipped)"
fi
echo ""

# ---------------------------------------------------------------------------
# Command generation
# ---------------------------------------------------------------------------
# Each phase generates run_benchmark.py commands into a temp file.
# Each command handles one (solver, game, seed-chunk) combination.
# xargs -P runs them in parallel with proper signal handling.
#
# Format: one complete shell command per line.
# ---------------------------------------------------------------------------

CMDFILE=$(mktemp)
trap 'rm -f "$CMDFILE"' EXIT

# emit_chunks LABEL SOLVER GAME STREAMLINER [EXTRA_SOLVER_ARGS...]
#
# Emits one run_benchmark.py command per seed chunk.
emit_chunks() {
    local label="$1" solver="$2" game="$3" streamliner="$4"
    shift 4
    local extra_args=("$@")

    local lo=$SEED_LO
    while [[ $lo -le $SEED_HI ]]; do
        local hi=$((lo + CHUNK_SIZE - 1))
        [[ $hi -gt $SEED_HI ]] && hi=$SEED_HI

        local outfile="$RESULTS_DIR/${label}_${game}_${lo}_${hi}.csv"
        local cmd="python3 '$RUN_BENCH' --solver '$solver' --type '$game'"
        cmd+=" --seeds ${lo}-${hi} --timeout $TIMEOUT"
        cmd+=" --output '$outfile' --label '$label' --no-summary"

        if [[ "$streamliner" != "none" ]]; then
            cmd+=" --streamliner '$streamliner'"
        fi

        if [[ ${#extra_args[@]} -gt 0 ]]; then
            cmd+=" --"
            for arg in "${extra_args[@]}"; do
                cmd+=" '$arg'"
            done
        fi

        echo "$cmd" >> "$CMDFILE"
        lo=$((hi + 1))
    done
}

# game_matches GAME
# Returns 0 if game matches --games filter (or no filter set).
game_matches() {
    [[ -z "$GAMES_OVERRIDE" ]] && return 0
    echo ",$GAMES_OVERRIDE," | grep -q ",$1," && return 0
    return 1
}

# ---------------------------------------------------------------------------
# Phase A: Incremental vs from-scratch multiplicity
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *A* ]]; then
    if ! $HAS_SCRATCH && ! $DRY_RUN; then
        echo "Phase A: SKIPPED (no from-scratch binary at $SOLVER_SCRATCH)"
        echo "  See --help for build instructions."
        echo ""
    else
        echo "Phase A: Incremental vs From-Scratch Multiplicity"

        # game:streamliner pairs
        for pair in klondike:suit-symmetry free-cell:suit-symmetry black-hole:auto-foundations; do
            game="${pair%%:*}"; str="${pair##*:}"
            game_matches "$game" || continue
            emit_chunks "A_incr"    "$SOLVER"         "$game" "$str" --cache-type multiplicity
            emit_chunks "A_scratch" "$SOLVER_SCRATCH"  "$game" "$str" --cache-type multiplicity
        done
    fi
fi

# ---------------------------------------------------------------------------
# Phase B: Flat vs Multiplicity (no symmetry)
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *B* ]]; then
    echo "Phase B: Flat vs Multiplicity (No Symmetry)"

    for pair in klondike-deal-1:none free-cell:none bakers-game:none \
                canfield:none somerset:none black-hole:auto-foundations; do
        game="${pair%%:*}"; str="${pair##*:}"
        game_matches "$game" || continue
        emit_chunks "B_flat" "$SOLVER_FLAT" "$game" "$str"
        emit_chunks "B_mult" "$SOLVER"      "$game" "$str" --cache-type multiplicity
    done
fi

# ---------------------------------------------------------------------------
# Phase C: LRU vs Multiplicity (no symmetry)
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *C* ]]; then
    echo "Phase C: LRU vs Multiplicity (No Symmetry)"

    for pair in klondike-deal-1:none free-cell:none bakers-game:none \
                canfield:none somerset:none black-hole:auto-foundations; do
        game="${pair%%:*}"; str="${pair##*:}"
        game_matches "$game" || continue
        emit_chunks "C_lru"  "$SOLVER_LRU" "$game" "$str"
        emit_chunks "C_mult" "$SOLVER"     "$game" "$str" --cache-type multiplicity
    done
fi

# ---------------------------------------------------------------------------
# Phase D: LRU vs Multiplicity + Suit-Symmetry (THE CRITICAL TEST)
# ---------------------------------------------------------------------------

if [[ "$PHASES" == *D* ]]; then
    echo "Phase D: LRU vs Multiplicity + Suit-Symmetry (THE CRITICAL TEST)"

    for pair in klondike:suit-symmetry klondike-deal-1:suit-symmetry \
                free-cell:suit-symmetry black-hole:auto-foundations; do
        game="${pair%%:*}"; str="${pair##*:}"
        game_matches "$game" || continue
        emit_chunks "D_lru"  "$SOLVER_LRU" "$game" "$str"
        emit_chunks "D_mult" "$SOLVER"     "$game" "$str" --cache-type multiplicity
    done
fi

# ---------------------------------------------------------------------------
# Execute or display
# ---------------------------------------------------------------------------

TOTAL=$(wc -l < "$CMDFILE" | tr -d ' ')

if [[ "$TOTAL" -eq 0 ]]; then
    echo ""
    echo "No commands generated (check --phase, --games, and prerequisites)."
    exit 0
fi

echo ""
echo "$TOTAL chunks to run ($WORKERS workers)"
echo ""

if $DRY_RUN; then
    cat "$CMDFILE"
    echo ""
    echo "(dry run — nothing executed)"
    exit 0
fi

# Run with xargs -P for parallelism.
# The process group ensures Ctrl-C kills all workers.
xargs -P "$WORKERS" -I '{}' bash -c '{}' < "$CMDFILE"

# ---------------------------------------------------------------------------
# Merge chunk CSVs into per-label combined files
# ---------------------------------------------------------------------------

echo ""
echo "Merging results..."

# Collect unique labels from filenames
LABELS=$(ls "$RESULTS_DIR"/*.csv 2>/dev/null \
    | xargs -I{} basename {} \
    | sed 's/_[^_]*_[0-9]*_[0-9]*.csv$//' \
    | sort -u)

for label in $LABELS; do
    combined="$RESULTS_DIR/combined_${label}.csv"
    first=1
    for f in "$RESULTS_DIR"/${label}_*.csv; do
        [[ -f "$f" ]] || continue
        [[ "$(basename "$f")" == combined_* ]] && continue
        if [[ "$first" == 1 ]]; then
            head -1 "$f" > "$combined"
            first=0
        fi
        tail -n +2 "$f" >> "$combined"
    done
    if [[ "$first" == 0 ]]; then
        rows=$(wc -l < "$combined" | tr -d ' ')
        echo "  combined_${label}.csv ($rows rows)"
    fi
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " Done. Results in $RESULTS_DIR/"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Combined files:"
ls -1 "$RESULTS_DIR"/combined_*.csv 2>/dev/null || echo "  (none)"
echo ""
echo "Analysis examples:"
echo ""
echo "  # Quick summary:"
echo "  Rscript analysis/summary.R $RESULTS_DIR/combined_D_mult.csv"
echo ""
echo "  # Full comparison report:"
echo "  Rscript analysis/benchmark.R \\"
echo "      --baseline $RESULTS_DIR/combined_D_lru.csv \\"
echo "      --current  $RESULTS_DIR/combined_D_mult.csv \\"
echo "      --output   $RESULTS_DIR/report_D.html"
if [[ "$PHASES" == *A* ]]; then
    echo ""
    echo "  # Validate Comparison A (states_searched must match):"
    echo "  diff <(cut -d, -f1,6 $RESULTS_DIR/combined_A_incr.csv | sort) \\"
    echo "       <(cut -d, -f1,6 $RESULTS_DIR/combined_A_scratch.csv | sort)"
fi
echo ""
