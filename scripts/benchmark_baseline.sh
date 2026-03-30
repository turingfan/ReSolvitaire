#!/usr/bin/env bash
#
# Throughput baseline: record flat-cache nodes/second for all flat-cache games.
#
# Results are printed as a table and saved to results/throughput_baseline_<date>.json
# for future milestone comparisons.
#
# Usage: scripts/benchmark_baseline.sh [--exe <path>] [--seeds <start> <end>]
#                                       [--iterations <N>] [--timeout-ms <ms>]
#                                       [--out <file>]
#
# Defaults: exe=cmake-build-release/bin/solvitaire
#           seeds 1-20, 1 iteration, warmup=true, timeout=30000ms
#           out=results/throughput_baseline_<YYYYMMDD>.json

set -euo pipefail

EXE="cmake-build-release/bin/solvitaire"
SEED_START=1
SEED_END=20
ITERATIONS=1
TIMEOUT_MS=30000
OUTFILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --exe)        EXE="$2";        shift 2 ;;
        --seeds)      SEED_START="$2"; SEED_END="$3"; shift 3 ;;
        --iterations) ITERATIONS="$2"; shift 2 ;;
        --timeout-ms) TIMEOUT_MS="$2"; shift 2 ;;
        --out)        OUTFILE="$2";    shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ ! -x "$EXE" ]]; then
    echo "Executable not found: $EXE" >&2
    exit 2
fi

mkdir -p docs/cache-redesign/benchmarks

if [[ -z "$OUTFILE" ]]; then
    OUTFILE="docs/cache-redesign/benchmarks/throughput_baseline_$(date +%Y%m%d).json"
fi

# All flat-cache game types (use_new_cache returns true)
GAMES=(
    free-cell
    bakers-game
    somerset
    seahaven-towers
    flower-garden
    spanish-patience
    black-hole
    golf
    klondike
    fortunes-favor
    canfield-strict
)

extract() {
    python3 -c "import sys,json; d=json.loads(sys.argv[1]); print(d['aggregate_stats'].get('$2', 0))" "$1"
}

printf "\n%-22s %14s %12s %12s\n" "Game" "Mnodes/s" "Mean(ms)" "Median(ms)"
printf '%s\n' "$(printf '=%.0s' {1..64})"

# Accumulate JSON entries
json_entries=()

for game in "${GAMES[@]}"; do
    raw=$(${EXE} --type "${game}" --streamliners none --benchmark \
        --benchmark-seeds ${SEED_START} ${SEED_END} \
        --benchmark-iterations ${ITERATIONS} \
        --benchmark-warmup true \
        --timeout ${TIMEOUT_MS} 2>/dev/null)

    nps=$(extract      "$raw" "nodes_per_second")
    mean_us=$(extract  "$raw" "mean_time_us")
    median_us=$(extract "$raw" "median_time_us")
    mean_nodes=$(extract "$raw" "mean_nodes")
    median_nodes=$(extract "$raw" "median_nodes")

    python3 - "$game" "$nps" "$mean_us" "$median_us" <<'PYEOF'
import sys
game, nps, mean_us, median_us = sys.argv[1:]
mn = float(nps)/1e6
print(f"{game:<22} {mn:>14.3f} {float(mean_us)/1000:>12.1f} {float(median_us)/1000:>12.1f}")
PYEOF

    json_entries+=("$(python3 - "$game" "$nps" "$mean_us" "$median_us" "$mean_nodes" "$median_nodes" \
        "$SEED_START" "$SEED_END" "$ITERATIONS" "$TIMEOUT_MS" <<'PYEOF'
import sys, json
game, nps, mean_us, median_us, mean_nodes, median_nodes, s1, s2, iters, tms = sys.argv[1:]
print(json.dumps({
    "game": game,
    "seeds": f"{s1}-{s2}",
    "iterations": int(iters),
    "timeout_ms": int(tms),
    "streamliner": "none",
    "nodes_per_second": float(nps),
    "mean_nodes": float(mean_nodes),
    "median_nodes": float(median_nodes),
    "mean_time_us": float(mean_us),
    "median_time_us": float(median_us),
}))
PYEOF
)")
done

printf '\nSeeds %d-%d, %d iteration(s) per seed, timeout %dms, streamliners=none\n' \
    ${SEED_START} ${SEED_END} ${ITERATIONS} ${TIMEOUT_MS}

# Write JSON output
(
    echo "{"
    echo "  \"date\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "  \"seeds\": \"${SEED_START}-${SEED_END}\","
    echo "  \"iterations\": ${ITERATIONS},"
    echo "  \"timeout_ms\": ${TIMEOUT_MS},"
    echo "  \"streamliner\": \"none\","
    echo "  \"results\": ["
    first=1
    for entry in "${json_entries[@]}"; do
        if [[ $first -eq 1 ]]; then
            first=0
        else
            echo ","
        fi
        printf '    %s' "$entry"
    done
    echo ""
    echo "  ]"
    echo "}"
) > "$OUTFILE"

printf 'Results saved to: %s\n' "$OUTFILE"
