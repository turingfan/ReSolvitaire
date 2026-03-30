import os
import gzip
import csv
import json
import argparse
import subprocess
import multiprocessing
from pathlib import Path

# Defaults
DEFAULT_DATA_DIR = "./solvitaire-paper-v10-Feb2026"

GAMES = [
    "accordion", "alpha-star", "american-canister", "bakers-game", "beleaguered-castle",
    "black-hole", "british-canister", "canfield", "canfield-strict", "delta-star",
    "east-haven", "eight-off", "fan", "fore-cell", "fore-cell-same-suit",
    "fortunes-favor", "free-cell", "free-cell-0-cell", "free-cell-1-cell", "free-cell-2-cell",
    "free-cell-3-cell", "free-cell-4-pile", "free-cell-5-pile", "free-cell-6-pile", "free-cell-7-pile",
    "gaps-basic-variant", "gaps-one-deal", "golf", "king-albert", "klondike",
    "klondike-deal-1", "klondike-deal-1-noworryback", "klondike-deal-2", "klondike-deal-2-noworryback",
    "klondike-deal-3-anyspace", "klondike-deal-3-anysuit", "klondike-deal-3-anysuitanyspace",
    "klondike-deal-3-anysuitnospace", "klondike-deal-3-nospace", "klondike-deal-3-noworryback",
    "klondike-deal-3-samesuit", "klondike-deal-3-samesuitanyspace", "klondike-deal-3-samesuitnospace",
    "klondike-deal-4", "klondike-deal-4-noworryback", "klondike-deal-5", "klondike-deal-5-noworryback",
    "klondike-deal-6", "klondike-deal-6-noworryback", "klondike-deal-7", "klondike-deal-7-noworryback",
    "klondike-deal-8", "klondike-deal-8-noworryback", "klondike-deal-9", "klondike-deal-9-noworryback",
    "klondike-deal-10", "klondike-deal-10-noworryback", "klondike-deal-11", "klondike-deal-11-noworryback",
    "klondike-deal-12", "klondike-deal-12-noworryback", "klondike-deal-13", "klondike-deal-13-noworryback",
    "late-binding-solitaire", "mrs-mop", "northwest-territory", "raglan", "seahaven-towers",
    "siegecraft", "simple-simon", "somerset", "spanish-patience", "spider",
    "spiderette", "streets-and-alleys", "stronghold", "thirty", "thirtysix",
    "trigon", "will-o-the-wisp", "worm-hole"
]

TARGET_SETS = {
    "1m": {'total_target': 60000},
    "5m": {'total_target': 300000},
    "1h": {'total_target': 3600000},
    "6h": {'total_target': 21600000}
}

# Maps curation set names to regression level names (for oracle file paths)
LEVEL_MAP = {"1m": "level2", "5m": "level3", "1h": "level4", "6h": "level5"}
for k, v in TARGET_SETS.items():
    v['per_instance_target'] = v['total_target'] / (2 * len(GAMES))

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

def get_row_metrics(row, is_smart, lookup_key=""):
    """Extract metrics from a CSV row, returning all oracle-ready fields.

    Returns a dict with keys:
        time_ms, outcome, removed, states_searched,
        unique_states, backtracks, max_depth, streamliner
    or None if the instance should be skipped (timeout, memout, etc.).

    For backwards compatibility, callers that only need (time_ms, outcome,
    removed, states) can destructure: m['time_ms'], m['outcome'], etc.
    """
    row = [s.strip() for s in row]
    try:
        if is_smart:
            run1_outcome = row[1].lower()
            if "solved" in run1_outcome:
                removed = int(row[7])
                if removed > 0:
                    return None
                return {
                    'time_ms': float(row[2]), 'outcome': "solved",
                    'removed': removed,
                    'states_searched': int(row[3]), 'unique_states': int(row[4]),
                    'backtracks': int(row[5]), 'max_depth': int(row[10]),
                    'streamliner': "both"
                }

            # If run 1 is non-definitive, skip
            if any(x in run1_outcome for x in ["timeout", "limit", "interrupted"]):
                return None

            # Run 1 unsolvable. Check for Run 2.
            if len(row) >= 24:
                r2_outcome_str = row[23].lower()
                if any(x in r2_outcome_str for x in ["timeout", "limit", "interrupted"]):
                    return None
                r2_removed = int(row[18])
                if r2_removed > 0:
                    return None
                return {
                    'time_ms': float(row[13]),
                    'outcome': "solved" if "solved" in r2_outcome_str else "unsolvable",
                    'removed': r2_removed,
                    'states_searched': int(row[14]), 'unique_states': int(row[15]),
                    'backtracks': int(row[16]), 'max_depth': int(row[21]),
                    'streamliner': "none"
                }
            else:
                # No run 2, and run 1 was unsolvable.
                removed = int(row[7])
                if removed > 0:
                    return None
                return {
                    'time_ms': float(row[2]), 'outcome': "unsolvable",
                    'removed': removed,
                    'states_searched': int(row[3]), 'unique_states': int(row[4]),
                    'backtracks': int(row[5]), 'max_depth': int(row[10]),
                    'streamliner': "none"
                }
        else:
            # Single run
            outcome_str = row[1].lower()
            if any(x in outcome_str for x in ["timeout", "limit", "interrupted"]):
                return None

            removed = int(row[7])
            if removed > 0:
                return None

            if len(row) > 12:
                overall = row[12].lower()
                if any(x in overall for x in ["timeout", "limit", "interrupted"]):
                    return None
                outcome = "solved" if "solved" in overall or "solved" in outcome_str else "unsolvable"
            else:
                outcome = "solved" if "solved" in outcome_str else "unsolvable"

            streamliner = "both" if "-both-" in lookup_key else "none"
            return {
                'time_ms': float(row[2]), 'outcome': outcome,
                'removed': removed,
                'states_searched': int(row[3]), 'unique_states': int(row[4]),
                'backtracks': int(row[5]), 'max_depth': int(row[10]),
                'streamliner': streamliner
            }
    except:
        return None

def find_best_instances(game, target_ms, smart_set, single_set, results_dir):
    best_winnable = None
    best_unwinnable = None
    
    game_dir = Path(results_dir) / game
    if not game_dir.exists():
        return None, None
        
    csv_files = list(game_dir.rglob("*.csv.gz"))
    
    # x5 time restriction
    max_time_ms = 5 * target_ms
    
    row_count = 0
    
    for csv_file in csv_files:
        rel_csv = str(csv_file).split("ExperimentalResults/")[-1]
        lookup_key = rel_csv.replace(".csv.gz", "").replace(".csv", "")
        is_smart = lookup_key in smart_set
        
        try:
            with gzip.open(csv_file, 'rt') as f:
                # Use plain line-by-line reading for speed
                for line in f:
                    if not line or not line[0].isdigit():
                        continue
                    
                    row = line.split(',')
                    row_count += 1
                    
                    # Heuristic fallback if not in AAA files
                    if lookup_key not in smart_set and lookup_key not in single_set:
                        is_row_smart = len(row) > 15
                    else:
                        is_row_smart = is_smart

                    metrics = get_row_metrics(row, is_row_smart, lookup_key)
                    if metrics is None or metrics['time_ms'] > max_time_ms:
                        continue

                    time_ms = metrics['time_ms']
                    outcome = metrics['outcome']
                    seed = int(row[0])
                    inst_data = {
                        'seed': seed, 'time': time_ms,
                        'removed': metrics['removed'],
                        'csv': str(csv_file), 'row': row,
                        'states': metrics['states_searched'],
                        'oracle': metrics  # full oracle-ready data
                    }

                    diff = abs(time_ms - target_ms) / target_ms if target_ms > 0 else 0
                    
                    # Selection and Early Termination Logic
                    if outcome == "solved":
                        if best_winnable is None or diff < abs(best_winnable['time'] - target_ms) / target_ms:
                            best_winnable = inst_data
                        
                        # Stop immediately if very close
                        if diff <= 0.05:
                            # We found a "good enough" winnable, but we still need an unwinnable.
                            pass
                    elif outcome == "unsolvable":
                        if best_unwinnable is None or diff < abs(best_unwinnable['time'] - target_ms) / target_ms:
                            best_unwinnable = inst_data
                        
                        if diff <= 0.05:
                            pass

                    # Progress-based Early Termination
                    if best_winnable and best_unwinnable:
                        w_diff = abs(best_winnable['time'] - target_ms) / target_ms
                        u_diff = abs(best_unwinnable['time'] - target_ms) / target_ms
                        
                        if w_diff <= 0.05 and u_diff <= 0.05:
                            return best_winnable, best_unwinnable
                        if row_count > 1000 and w_diff <= 0.10 and u_diff <= 0.10:
                            return best_winnable, best_unwinnable
                        if row_count > 2500 and w_diff <= 0.25 and u_diff <= 0.25:
                            return best_winnable, best_unwinnable

        except Exception as e:
            continue
            
    return best_winnable, best_unwinnable

def process_game(args):
    game, target_ms, smart_set, single_set, results_dir = args
    print(f"Processing {game:30}...")
    w, u = find_best_instances(game, target_ms, smart_set, single_set, results_dir)
    return game, w, u

RULES_ROOT = "tests/rules"

def generate_oracle(set_name, curated_data):
    """Generate oracle JSON directly from curated instance data.

    This is called immediately after curation so the oracle is always
    in sync with the curated instances.  The oracle metrics come from
    the 'oracle' dict stored at curation time — no re-derivation from
    raw CSV rows is needed.
    """
    level = LEVEL_MAP[set_name]
    oracle = []

    for game, instances in curated_data['instances'].items():
        for outcome_key in ['winnable', 'unwinnable']:
            inst = instances.get(outcome_key)
            if inst is None:
                continue

            m = inst.get('oracle')
            if m is None:
                print(f"Warning: {game} {outcome_key} has no oracle data, skipping")
                continue

            # Skip memout instances (belt-and-suspenders; curation should
            # already have excluded these, but guard against stale data)
            if m.get('removed', 0) > 0:
                print(f"Skipping {game} seed {inst['seed']} ({outcome_key}): "
                      f"removed={m['removed']} (memout)")
                continue

            seed = inst['seed']
            final_outcome = "winnable" if m['outcome'] == "solved" else "unwinnable"
            filename = f"{game}_{seed}_{final_outcome}.json"
            rel_path = f"resources/{level}/{filename}"

            oracle_entry = {
                "instance": rel_path,
                "states_searched": m['states_searched'],
                "unique_states": m['unique_states'],
                "backtracks": m['backtracks'],
                "max_depth": m['max_depth'],
                "solution_type": "solved" if m['outcome'] == "solved" else "unsolvable",
                "baseline_time_ms": m['time_ms'],
                "streamliner": m['streamliner']
            }

            custom_rules_path = os.path.join(RULES_ROOT, f"{game}.json")
            if os.path.exists(custom_rules_path):
                oracle_entry["custom_rules"] = custom_rules_path
            else:
                oracle_entry["game_type"] = game

            oracle.append(oracle_entry)

    oracle_path = os.path.join("tests", "oracles", f"{level}.json")
    os.makedirs(os.path.dirname(oracle_path), exist_ok=True)
    with open(oracle_path, 'w') as f:
        json.dump(oracle, f, indent=2)
    print(f"Generated oracle: {oracle_path} ({len(oracle)} instances)")
    return oracle_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--set", choices=TARGET_SETS.keys(), required=True)
    parser.add_argument("--data-dir", default=DEFAULT_DATA_DIR, help="Base directory for the experimental data repository.")
    args = parser.parse_args()

    results_dir = os.path.join(args.data_dir, "ExperimentalResults")
    if not os.path.exists(results_dir):
        print(f"Error: ExperimentalResults directory not found at {results_dir}")
        return

    smart_set, single_set = load_aaa_sets(args.data_dir)
    target_set = TARGET_SETS[args.set]
    target_ms = target_set['per_instance_target']

    print(f"Searching for set {args.set} (Target: {target_ms:.2f}ms, Max: {target_ms*5:.2f}ms)")

    tasks = [(g, target_ms, smart_set, single_set, results_dir) for g in GAMES]

    num_procs = min(multiprocessing.cpu_count(), 8)
    results = {}
    with multiprocessing.Pool(processes=num_procs) as pool:
        for game, w, u in pool.imap_unordered(process_game, tasks):
            results[game] = {'winnable': w, 'unwinnable': u}

    print("\nSearch complete.")

    # Summary of gaps
    missing_w = [g for g, r in results.items() if r['winnable'] is None]
    missing_u = [g for g, r in results.items() if r['unwinnable'] is None]

    if missing_w: print(f"Missing winnable for: {', '.join(missing_w)}")
    if missing_u: print(f"Missing unwinnable for: {', '.join(missing_u)}")

    output_data = {
        'target_set': args.set,
        'target_per_instance_ms': target_ms,
        'instances': results
    }

    output_path = f"tests/resources/curated_sets/curated_instances_{args.set}.json"
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(output_data, f, indent=2)

    print(f"Curated instances saved to {output_path}")

    # Generate oracle immediately so it is always in sync with curation
    generate_oracle(args.set, output_data)

if __name__ == "__main__":
    main()
