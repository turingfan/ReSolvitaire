#!/usr/bin/env bash
set -euo pipefail

# Validation harness for variant binaries.
# Compares the 'solution_type' (outcome) of the default, flat, hash-only, and LRU binaries.
# Requirement: jq must be installed.

if ! command -v jq &> /dev/null; then
    echo "Error: jq is not installed. Please install it to run this validation script." >&2
    exit 1
fi

TYPES="klondike free-cell"
TARGET_MATCHES=10
TIMEOUT=60000

# Check if binaries exist
BIN_DIR="cmake-build-release/bin"
BINS=("solvitaire" "solvitaire-flat" "solvitaire-hash-only" "solvitaire-lru")

for bin in "${BINS[@]}"; do
    if [[ ! -f "$BIN_DIR/$bin" ]]; then
        echo "Error: Binary $BIN_DIR/$bin not found. Build the project first." >&2
        exit 1
    fi
done

for type in $TYPES; do
    echo "Validating search parity for type: $type"
    matches=0
    seed=1
    
    while [[ $matches -lt $TARGET_MATCHES ]]; do
        # Run all variants with a timeout
        default=$($BIN_DIR/solvitaire           --type "$type" --random "$seed" --json --timeout $TIMEOUT | jq -r .solution_type | tr '[:upper:]' '[:lower:]')
        flat=$($BIN_DIR/solvitaire-flat          --type "$type" --random "$seed" --json --timeout $TIMEOUT | jq -r .solution_type | tr '[:upper:]' '[:lower:]')
        hash=$($BIN_DIR/solvitaire-hash-only     --type "$type" --random "$seed" --json --timeout $TIMEOUT | jq -r .solution_type | tr '[:upper:]' '[:lower:]')
        lru=$($BIN_DIR/solvitaire-lru            --type "$type" --random "$seed" --json --force-lru --timeout $TIMEOUT | jq -r .solution_type | tr '[:upper:]' '[:lower:]')

        # Filter outcomes to find non-timeout versions
        # NOTE: empty outcomes (due to crash or jq failure) are treated as errors
        outcomes=("$default" "$flat" "$hash" "$lru")
        non_timeouts=()
        all_finished=true
        
        for o in "${outcomes[@]}"; do
            if [[ -z "$o" || "$o" == "null" ]]; then
                echo "  [FAIL] Binary failed to produce valid JSON for seed $seed" >&2
                exit 1
            fi
            if [[ "$o" != "timeout" ]]; then
                non_timeouts+=("$o")
            else
                all_finished=false
            fi
        done

        # Identify unique real outcomes
        unique_outcomes=$(printf "%s\n" "${non_timeouts[@]}" | sort | uniq)
        num_unique=$(echo "$unique_outcomes" | grep -c . || true)

        if [[ $num_unique -gt 1 ]]; then
            echo "  [FAIL] MEANINGFUL MISMATCH in $type seed=$seed" >&2
            echo "    default: $default" >&2
            echo "    flat:    $flat" >&2
            echo "    hash:    $hash" >&2
            echo "    lru:     $lru" >&2
            exit 1
        fi

        if [[ $num_unique -eq 1 ]]; then
            if [[ "$all_finished" == "true" ]]; then
                echo "  [ok] Seed $seed matched: $unique_outcomes"
                matches=$((matches + 1))
            else
                # Some timed out, but those that finished agreed.
                # Per user request, this is OK, but we "run one more instance" to hit the TARGET_MATCHES quota of full matches.
                echo "  [info] Seed $seed partial match: $unique_outcomes (some binaries timed out). Running one more..."
            fi
        else
            # num_unique is 0, meaning ALL timed out
            echo "  [skip] Seed $seed all binaries timed out. Running one more..."
        fi
        
        seed=$((seed + 1))
    done
done

echo "Success: All outcomes match (found $TARGET_MATCHES full comparisons per type)."
