#!/usr/bin/env bash
#
# Metamorphic test: compare flat cache (M6 default) vs --force-lru (pre-M6 LRU)
# on outcome only.  Node counts will differ because flat cache omits pile ordering.
#
# Usage: scripts/metamorphic_test.sh [--exe <path>] [--seeds <N>]
#
# Defaults: exe=cmake-build-release/bin/solvitaire, seeds=5
#
# Exit code: 0 if all outcomes agree, 1 if any mismatch.

set -euo pipefail

EXE="cmake-build-release/bin/solvitaire"
SEEDS=5
TIMEOUT_MS=10000

while [[ $# -gt 0 ]]; do
    case "$1" in
        --exe)        EXE="$2";        shift 2 ;;
        --seeds)      SEEDS="$2";      shift 2 ;;
        --timeout-ms) TIMEOUT_MS="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ ! -x "$EXE" ]]; then
    echo "Executable not found: $EXE" >&2
    exit 2
fi

# Game types that use flat cache by default (use_new_cache returns true)
GAME_TYPES=(
    free-cell
    bakers-game
    somerset
    seahaven-towers
    spanish-patience
    flower-garden
    klondike
    black-hole
    golf
    fortunes-favor
    canfield-strict
)

PASS=0
FAIL=0

get_outcome() {
    local game="$1" seed="$2" extra_flags="${3:-}"
    local out
    # shellcheck disable=SC2086
    out=$("$EXE" --type "$game" --random "$seed" $extra_flags --timeout "$TIMEOUT_MS" --json 2>/dev/null) || true
    python3 -c "import sys,json; d=json.loads(sys.argv[1]); print(d['solution_type'])" "$out" 2>/dev/null || echo "error"
}

for game in "${GAME_TYPES[@]}"; do
    for seed in $(seq 1 "$SEEDS"); do
        flat_outcome=$(get_outcome "$game" "$seed" "")
        lru_outcome=$(get_outcome  "$game" "$seed" "--force-lru")

        if [[ "$flat_outcome" == "$lru_outcome" ]]; then
            echo "PASS  ${game} seed=${seed}: ${flat_outcome}"
            PASS=$((PASS+1))
        else
            echo "FAIL  ${game} seed=${seed}: flat=${flat_outcome} lru=${lru_outcome}"
            FAIL=$((FAIL+1))
        fi
    done
done

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[[ "$FAIL" -eq 0 ]]
