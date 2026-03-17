import os
import gzip
import csv
import json
import argparse
from pathlib import Path

# Ground Truth Mapping Files
AAA_BASE = "/Users/ipg/Research/ReSolvitaire-project/03-Large-Datasets/solvitaire-paper-v10-Feb2026/AnalysisScripts"
AAA_SMART = ["AAA-smartfiles", "AAA-smartnotimefiles"]
AAA_SINGLE = ["AAA-singlerunfiles", "AAA-singlerunnotimefiles"]

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
for k, v in TARGET_SETS.items():
    v['per_instance_target'] = v['total_target'] / (2 * len(GAMES))

RESULTS_DIR = "/Users/ipg/Research/ReSolvitaire-project/03-Large-Datasets/solvitaire-paper-v10-Feb2026/ExperimentalResults"

def load_aaa_sets():
    smart_set = set()
    single_set = set()
    for f in AAA_SMART:
        path = os.path.join(AAA_BASE, f)
        if os.path.exists(path):
            with open(path, 'r') as fd:
                for line in fd:
                    smart_set.add(line.strip())
    for f in AAA_SINGLE:
        path = os.path.join(AAA_BASE, f)
        if os.path.exists(path):
            with open(path, 'r') as fd:
                for line in fd:
                    single_set.add(line.strip())
    return smart_set, single_set

def get_row_metrics(row, is_smart):
    """Returns (total_time_ms, outcome, removed, states)"""
    row = [s.strip() for s in row]
    if is_smart:
        run1_outcome = row[1].lower()
        if "solved" in run1_outcome:
            # Run 1 solution is the ground truth
            return float(row[2]), "solved", int(row[7]), int(row[3])
        else:
            # Run 1 didn't solve (unsolvable or timeout)
            # Check for Run 2 (starts at index 12)
            if len(row) >= 24:
                # Column 13 is Run 2 time
                # Column 23 is Run 2 final outcome
                # But wait, overall result might be in col 23 or 12?
                # Actually, according to export script logic:
                # If Run 1 was not solved, we use Run 2 metrics.
                r2_time = float(row[13])
                r2_outcome = "solved" if "solved" in row[23].lower() else "unsolvable"
                r2_removed = int(row[18])
                r2_states = int(row[14])
                return r2_time, r2_outcome, r2_removed, r2_states
            else:
                # No run 2? Use Run 1 but it's likely a timeout/unsolvable
                outcome = "solved" if "solved" in run1_outcome else "unsolvable"
                return float(row[2]), outcome, int(row[7]), int(row[3])
    else:
        # Single run
        outcome = "solved" if "solved" in row[1].lower() or (len(row) > 12 and "solved" in row[12].lower()) else "unsolvable"
        return float(row[2]), outcome, int(row[7]), int(row[3])

def find_best_instances(game, target_ms, smart_set, single_set):
    best_winnable = None
    best_unwinnable = None
    
    game_dir = Path(RESULTS_DIR) / game
    if not game_dir.exists():
        return None, None
        
    csv_files = list(game_dir.rglob("*.csv.gz"))
    
    for csv_file in csv_files:
        rel_csv = str(csv_file).split("ExperimentalResults/")[-1]
        lookup_key = rel_csv.replace(".csv.gz", "").replace(".csv", "")
        
        is_smart = lookup_key in smart_set
        if not is_smart and lookup_key not in single_set:
            # Heuristic fallback if not in AAA files
            # But we'll try to be strict if possible
            pass

        try:
            with gzip.open(csv_file, 'rt') as f:
                reader = csv.reader(f)
                for row in reader:
                    if not row or not row[0].isdigit():
                        continue
                    
                    # If not in AAA files, check column count as fallback
                    if lookup_key not in smart_set and lookup_key not in single_set:
                        is_row_smart = len(row) > 15
                    else:
                        is_row_smart = is_smart
                        
                    try:
                        time_ms, outcome, removed, states = get_row_metrics(row, is_row_smart)
                        seed = int(row[0])
                        
                        inst_data = {'seed': seed, 'time': time_ms, 'removed': removed, 'csv': str(csv_file), 'row': row, 'states': states}
                        
                        if outcome == "solved":
                            if best_winnable is None or abs(time_ms - target_ms) < abs(best_winnable['time'] - target_ms):
                                best_winnable = inst_data
                            elif abs(time_ms - target_ms) == abs(best_winnable['time'] - target_ms):
                                if removed < best_winnable['removed']:
                                    best_winnable = inst_data
                        elif outcome == "unsolvable":
                            if best_unwinnable is None or abs(time_ms - target_ms) < abs(best_unwinnable['time'] - target_ms):
                                best_unwinnable = inst_data
                            elif abs(time_ms - target_ms) == abs(best_unwinnable['time'] - target_ms):
                                if removed < best_unwinnable['removed']:
                                    best_unwinnable = inst_data
                    except (ValueError, IndexError):
                        continue
        except Exception as e:
            print(f"Error reading {csv_file}: {e}")
            continue
            
    return best_winnable, best_unwinnable

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--set", choices=TARGET_SETS.keys(), required=True)
    args = parser.parse_args()
    
    smart_set, single_set = load_aaa_sets()
    
    target_set = TARGET_SETS[args.set]
    target_ms = target_set['per_instance_target']
    
    print(f"Searching for set {args.set} (Target per instance: {target_ms:.2f}ms)")
    
    results = {}
    for game in GAMES:
        print(f"Processing {game}...", end='\r')
        w, u = find_best_instances(game, target_ms, smart_set, single_set)
        results[game] = {'winnable': w, 'unwinnable': u}
    
    print("\nSearch complete.")
    
    output_data = {
        'target_set': args.set,
        'target_per_instance_ms': target_ms,
        'instances': results
    }
    
    with open(f"curated_instances_{args.set}.json", "w") as f:
        json.dump(output_data, f, indent=2)
    
    print(f"Results saved to curated_instances_{args.set}.json")

if __name__ == "__main__":
    main()
