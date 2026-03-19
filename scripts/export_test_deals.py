import os
import json
import subprocess
from pathlib import Path

SOLVITAIRE_BIN = "cmake-build-release/bin/solvitaire"
RESOURCES_ROOT = "tests/resources"
RULES_ROOT = "tests/rules"
SETS = ["1m", "5m", "1h", "6h"]
LEVEL_MAP = {"1m": "level2", "5m": "level3", "1h": "level4", "6h": "level5"}

# Defaults
DEFAULT_DATA_DIR = "./solvitaire-paper-v10-Feb2026"

def load_aaa_sets(data_dir):
    aaa_base = os.path.join(data_dir, "AnalysisScripts")
    smart_set = set()
    single_set = set()
    
    smart_files = ["AAA-smartfiles", "AAA-smartnotimefiles"]
    single_files = ["AAA-singlerunfiles", "AAA-singlerunnotimefiles"]
    
    for f in smart_files:
        path = os.path.join(aaa_base, f)
        if os.path.exists(path):
            with open(path, 'r') as fd:
                for line in fd:
                    smart_set.add(line.strip())
                    
    for f in single_files:
        path = os.path.join(aaa_base, f)
        if os.path.exists(path):
            with open(path, 'r') as fd:
                for line in fd:
                    single_set.add(line.strip())
                    
    return smart_set, single_set

def export_deals(data_dir):
    smart_set, single_set = load_aaa_sets(data_dir)
    
    for set_name in SETS:
        level = LEVEL_MAP[set_name]
        json_file = f"tests/resources/curated_sets/curated_instances_{set_name}.json"
        if not os.path.exists(json_file):
            print(f"Skipping {json_file}, not found.")
            continue
            
        with open(json_file, 'r') as f:
            data = json.load(f)
            
        oracle = []
        
        for game, instances in data['instances'].items():
            for outcome_key in ['winnable', 'unwinnable']:
                inst = instances[outcome_key]
                if inst is None:
                    continue

                # Skip instances whose original run exhausted the cache
                # (states_removed_from_cache > 0).  Such runs may have missed
                # states and produced incorrect unsolvable verdicts (memout).
                if inst.get('removed', 0) > 0:
                    print(f"Skipping {game} seed {inst['seed']} ({outcome_key}): "
                          f"removed={inst['removed']} (memout/cache exhaustion)")
                    continue

                seed = inst['seed']
                row = [s.strip() for s in inst['row']]
                csv_path = inst['csv']
                
                # Normalize CSV path for AAA lookup
                # Example: .../ExperimentalResults/game/vX.X/filename.csv.gz -> game/vX.X/filename
                rel_csv = csv_path.split("ExperimentalResults/")[-1]
                lookup_key = rel_csv.replace(".csv.gz", "").replace(".csv", "")
                
                # Determine "Smart" vs "Single" from Ground Truth Mapping
                is_smart = lookup_key in smart_set
                # If not in either, use column count as heuristic
                if not is_smart and lookup_key not in single_set:
                    is_smart = len(row) > 15 
                    if is_smart:
                        print(f"Heuristic fallback: {lookup_key} treated as SMART")

                streamliner = "none"
                if is_smart:
                    # Smart (Multi-run) Logic:
                    # Run 1: Cols 1-11
                    # Run 2: Cols 12-23 (if present)
                    run1_outcome = row[1].lower()
                    
                    if "solved" in run1_outcome:
                        # Run 1 solution is trusted
                        streamliner = "both"
                        time_ms = float(row[2])
                        idx_states = 3
                        idx_unique = 4
                        idx_backtracks = 5
                        idx_max_depth = 10
                        final_outcome = "winnable"
                    else:
                        # Run 1 unsolvable or timeout -> Rerun (NONE) is ground truth
                        streamliner = "none"
                        if len(row) >= 24:
                            # Run 2 metrics
                            time_ms = float(row[13])
                            idx_states = 14
                            idx_unique = 15
                            idx_backtracks = 16
                            idx_max_depth = 21
                            final_outcome = "winnable" if "solved" in row[23].lower() else "unsolvable"
                        else:
                            # Fallback if Run 2 is missing (unexpected for smart)
                            time_ms = float(row[2])
                            idx_states = 3
                            idx_unique = 4
                            idx_backtracks = 5
                            idx_max_depth = 10
                    # Determine definitive outcome
                    if "solved" in run1_outcome:
                        final_outcome = "winnable"
                    elif "unsolvable" in run1_outcome:
                        final_outcome = "unwinnable"
                    else:
                        print(f"Warning: Non-definitive outcome for {game} seed {seed}: {run1_outcome}")
                        continue
                else:
                    # Single-run (NONE) Logic:
                    if "-both-" in lookup_key:
                        streamliner = "both"
                    else:
                        streamliner = "none"
                        
                    time_ms = float(row[2])
                    idx_states = 3
                    idx_unique = 4
                    idx_backtracks = 5
                    idx_max_depth = 10
                    
                    outcome_str = row[1].lower()
                    if "solved" in outcome_str:
                        final_outcome = "winnable"
                    elif "unsolvable" in outcome_str:
                        final_outcome = "unwinnable"
                    else:
                        print(f"Warning: Non-definitive outcome for {game} seed {seed}: {outcome_str}")
                        continue

                try:
                    states = int(row[idx_states])
                    unique = int(row[idx_unique])
                    backtracks = int(row[idx_backtracks])
                    max_depth = int(row[idx_max_depth])
                except (ValueError, IndexError):
                    print(f"Warning: Missing metrics for {game} seed {seed} in {csv_path}")
                    continue

                # Deal Export
                filename = f"{game}_{seed}_{final_outcome}.json"
                rel_path = f"resources/{level}/{filename}"
                abs_path = os.path.join(RESOURCES_ROOT, level, filename)
                
                if not os.path.exists(os.path.dirname(abs_path)):
                    os.makedirs(os.path.dirname(abs_path), exist_ok=True)
                
                # Check for custom rules
                custom_rules_path = os.path.join(RULES_ROOT, f"{game}.json")
                use_custom_rules = os.path.exists(custom_rules_path)
                
                cmd = [
                    SOLVITAIRE_BIN,
                    "--deal-only",
                    "--random", str(seed),
                    "--reveal-hidden"
                ]
                if use_custom_rules:
                    cmd.extend(["--custom-rules", custom_rules_path])
                else:
                    cmd.extend(["--type", game])
                
                try:
                    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
                    with open(abs_path, 'w') as df:
                        df.write(res.stdout)
                        
                    oracle_entry = {
                        "instance": rel_path,
                        "states_searched": states,
                        "unique_states": unique,
                        "backtracks": backtracks,
                        "max_depth": max_depth,
                        "solution_type": "solved" if final_outcome == "winnable" else "unsolvable",
                        "baseline_time_ms": time_ms,
                        "streamliner": streamliner
                    }
                    if use_custom_rules:
                        oracle_entry["custom_rules"] = os.path.join("tests/rules", f"{game}.json")
                    else:
                        oracle_entry["game_type"] = game
                        
                    oracle.append(oracle_entry)
                except Exception as e:
                    print(f"Failed to export {game} seed {seed}: {e}")
                    
        # Write oracle for this level
        oracle_path = os.path.join("tests", "oracles", f"{level}.json")
        os.makedirs(os.path.dirname(oracle_path), exist_ok=True)
        with open(oracle_path, 'w') as f:
            json.dump(oracle, f, indent=2)
        print(f"Generated {oracle_path} ({len(oracle)} instances)")

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", default=DEFAULT_DATA_DIR, help="Base directory of the dataset")
    args = parser.parse_args()
    export_deals(args.data_dir)
if __name__ == "__main__":
    main()
