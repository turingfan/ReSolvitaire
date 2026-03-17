import os
import json
import subprocess
import argparse
from pathlib import Path

SOLVITAIRE_BIN = "cmake-build-release/bin/solvitaire"
RESOURCES_ROOT = "tests/resources"
RULES_ROOT = "tests/rules"
ORACLES_ROOT = "tests/oracles"
SETS = ["1m", "5m", "1h", "6h"]
LEVEL_MAP = {"1m": "level2", "5m": "level3", "1h": "level4", "6h": "level5"}

# Placeholder for the experimental data repository.
# Users can specify this via the --data-dir argument.
DEFAULT_DATA_DIR = "./solvitaire-paper-v10-Feb2026"

def get_deal_json(game, seed, custom_rules=None):
    if custom_rules:
        cmd = [SOLVITAIRE_BIN, "--random", str(seed), "--custom-rules", custom_rules, "--deal-only", "--json", "--reveal-hidden"]
    else:
        cmd = [SOLVITAIRE_BIN, "--random", str(seed), "--type", game, "--deal-only", "--json", "--reveal-hidden"]
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return None
    return result.stdout

def export_set(set_name, data_dir):
    json_path = f"curated_instances_{set_name}.json"
    if not os.path.exists(json_path):
        print(f"Skipping {set_name}: {json_path} not found")
        return

    with open(json_path, 'r') as f:
        data = json.load(f)

    level = LEVEL_MAP[set_name]
    output_dir = Path(RESOURCES_ROOT) / level
    output_dir.mkdir(parents=True, exist_ok=True)
    
    results_base = os.path.join(data_dir, "ExperimentalResults")

    oracle = []
    
    instances = data['instances']
    print(f"Processing {len(instances)} games for {set_name}")
    for game, types in instances.items():
        for status, inst in types.items():
            if not inst: continue
            
            # Resolve custom rules if applicable
            custom_rules = None
            if game in ["accordion", "gaps-basic-variant", "gaps-one-deal"]:
                rule_file = f"tests/rules/{game}.json"
                if os.path.exists(rule_file):
                    custom_rules = rule_file

            deal_json = get_deal_json(game, inst['seed'], custom_rules)
            if deal_json:
                # ...
                filename = f"{game}_{inst['seed']}_{status}.json"
                with open(output_dir / filename, "w") as f:
                    f.write(deal_json)
                
                # Determine streamliner
                # The curated JSON now uses relative paths for 'csv'
                csv_rel = inst['csv']
                smart_files = ["AAA-smartfiles", "AAA-smartnotimefiles"]
                is_smart = False
                aaa_base = os.path.join(data_dir, "AnalysisScripts")
                for f_name in smart_files:
                    path = os.path.join(aaa_base, f_name)
                    if os.path.exists(path):
                        with open(path, 'r') as fd:
                            if any(line.strip() in csv_rel for line in fd):
                                is_smart = True
                                break
                
                streamliner = "both" if is_smart else "none"
                
                oracle.append({
                    "instance": f"tests/resources/{level}/{filename}",
                    "states_searched": inst['states'],
                    "unique_states": int(inst['row'][9]) if len(inst['row']) > 9 else 0, # Best guess for unique states if not in row
                    "backtracks": int(inst['row'][5]) if len(inst['row']) > 5 else 0,
                    "max_depth": int(inst['row'][6]) if len(inst['row']) > 6 else 0,
                    "solution_type": "solved" if status == "winnable" else "unsolvable",
                    "baseline_time_ms": inst['time'],
                    "streamliner": streamliner,
                    "custom_rules": custom_rules
                })

    oracle_file = Path(ORACLES_ROOT) / f"{level}.json"
    with open(oracle_file, "w") as f:
        json.dump(oracle, f, indent=2)
    print(f"Generated {oracle_file} ({len(oracle)} instances)")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", default=DEFAULT_DATA_DIR, help="Base directory for the experimental data repository.")
    args = parser.parse_args()

    for s in SETS:
        export_set(s, args.data_dir)

if __name__ == "__main__":
    main()
