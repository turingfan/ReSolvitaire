#!/usr/bin/env python3
import os
import subprocess
import json
import argparse
import sys

def run_regression(solver_path, instances_dir, oracle_path):
    if not os.path.exists(solver_path):
        print(f"Error: Solver not found at {solver_path}")
        return 1
    if not os.path.exists(instances_dir):
        print(f"Error: Instances directory not found at {instances_dir}")
        return 1
    if not os.path.exists(oracle_path):
        print(f"Error: Oracle not found at {oracle_path}")
        return 1

    with open(oracle_path, 'r') as f:
        oracle = json.load(f)

    instances = sorted([f for f in os.listdir(instances_dir) if f.endswith(".json")])
    total = len(instances)
    failed = 0
    passed = 0

    print(f"Running Level 1 Regression: {total} instances")
    print("-" * 60)

    for filename in instances:
        instance_path = os.path.join(instances_dir, filename)
        game_type = filename.split("_seed_")[0]
        
        if filename not in oracle:
            print(f"[SKIP] {filename} (Not in oracle)")
            continue

        baseline = oracle[filename]
        
        cmd = [solver_path, instance_path, "--type", game_type, "--json"]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            if result.returncode != 0:
                print(f"[FAIL] {filename} (Solver crashed with return code {result.returncode})")
                print(f"Stderr: {result.stderr}")
                failed += 1
                continue

            output = json.loads(result.stdout.strip())
            
            # Comparison Logic
            diffs = []
            mapping = {
                "solution_type": "solution_type",
                "states_searched": "states_searched",
                "backtracks": "backtracks"
            }
            for out_key, baseline_key in mapping.items():
                if out_key not in output:
                    diffs.append(f"Missing key '{out_key}' in output")
                    continue
                if output[out_key] != baseline.get(baseline_key):
                    diffs.append(f"{out_key}: {output[out_key]} (expected {baseline.get(baseline_key)})")
            
            if diffs:
                print(f"[FAIL] {filename}")
                for d in diffs:
                    print(f"  - {d}")
                failed += 1
            else:
                passed += 1

        except subprocess.TimeoutExpired:
            print(f"[FAIL] {filename} (Timeout)")
            failed += 1
        except Exception as e:
            print(f"[ERROR] {filename}: {e}")
            failed += 1

    print("-" * 60)
    print(f"Passed: {passed}/{total}")
    if failed > 0:
        print(f"FAILED: {failed}/{total}")
        return 1
    
    print("Regression suite passed successfully.")
    return 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Solvitaire Level 1 Regression Runner")
    parser.add_argument("--exe", required=True, help="Path to solvitaire executable")
    parser.add_argument("--instances", required=True, help="Path to instances directory")
    parser.add_argument("--oracle", required=True, help="Path to baseline oracle JSON")
    
    args = parser.parse_args()
    sys.exit(run_regression(args.exe, args.instances, args.oracle))
