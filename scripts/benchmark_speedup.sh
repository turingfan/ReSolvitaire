#!/usr/bin/env bash
#
# Benchmark M6 speedup: flat cache vs --force-lru (LRU with pile ordering).
#
# Runs seed-based benchmarks for each game type, then prints a summary table
# comparing mean solve time and nodes/second between the two cache strategies.
#
# Usage: scripts/benchmark_speedup.sh [--exe <path>] [--seeds <start> <end>]
#                                      [--iterations <N>] [--timeout-ms <ms>]
#
# Defaults: exe=cmake-build-release/bin/solvitaire
#           seeds 1-20, 1 iteration per seed, warmup=true, timeout=30000ms
#
# Excluded games (confounded comparisons):
#   spanish-patience  -- 13 interchangeable piles (too many confounders)
#   black-hole        -- hole game: LRU always applies suit symmetry internally
#   golf, worm-hole   -- hole games (same reason)

set -euo pipefail

EXE="cmake-build-release/bin/solvitaire"
SEED_START=1
SEED_END=20
ITERATIONS=1
TIMEOUT_MS=30000

while [[ $# -gt 0 ]]; do
    case "$1" in
        --exe)        EXE="$2";        shift 2 ;;
        --seeds)      SEED_START="$2"; SEED_END="$3"; shift 3 ;;
        --iterations) ITERATIONS="$2"; shift 2 ;;
        --timeout-ms) TIMEOUT_MS="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ ! -x "$EXE" ]]; then
    echo "Executable not found: $EXE" >&2
    exit 2
fi

# Clean games for speedup measurement: no hole, no intrinsic suit symmetry
GAMES=(free-cell bakers-game somerset seahaven-towers flower-garden)

extract() {
    # $1 = JSON string, $2 = field name
    python3 -c "import sys,json; d=json.loads(sys.argv[1]); print(d['aggregate_stats'].get('$2', 0))" "$1"
}

printf "\n%-20s %12s %12s %10s %14s %14s\n" \
    "Game" "Flat(ms)" "LRU(ms)" "Speedup" "Flat(Mnodes/s)" "LRU(Mnodes/s)"
printf '%s\n' "$(printf '=%.0s' {1..86})"

for game in "${GAMES[@]}"; do
    flat_json=$(${EXE} --type "${game}" --streamliners none --benchmark \
        --benchmark-seeds ${SEED_START} ${SEED_END} \
        --benchmark-iterations ${ITERATIONS} \
        --benchmark-warmup true \
        --timeout ${TIMEOUT_MS} 2>/dev/null)

    lru_json=$(${EXE} --type "${game}" --streamliners none --benchmark \
        --benchmark-seeds ${SEED_START} ${SEED_END} \
        --benchmark-iterations ${ITERATIONS} \
        --benchmark-warmup true \
        --timeout ${TIMEOUT_MS} \
        --force-lru 2>/dev/null)

    geo_mean_us=$(extract "$flat_json" "geometric_mean_time_us")
    lru_geo_us=$(extract  "$lru_json"  "geometric_mean_time_us")
    flat_median_us=$(extract "$flat_json" "median_time_us")
    lru_median_us=$(extract  "$lru_json"  "median_time_us")
    flat_nps=$(extract     "$flat_json" "mean_nps")
    lru_nps=$(extract      "$lru_json"  "mean_nps")

    python3 - "$game" "$flat_median_us" "$lru_median_us" "$geo_mean_us" "$lru_geo_us" "$flat_nps" "$lru_nps" <<'PYEOF'
import sys
game, f_med, l_med, f_geo, l_geo, f_nps, l_nps = sys.argv[1:]
f_med=float(f_med); l_med=float(l_med); f_geo=float(f_geo); l_geo=float(l_geo)
f_nps=float(f_nps); l_nps=float(l_nps)
speedup = l_geo / f_geo if f_geo > 0 else float('nan')
print(f"{game:<20} {f_med/1000:>12.1f} {l_med/1000:>12.1f} {speedup:>10.2f}x {f_nps/1e6:>14.2f} {l_nps/1e6:>14.2f}")
PYEOF
done

printf '\n'
printf 'Seeds %d-%d, %d iteration(s) per seed, timeout %dms, streamliners=none\n' \
    ${SEED_START} ${SEED_END} ${ITERATIONS} ${TIMEOUT_MS}
printf 'Speedup = LRU geo-mean / flat geo-mean (>1x = flat is faster)\n'
