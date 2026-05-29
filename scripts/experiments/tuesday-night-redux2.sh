#!/usr/bin/env bash
#
# tuesday-night-redux2.sh — Multi-solver-variant version: compares lru/flat/hash-only/default across all games.
# (See tuesday-night-redux.sh for the simpler single-solver version.)
# NOTE: if $SOLVER is set in the environment, all four solver variables will be overridden to the same path.
#
# 35 distinct flat-cache-eligible games (alina excluded), 50 seeds, 1 warmup, median of 3, 60s timeout.
# Uses run_benchmark.py per game type with --label per variant, then merges into combined.csv.
#
# Intended to be run through bench:
#
#   bench --hook benchmark -m "Tuesday night redux" tuesday-night-redux \
#       -- scripts/experiments/tuesday-night-redux.sh
#
# Requires $BENCH_RUN_DIR to be set (bench does this automatically).
# Run from the ReSolvitaire code repo root.

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration — edit these to taste
# ---------------------------------------------------------------------------

LRUSOLVER="${SOLVER:-cmake-build-release/bin/solvitaire-lru}"
FLATSOLVER="${SOLVER:-cmake-build-release/bin/solvitaire-flat}"
DEFAULTSOLVER="${SOLVER:-cmake-build-release/bin/solvitaire}"
HASHSOLVER="${SOLVER:-cmake-build-release/bin/solvitaire-hash-only}"
SEEDS="${SEEDS:-1-50}"
TIMEOUT="${TIMEOUT:-60000}"       # ms
WARMUP="${WARMUP:-1}"
ITERATIONS="${ITERATIONS:-3}"     # median-of-3
RUNARGS="${RUNARGS:-}"             # args to pass to run_benchmark

    #alina

GAMES=(
    alpha-star
    somerset
    american-canister
    bakers-game
    beleaguered-castle
    black-hole
    british-canister
    canfield
    castles-of-spain
    chameleon
    delta-star
    duchess
    eight-off
    fan
    flower-garden
    fore-cell
    fortunes-favor
    free-cell
    golf
    king-albert
    klondike
    late-binding-solitaire
    northwest-territory
    raglan
    scotch-patience
    seahaven-towers
    siegecraft
    simple-simon
    spanish-patience
    streets-and-alleys
    stronghold
    thirty
    thirtysix
    trigon
    worm-hole
)

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------

if [[ -z "${BENCH_RUN_DIR:-}" ]]; then
    echo "FATAL: \$BENCH_RUN_DIR not set. Run this script through bench." >&2
    exit 1
fi

if [[ ! -x "$LRUSOLVER" ]]; then
    echo "FATAL: solver not found at $LRUSOLVER" >&2
    exit 1
fi

OUTDIR="$BENCH_RUN_DIR/data"
mkdir -p "$OUTDIR"

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RUN_BENCH="$SCRIPT_DIR/run_benchmark.py"

if [[ ! -f "$RUN_BENCH" ]]; then
    echo "FATAL: run_benchmark.py not found at $RUN_BENCH" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Run each game type
# ---------------------------------------------------------------------------

echo "=== Tuesday night redux ==="
echo "  Seeds:      $SEEDS"
echo "  Timeout:    ${TIMEOUT}ms"
echo "  Warmup:     $WARMUP"
echo "  Iterations: $ITERATIONS"
echo "  Games:      ${#GAMES[@]}"
echo "  Output:     $OUTDIR"
echo ""

FAILED=0

for GAME in "${GAMES[@]}"; do
    echo "--- $GAME --- $LRUSOLVER ---"
    python3 "$RUN_BENCH" \
        --solver "$LRUSOLVER" \
        --type "$GAME" \
        --seeds "$SEEDS" \
        --timeout "$TIMEOUT" \
        --warmup "$WARMUP" \
        --iterations "$ITERATIONS" \
        --no-summary \
        --label "$LRUSOLVER" \
        --output "$OUTDIR/${GAME}.csv" \
        -- --force-lru \
        $RUNARGS || {
            echo "WARNING: $GAME failed (exit $?), continuing" >&2
            FAILED=$((FAILED + 1))
        } 
    echo ""
done

for SOLVER in $DEFAULTSOLVER $FLATSOLVER $HASHSOLVER; do 
    for GAME in "${GAMES[@]}"; do
    echo "--- $GAME --- $SOLVER ---"
    python3 "$RUN_BENCH" \
        --solver "$SOLVER" \
        --type "$GAME" \
        --seeds "$SEEDS" \
        --timeout "$TIMEOUT" \
        --warmup "$WARMUP" \
        --iterations "$ITERATIONS" \
        --no-summary \
        --label "$SOLVER" \
        --output "$OUTDIR/${GAME}.csv" \
        $RUNARGS || {
            echo "WARNING: $GAME failed (exit $?), continuing" >&2
            FAILED=$((FAILED + 1))
        } 
    echo ""
done
done

# ---------------------------------------------------------------------------
# Merge into combined.csv
# ---------------------------------------------------------------------------

echo "Merging results..."
FIRST=1
for f in "$OUTDIR"/*.csv; do
    [[ "$(basename "$f")" == "combined.csv" ]] && continue
    if [[ "$FIRST" == 1 ]]; then
        head -1 "$f" > "$OUTDIR/combined.csv"
        FIRST=0
    fi
    tail -n +2 "$f" >> "$OUTDIR/combined.csv"
done

ROWS=$(wc -l < "$OUTDIR/combined.csv")
echo ""
echo "=== Done ==="
echo "  Combined: $ROWS rows (including header)"
echo "  Output:   $OUTDIR/combined.csv"
[[ "$FAILED" -gt 0 ]] && echo "  WARNING: $FAILED game(s) failed"

exit 0
