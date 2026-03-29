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

    flat_mean_us=$(extract "$flat_json" "mean_time_us")
    lru_mean_us=$(extract  "$lru_json"  "mean_time_us")
    flat_nps=$(extract     "$flat_json" "nodes_per_second")
    lru_nps=$(extract      "$lru_json"  "nodes_per_second")

    python3 - "$game" "$flat_mean_us" "$lru_mean_us" "$flat_nps" "$lru_nps" <<'PYEOF'
import sys
game, flat_us, lru_us, flat_nps, lru_nps = sys.argv[1:]
flat_us  = float(flat_us);  lru_us  = float(lru_us)
flat_nps = float(flat_nps); lru_nps = float(lru_nps)
flat_ms  = flat_us / 1000;  lru_ms  = lru_us / 1000
speedup  = lru_ms / flat_ms if flat_ms > 0 else float('nan')
flat_mn  = flat_nps / 1e6;  lru_mn  = lru_nps / 1e6
print(f"{game:<20} {flat_ms:>12.1f} {lru_ms:>12.1f} {speedup:>10.2f}x {flat_mn:>14.2f} {lru_mn:>14.2f}")
PYEOF
done

printf '\n'
printf 'Seeds %d-%d, %d iteration(s) per seed, timeout %dms, streamliners=none\n' \
    ${SEED_START} ${SEED_END} ${ITERATIONS} ${TIMEOUT_MS}
printf 'Speedup = LRU mean time / flat mean time (>1x = flat is faster)\n'
