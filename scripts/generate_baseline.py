#!/usr/bin/env python3
"""
Generate the Level 1 regression oracle by running the solver on each JSON
instance file with the streamliner that matches the ground-truth experiment.

Usage:
  python3 scripts/generate_baseline.py \
      --exe cmake-build-release/bin/solvitaire \
      --instances tests/resources/level1 \
      --output tests/oracles/level1.json \
      --data-dir /path/to/solvitaire-paper-v10-Feb2026

The --data-dir argument is optional.  When supplied, the script reads the
AAA-smartfiles / AAA-singlerunfiles index to determine whether each instance
was originally solved with '--streamliners both' (smart-run, winnable in
Run 1) or '--streamliners none' (single-run or smart-run unsolvable).  This
ensures oracle state counts match the ground-truth experimental results.

When --data-dir is omitted every instance is run with '--streamliners none'
(safe but produces oracle values that differ from the paper for smart-run
winnable games such as free-cell, klondike, and spanish-patience).
"""

import os
import gzip
import subprocess
import json
import argparse
import time


# ---------------------------------------------------------------------------
# Ground-truth lookup
# ---------------------------------------------------------------------------

def load_aaa_sets(data_dir):
    aaa_base = os.path.join(data_dir, "AnalysisScripts")
    smart_set, single_set = set(), set()
    for fname in ["AAA-smartfiles", "AAA-smartnotimefiles"]:
        p = os.path.join(aaa_base, fname)
        if os.path.exists(p):
            for line in open(p):
                smart_set.add(line.strip())
    for fname in ["AAA-singlerunfiles", "AAA-singlerunnotimefiles"]:
        p = os.path.join(aaa_base, fname)
        if os.path.exists(p):
            for line in open(p):
                single_set.add(line.strip())
    return smart_set, single_set


def build_streamliner_map(data_dir, instance_filenames):
    """
    Return {filename: 'both'|'none'} by scanning the experimental CSVs.

    Logic mirrors export_test_deals.py:
      - Smart-run, winnable in Run 1  => streamliner 'both'
      - Everything else               => streamliner 'none'
    """
    smart_set, _ = load_aaa_sets(data_dir)
    results_dir = os.path.join(data_dir, "ExperimentalResults")

    # Parse the needed (game, seed) pairs from filenames
    needed = {}
    for fname in instance_filenames:
        parts = fname.replace(".json", "").split("_seed_")
        if len(parts) == 2:
            game, seed = parts[0], int(parts[1])
            needed.setdefault(game, {})[seed] = fname

    streamliner_map = {}

    for game, seeds in sorted(needed.items()):
        game_dir = os.path.join(results_dir, game)
        if not os.path.isdir(game_dir):
            print(f"  [WARN] No ground-truth directory for {game} - defaulting to 'none'")
            for fname in seeds.values():
                streamliner_map[fname] = "none"
            continue

        found = set()
        for root, _, files in os.walk(game_dir):
            if len(found) == len(seeds):
                break
            for csv_fname in sorted(files):
                if not csv_fname.endswith(".csv.gz"):
                    continue
                csv_path = os.path.join(root, csv_fname)
                rel = csv_path.split("ExperimentalResults/")[-1]
                lookup = rel.replace(".csv.gz", "")
                is_smart = lookup in smart_set

                try:
                    with gzip.open(csv_path, "rt") as f:
                        for line in f:
                            line = line.strip()
                            if not line or not line[0].isdigit():
                                continue
                            row = [s.strip() for s in line.split(",")]
                            try:
                                seed = int(row[0])
                            except ValueError:
                                continue
                            if seed not in seeds or seeds[seed] in streamliner_map:
                                continue
                            fname = seeds[seed]
                            if is_smart:
                                run1_out = row[1].lower()
                                run1_solved = ("solved" in run1_out
                                               and "unsolvable" not in run1_out)
                                streamliner_map[fname] = "both" if run1_solved else "none"
                            else:
                                streamliner_map[fname] = "none"
                            found.add(seed)
                except Exception:
                    continue

        # Anything not found in CSVs defaults to none
        for seed, fname in seeds.items():
            if fname not in streamliner_map:
                print(f"  [WARN] {fname}: not found in ground-truth data - defaulting to 'none'")
                streamliner_map[fname] = "none"

    return streamliner_map


# ---------------------------------------------------------------------------
# Oracle generation
# ---------------------------------------------------------------------------

def generate_baseline(exe, instances_dir, output_path, data_dir=None, timeout_ms=30000):
    files = sorted(f for f in os.listdir(instances_dir) if f.endswith(".json"))
    total = len(files)
    print(f"Generating Level 1 oracle for {total} instances...")

    # Determine per-instance streamliner
    if data_dir:
        print(f"Looking up streamliners from ground-truth data in:\n  {data_dir}")
        streamliner_map = build_streamliner_map(data_dir, files)
        n_both = sum(1 for v in streamliner_map.values() if v == "both")
        print(f"  {n_both} instances will use 'both', {total - n_both} will use 'none'")
    else:
        print("No --data-dir supplied; all instances will use 'none' (streamliner).")
        streamliner_map = {f: "none" for f in files}

    oracle = {}
    start = time.time()

    for i, filename in enumerate(files):
        instance_path = os.path.join(instances_dir, filename)
        game_type = filename.split("_seed_")[0]
        streamliner = streamliner_map.get(filename, "none")

        cmd = [exe, instance_path,
               "--type", game_type,
               "--streamliners", streamliner,
               "--timeout", str(timeout_ms),
               "--json"]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True,
                                    timeout=timeout_ms / 1000.0 + 10)
            # Strip any stray non-JSON lines (e.g. log messages)
            json_line = next(
                (l for l in result.stdout.strip().splitlines() if l.startswith("{")),
                None
            )
            if not json_line:
                print(f"  [ERROR] {filename}: no JSON in output")
                print(f"    stdout: {result.stdout!r}")
                continue
            entry = json.loads(json_line)
            entry["instance_name"] = f"level1/instances/{filename}"
            entry["game_type"] = game_type
            entry["streamliner"] = streamliner
            oracle[filename] = entry

            if (i + 1) % 10 == 0 or (i + 1) == total:
                elapsed = time.time() - start
                print(f"  [{i+1}/{total}] {elapsed:.1f}s elapsed "
                      f"({elapsed/(i+1):.2f}s/instance avg)")
        except subprocess.TimeoutExpired:
            print(f"  [ERROR] {filename}: timed out after {timeout_ms}ms")
        except Exception as e:
            print(f"  [ERROR] {filename}: {e}")

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(oracle, f, indent=4)

    n_written = len(oracle)
    print(f"\nOracle written: {output_path}")
    print(f"  {n_written}/{total} instances recorded")
    print(f"  Total time: {time.time() - start:.1f}s")
    if n_written < total:
        print(f"  WARNING: {total - n_written} instances failed and are NOT in the oracle")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate Level 1 regression oracle from JSON instance files")
    parser.add_argument("--exe", required=True,
                        help="Path to solvitaire executable")
    parser.add_argument("--instances", required=True,
                        help="Directory containing Level 1 instance JSON files")
    parser.add_argument("--output", required=True,
                        help="Output path for oracle JSON")
    parser.add_argument("--data-dir",
                        help="Path to solvitaire-paper experimental data directory "
                             "(used to look up per-instance streamliner settings). "
                             "If omitted, all instances are run with --streamliners none.")
    parser.add_argument("--timeout-ms", type=int, default=30000,
                        help="Per-instance solver timeout in ms (default: 30000)")
    args = parser.parse_args()

    generate_baseline(
        exe=args.exe,
        instances_dir=args.instances,
        output_path=args.output,
        data_dir=args.data_dir,
        timeout_ms=args.timeout_ms,
    )
